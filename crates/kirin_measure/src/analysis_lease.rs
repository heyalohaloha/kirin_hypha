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
        Self {
            analysis: AnalysisLease::at_paths(analysis_paths),
            process_audition: AnalysisLease::at_paths([process_audition_path]),
            project_audition: crate::project_audition_lease::ProjectAuditionLease::new(
                plugin_data_dir,
                project_hash,
            ),
            held: false,
        }
    }

    pub fn try_acquire_for(&mut self, owner_name: &str) -> io::Result<bool> {
        if self.held {
            return Ok(true);
        }
        if !self.analysis.try_acquire_for(owner_name)? {
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
    }

    pub fn held(&self) -> bool {
        self.held
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn two_owners_hold_distinct_stable_kernel_slots_and_third_waits() {
        let temp = tempfile::tempdir().unwrap();
        let paths = [
            temp.path().join("analysis.0.lease"),
            temp.path().join("analysis.1.lease"),
        ];
        let make_lease = || AnalysisLease::at_paths(paths.clone());
        let mut first = make_lease();
        let mut second = make_lease();
        let mut third = make_lease();
        assert!(first.try_acquire_for("Mix").unwrap());
        assert!(second.try_acquire_for("Vocal").unwrap());
        assert_eq!(first.held_slot(), Some(0));
        assert_eq!(second.held_slot(), Some(1));
        assert!(!third.try_acquire_for("Music").unwrap());
        assert_eq!(
            third.observed_owner_names(),
            ["Mix".to_string(), "Vocal".to_string()]
        );
        first.release();
        assert!(third.try_acquire_for("Music").unwrap());
        assert_eq!(third.held_slot(), Some(0));
    }

    #[test]
    fn held_owner_rename_is_published_without_releasing_its_slot() {
        let temp = tempfile::tempdir().unwrap();
        let paths = [
            temp.path().join("analysis.0.lease"),
            temp.path().join("analysis.1.lease"),
        ];
        let mut owner = AnalysisLease::at_paths(paths.clone());
        let mut observer = AnalysisLease::at_paths(paths);
        assert!(owner.try_acquire_for("Mix").unwrap());
        assert!(owner.try_acquire_for("2Mix").unwrap());
        assert!(observer.try_acquire_for("Vocal").unwrap());
        let mut waiting = AnalysisLease::at_paths([
            temp.path().join("analysis.0.lease"),
            temp.path().join("analysis.1.lease"),
        ]);
        assert!(!waiting.try_acquire_for("Music").unwrap());
        assert_eq!(
            waiting.observed_owner_names(),
            ["2Mix".to_string(), "Vocal".to_string()]
        );
    }

    #[test]
    fn unknown_kernel_owner_falls_back_without_inventing_a_name() {
        let temp = tempfile::tempdir().unwrap();
        let path = temp.path().join("analysis.lease");
        let external = OpenOptions::new()
            .read(true)
            .write(true)
            .create(true)
            .truncate(false)
            .open(&path)
            .unwrap();
        external.try_lock().unwrap();

        let mut observer = AnalysisLease::at_path(path);
        assert!(!observer.try_acquire_for("Music").unwrap());
        assert_eq!(
            observer.observed_owner_names(),
            [String::new(), String::new()]
        );
    }

    #[test]
    fn invalid_parent_fails_without_claiming() {
        let temp = tempfile::tempdir().unwrap();
        let blocker = temp.path().join("file");
        std::fs::write(&blocker, b"not a directory").unwrap();
        let mut lease = AnalysisLease::at_path(blocker.join("analysis.lease"));
        assert!(lease.try_acquire().is_err());
    }

    #[test]
    fn one_broken_slot_does_not_hide_an_available_valid_slot() {
        let temp = tempfile::tempdir().unwrap();
        let blocker = temp.path().join("file");
        std::fs::write(&blocker, b"not a directory").unwrap();
        let valid = temp.path().join("analysis.1.lease");
        let mut lease = AnalysisLease::at_paths([blocker.join("analysis.0.lease"), valid]);
        assert!(lease.try_acquire().unwrap());
        assert_eq!(lease.held_slot(), Some(1));
    }

    #[test]
    fn only_one_audition_owns_one_of_two_analysis_slots() {
        let temp = tempfile::tempdir().unwrap();
        let paths = [
            temp.path().join("analysis.0.lease"),
            temp.path().join("analysis.1.lease"),
        ];
        let process_path = temp.path().join("process.audition.lease");
        let make_admission = || {
            AuditionAdmission::at_paths(paths.clone(), process_path.clone(), temp.path(), "project")
        };
        let mut first = make_admission();
        let mut second = make_admission();
        assert!(first.try_acquire_for("Reference").unwrap());
        assert!(first.held());
        assert!(!second.try_acquire_for("PRE POST Blind").unwrap());
        assert!(!second.held());

        let mut other_analysis = AnalysisLease::at_paths(paths.clone());
        assert!(other_analysis.try_acquire_for("Drum").unwrap());
        first.release();
        assert!(!first.held());
        assert!(second.try_acquire_for("PRE POST Blind").unwrap());
    }

    #[test]
    fn one_process_cannot_audition_two_projects() {
        let temp = tempfile::tempdir().unwrap();
        let paths = [
            temp.path().join("analysis.0"),
            temp.path().join("analysis.1"),
        ];
        let process = temp.path().join("process.audition");
        let mut first =
            AuditionAdmission::at_paths(paths.clone(), process.clone(), temp.path(), "first");
        let mut second = AuditionAdmission::at_paths(paths, process, temp.path(), "second");
        assert!(first.try_acquire_for("Reference").unwrap());
        assert!(!second.try_acquire_for("PRE POST Blind").unwrap());
    }
}
