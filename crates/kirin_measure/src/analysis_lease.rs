//! Process-wide ownership for at most two optional POST Analysis workers.
//!
//! Each slot has its own stable per-process file that is intentionally retained after release.
//! Removing a lock file can race a new owner that already opened the old inode, allowing an extra
//! owner on a replacement file. The kernel releases every held slot automatically on close or
//! process crash.
//!
//! Pair names are presentation metadata, not lock authority. They stay in a process-local registry
//! keyed by the exact stable lease path, avoiding reads from a Windows-locked file. A holder ID
//! prevents a late release from clearing a replacement owner; an unknown owner fails closed to the
//! name-free UI status.

// Admission research uses the real kernel slots but is not a product start route. Record scope,
// output receipts, and the return-level state must be integrated before exposing local Blind.
#[cfg(test)]
#[path = "analysis_blind_admission_probe.rs"]
mod blind_admission_probe;

use std::collections::HashMap;
use std::fs::{File, OpenOptions, TryLockError};
use std::io;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Mutex, OnceLock};
#[path = "reference_capture_admission.rs"]
pub mod capture;
#[path = "reference_analysis_owner.rs"]
pub mod reference_owner;

#[cfg(not(test))]
use crate::storage::PlatformPaths;

#[cfg(unix)]
use std::os::unix::fs::OpenOptionsExt;

pub const ANALYSIS_SLOT_COUNT: usize = 2;
pub const ANALYSIS_OWNER_NAME_MAX_BYTES: usize = 64;

#[derive(Clone, Debug)]
struct RegisteredOwner {
    lease_id: u64,
    name: String,
}

static NEXT_LEASE_ID: AtomicU64 = AtomicU64::new(1);
static OWNER_REGISTRY: OnceLock<Mutex<HashMap<PathBuf, RegisteredOwner>>> = OnceLock::new();

fn owner_registry() -> &'static Mutex<HashMap<PathBuf, RegisteredOwner>> {
    OWNER_REGISTRY.get_or_init(|| Mutex::new(HashMap::new()))
}

fn register_owner(path: &Path, lease_id: u64, name: &str) {
    let mut registry = match owner_registry().lock() {
        Ok(registry) => registry,
        Err(poisoned) => poisoned.into_inner(),
    };
    registry.insert(
        path.to_path_buf(),
        RegisteredOwner {
            lease_id,
            name: name.to_string(),
        },
    );
}

fn registered_owner(path: &Path) -> String {
    let registry = match owner_registry().lock() {
        Ok(registry) => registry,
        Err(poisoned) => poisoned.into_inner(),
    };
    registry
        .get(path)
        .map(|owner| owner.name.clone())
        .unwrap_or_default()
}

fn unregister_owner(path: &Path, lease_id: u64) {
    let mut registry = match owner_registry().lock() {
        Ok(registry) => registry,
        Err(poisoned) => poisoned.into_inner(),
    };
    if registry
        .get(path)
        .is_some_and(|owner| owner.lease_id == lease_id)
    {
        registry.remove(path);
    }
}

#[derive(Debug)]
pub(super) struct AnalysisLease {
    lease_id: u64,
    paths: Vec<PathBuf>,
    held: Option<(usize, PathBuf, File, String)>,
    observed_owner_names: [String; ANALYSIS_SLOT_COUNT],
}

impl AnalysisLease {
    #[cfg(not(test))]
    pub(super) fn for_current_process() -> Self {
        let root = PlatformPaths::current_kirin_tmp_root().join("analysis");
        Self::at_paths([
            root.join(format!("{}.0.lease", std::process::id())),
            root.join(format!("{}.1.lease", std::process::id())),
        ])
    }

    #[cfg(test)]
    pub(super) fn at_path(path: PathBuf) -> Self {
        Self::at_paths([path])
    }

    pub(super) fn at_paths(paths: impl IntoIterator<Item = PathBuf>) -> Self {
        Self {
            lease_id: NEXT_LEASE_ID.fetch_add(1, Ordering::Relaxed),
            paths: paths.into_iter().collect(),
            held: None,
            observed_owner_names: Default::default(),
        }
    }

    #[cfg(test)]
    pub(super) fn try_acquire(&mut self) -> io::Result<bool> {
        self.try_acquire_for("")
    }

    pub(super) fn try_acquire_for(&mut self, owner_name: &str) -> io::Result<bool> {
        self.observed_owner_names = Default::default();
        if let Some((_, path, _, published_name)) = self.held.as_mut() {
            if published_name.as_str() != owner_name {
                let sanitized = crate::sanitize_name(owner_name);
                if *published_name != sanitized {
                    register_owner(path, self.lease_id, &sanitized);
                    *published_name = sanitized;
                }
            }
            return Ok(true);
        }
        let owner_name = crate::sanitize_name(owner_name);
        if self.paths.is_empty() {
            return Err(io::Error::new(
                io::ErrorKind::InvalidInput,
                "analysis lease has no slots",
            ));
        }
        let mut first_error = None;
        let mut occupied = false;
        for (index, path) in self.paths.iter().enumerate() {
            let Some(parent) = path.parent() else {
                first_error.get_or_insert_with(|| {
                    io::Error::new(
                        io::ErrorKind::InvalidInput,
                        "analysis lease has no parent directory",
                    )
                });
                continue;
            };
            if let Err(error) = std::fs::create_dir_all(parent) {
                first_error.get_or_insert(error);
                continue;
            }
            let mut options = OpenOptions::new();
            options.read(true).write(true).create(true);
            #[cfg(unix)]
            options.mode(0o600);
            let file = match options.open(path) {
                Ok(file) => file,
                Err(error) => {
                    first_error.get_or_insert(error);
                    continue;
                }
            };
            match file.try_lock() {
                Ok(()) => {
                    register_owner(path, self.lease_id, &owner_name);
                    self.held = Some((index, path.clone(), file, owner_name));
                    return Ok(true);
                }
                Err(TryLockError::WouldBlock) => {
                    occupied = true;
                    if index < self.observed_owner_names.len() {
                        self.observed_owner_names[index] = registered_owner(path);
                    }
                }
                Err(TryLockError::Error(error)) => {
                    first_error.get_or_insert(error);
                }
            }
        }
        match first_error {
            _ if occupied => Ok(false),
            Some(error) => Err(error),
            None => Ok(false),
        }
    }

    pub(super) fn release(&mut self) {
        if let Some((_, path, file, _)) = self.held.take() {
            unregister_owner(&path, self.lease_id);
            let _ = file.unlock();
        }
        self.observed_owner_names = Default::default();
    }

    pub(super) fn observed_owner_names(&self) -> [String; ANALYSIS_SLOT_COUNT] {
        self.observed_owner_names.clone()
    }

    #[cfg(test)]
    pub(super) fn held_slot(&self) -> Option<usize> {
        self.held.as_ref().map(|(index, _, _, _)| *index)
    }
}

impl Drop for AnalysisLease {
    fn drop(&mut self) {
        self.release();
    }
}

/// Non-RT admission for one comparison audition. It acquires one of the two optional Analysis
/// slots first, then process- and project-wide audition slots. Partial acquisition is rolled back.
/// Dropping the value releases every kernel lock after a host or plug-in crash as well.
#[derive(Debug)]
pub struct AuditionAdmission {
    analysis: AnalysisLease,
    process_audition: AnalysisLease,
    project_audition: crate::project_audition_lease::ProjectAuditionLease,
    held: bool,
    shared_analysis: Option<reference_owner::ReferenceAnalysisGrant>,
    capture_barrier: capture::CaptureBarrier,
}

impl AuditionAdmission {
    #[cfg(not(test))]
    pub fn for_current_project(plugin_data_dir: &Path, project_hash: &str) -> Self {
        let root = PlatformPaths::current_kirin_tmp_root().join("analysis");
        let process = std::process::id();
        Self::at_paths(
            [
                root.join(format!("{process}.0.lease")),
                root.join(format!("{process}.1.lease")),
            ],
            root.join(format!("{process}.audition.lease")),
            plugin_data_dir,
            project_hash,
        )
    }

    fn at_paths(
        analysis_paths: impl IntoIterator<Item = PathBuf>,
        process_audition_path: PathBuf,
        plugin_data_dir: &Path,
        project_hash: &str,
    ) -> Self {
        let process_barrier = process_audition_path.with_extension("capture-barrier");
        Self {
            analysis: AnalysisLease::at_paths(analysis_paths),
            process_audition: AnalysisLease::at_paths([process_audition_path]),
            project_audition: crate::project_audition_lease::ProjectAuditionLease::new(
                plugin_data_dir,
                project_hash,
            ),
            held: false,
            shared_analysis: None,
            capture_barrier: capture::CaptureBarrier::new(
                process_barrier,
                crate::reservation::reservation_dir(plugin_data_dir, project_hash)
                    .join(".capture-barrier"),
            ),
        }
    }

    pub fn try_acquire_for(&mut self, owner_name: &str) -> io::Result<bool> {
        if self.held {
            return Ok(true);
        }
        if self.shared_analysis.is_none() && !self.analysis.try_acquire_for(owner_name)? {
            return Ok(false);
        }
        if !self.process_audition.try_acquire_for(owner_name)? {
            self.analysis.release();
            return Ok(false);
        }
        match self.project_audition.try_acquire() {
            Ok(true) => {
                self.held = true;
                Ok(true)
            }
            Ok(false) => {
                self.process_audition.release();
                self.analysis.release();
                Ok(false)
            }
            Err(error) => {
                self.process_audition.release();
                self.analysis.release();
                Err(error)
            }
        }
    }

    pub fn release(&mut self) {
        self.project_audition.release();
        self.process_audition.release();
        self.analysis.release();
        self.held = false;
        self.shared_analysis = None;
        self.capture_barrier.release();
    }

    pub fn try_acquire_blind_for(&mut self, owner: &str) -> io::Result<bool> {
        if !self.capture_barrier.acquire()? {
            return Ok(false);
        }
        match self.try_acquire_for(owner) {
            Ok(true) => Ok(true),
            other => {
                self.capture_barrier.release();
                other
            }
        }
    }

    pub fn held(&self) -> bool {
        self.held
    }
}

#[cfg(test)]
#[path = "analysis_lease_slot_tests.rs"]
mod tests;
