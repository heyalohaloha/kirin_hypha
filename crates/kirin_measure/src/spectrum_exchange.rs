//! Exact-pair, renewable optional-analysis exchange between a visible POST and its latched PRE.
//! Its atomic control and payload files remain isolated from existing Watch and Record schemas.

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use uuid::Uuid;

#[path = "spectrum_exchange_codec.rs"]
pub(crate) mod codec;
#[path = "spectrum_exchange_join.rs"]
mod joining;
#[path = "perceptual_exchange_codec.rs"]
mod perceptual_codec;
#[path = "spectrum_exchange_pre.rs"]
mod pre_tick;
#[path = "spectrum_exchange_view.rs"]
mod view_state;

#[cfg(test)]
use crate::analysis_exchange_protocol::JSON_MAX_BYTES as REQUEST_MAX_BYTES;
use crate::analysis_exchange_protocol::{
    common_future_epoch, read_ready, read_request, remove_ready, request_path, validated_request,
    write_ready, write_request, AnalysisReady, AnalysisRequest, REQUEST_SCHEMA,
};
use crate::analysis_lease::AnalysisLease;
use crate::perceptual::PerceptualDifference;
use crate::perceptual_difference_timeline::PerceptualDifferenceTimeline;
use crate::spectrum::{AnalysisViewMode, SpectrumChannelMode, SpectrumDifference};
use crate::spectrum_exchange_worker::SpectrumExchangeWorker;
#[cfg(test)]
use crate::spectrum_runtime::SpectrumHistory;
use crate::spectrum_runtime::SpectrumRuntime;
#[cfg(test)]
use codec::{decode_snapshot, SNAPSHOT_MAX_BYTES};
use codec::{encode_snapshot, read_snapshot};
#[cfg(test)]
use joining::{
    exact_perceptual_differences, newest_exact_difference, newest_exact_perceptual_difference,
};
use joining::{store_joined_perceptual, store_joined_spectrum};
use perceptual_codec::{encode_perceptual_snapshot, read_perceptual_snapshot};

const REQUEST_RENEW_INTERVAL: Duration = Duration::from_millis(500);
const REQUEST_LEASE_MS: i64 = 1_500;
const WARMUP_LIMIT: Duration = Duration::from_secs(2);

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct SpectrumTarget {
    pub pre_instance_id: String,
    pub instance_dir: PathBuf,
}

impl SpectrumTarget {
    pub fn from_pre_json(pre_instance_id: String, pre_json: &Path) -> Option<Self> {
        (pre_json.file_name()?.to_str()? == "pre.json").then_some(Self {
            pre_instance_id,
            instance_dir: pre_json.parent()?.to_path_buf(),
        })
    }
}

#[derive(Clone, Copy, Debug, Default, Eq, PartialEq)]
#[repr(u8)]
pub enum SpectrumViewStatus {
    #[default]
    Hidden = 0,
    NoPair = 1,
    WarmingUp = 2,
    Active = 3,
    Unavailable = 4,
    InUse = 5,
}

#[derive(Clone, Debug, Default, PartialEq)]
pub struct SpectrumViewSnapshot {
    pub status: SpectrumViewStatus,
    pub analysis_mode: AnalysisViewMode,
    pub channel_mode: SpectrumChannelMode,
    pub channels: u8,
    pub difference: Option<SpectrumDifference>,
    pub perceptual_difference: Option<PerceptualDifference>,
    pub perceptual_timeline: PerceptualDifferenceTimeline,
}

struct PostSession {
    request_id: Uuid,
    target: Option<SpectrumTarget>,
    last_renewed: Option<Instant>,
    started_at: Option<Instant>,
    analysis_mode: AnalysisViewMode,
    channel_mode: SpectrumChannelMode,
    state_epoch_samples: Option<i64>,
}

struct PreSession {
    request_id: Uuid,
    last_written_end: Option<i64>,
    instance_dir: PathBuf,
    state_epoch_samples: Option<i64>,
}

pub struct SpectrumCoordinator {
    sample_rate: u32,
    runtime: Arc<SpectrumRuntime>,
    post_visible: AtomicBool,
    post_session: Mutex<Option<PostSession>>,
    pre_session: Mutex<Option<PreSession>>,
    view: Mutex<SpectrumViewSnapshot>,
    analysis_lease: Mutex<AnalysisLease>,
    pub(crate) exchange_worker: SpectrumExchangeWorker,
}

impl SpectrumCoordinator {
    pub fn new(sample_rate: u32, runtime: Arc<SpectrumRuntime>) -> Arc<Self> {
        #[cfg(not(test))]
        {
            Self::new_with_lease(sample_rate, runtime, AnalysisLease::for_current_process())
        }
        #[cfg(test)]
        {
            Self::new_for_test(sample_rate, runtime)
        }
    }

    fn new_with_lease(
        sample_rate: u32,
        runtime: Arc<SpectrumRuntime>,
        analysis_lease: AnalysisLease,
    ) -> Arc<Self> {
        Arc::new(Self {
            sample_rate,
            runtime,
            post_visible: AtomicBool::new(false),
            post_session: Mutex::new(None),
            pre_session: Mutex::new(None),
            view: Mutex::new(SpectrumViewSnapshot::default()),
            analysis_lease: Mutex::new(analysis_lease),
            exchange_worker: SpectrumExchangeWorker::new(),
        })
    }

    #[cfg(test)]
    fn new_for_test(sample_rate: u32, runtime: Arc<SpectrumRuntime>) -> Arc<Self> {
        Self::new_with_lease(
            sample_rate,
            runtime,
            AnalysisLease::at_path(
                std::env::temp_dir()
                    .join("kirin")
                    .join("analysis-tests")
                    .join(format!("{}.lease", Uuid::new_v4())),
            ),
        )
    }

    /// Message/control thread only. Filesystem work remains deferred to the existing IO thread.
    pub fn set_post_visible(&self, visible: bool) {
        let previous = self.post_visible.swap(visible, Ordering::AcqRel);
        if visible && !previous {
            if let Ok(mut session) = self.post_session.lock() {
                *session = Some(PostSession {
                    request_id: Uuid::new_v4(),
                    target: None,
                    last_renewed: None,
                    started_at: None,
                    analysis_mode: self.runtime.analysis_mode(),
                    channel_mode: self.runtime.channel_mode(),
                    state_epoch_samples: None,
                });
            }
        } else if !visible {
            // Serialize the close edge with an in-flight exchange tick before releasing the
            // process-wide lease. A new POST can never overlap the previous owner's worker.
            let _tick_guard = self.post_session.lock().ok();
            let _ = self.runtime.set_enabled(false);
            self.release_analysis_lease();
            self.store_view(SpectrumViewStatus::Hidden, None, None);
        }
        self.exchange_worker.notify();
    }

    pub fn post_visible(&self) -> bool {
        self.post_visible.load(Ordering::Acquire)
    }

    /// UI/control thread only. Spectrum and Perceptual Delta never analyze in parallel.
    pub fn set_post_analysis_mode(&self, mode: AnalysisViewMode) -> bool {
        let _session = match self.post_session.lock() {
            Ok(session) => session,
            Err(_) => return false,
        };
        if !self.runtime.set_analysis_mode(mode) {
            return false;
        }
        let status = if self.post_visible() {
            SpectrumViewStatus::WarmingUp
        } else {
            SpectrumViewStatus::Hidden
        };
        self.store_view(status, None, None);
        self.exchange_worker.notify();
        true
    }

    /// UI/control thread only. The next isolated exchange tick renews the exact request; this
    /// edge itself performs no filesystem access.
    pub fn set_post_channel_mode(&self, mode: SpectrumChannelMode) -> bool {
        // Serialize the mode edge with POST exchange publication. Without this guard, one tick
        // could clone an old exact pair, then publish it after the UI had already selected a new
        // channel definition and cleared the visible frame.
        let _session = match self.post_session.lock() {
            Ok(session) => session,
            Err(_) => return false,
        };
        if !self.runtime.set_channel_mode(mode) {
            return false;
        }
        let status = if self.post_visible() {
            SpectrumViewStatus::WarmingUp
        } else {
            SpectrumViewStatus::Hidden
        };
        self.store_view(status, None, None);
        self.exchange_worker.notify();
        true
    }

    /// POST IO-thread tick. `target` must come from the already-confirmed exact pair latch.
    pub(crate) fn post_tick(&self, post_instance_id: &str, target: Option<SpectrumTarget>) -> bool {
        let mut session_slot = match self.post_session.lock() {
            Ok(session) => session,
            Err(_) => return false,
        };
        if !self.post_visible() {
            if let Some(session) = session_slot.take() {
                cleanup_owned_request(session.target.as_ref(), session.request_id);
            }
            return false;
        }
        match self.ensure_analysis_lease() {
            Ok(true) => {}
            Ok(false) => {
                let _ = self.runtime.set_enabled(false);
                self.store_view(SpectrumViewStatus::InUse, None, None);
                return false;
            }
            Err(_) => {
                let _ = self.runtime.set_enabled(false);
                self.store_view(SpectrumViewStatus::Unavailable, None, None);
                return false;
            }
        }
        let session = session_slot.get_or_insert_with(|| PostSession {
            request_id: Uuid::new_v4(),
            target: None,
            last_renewed: None,
            started_at: None,
            analysis_mode: self.runtime.analysis_mode(),
            channel_mode: self.runtime.channel_mode(),
            state_epoch_samples: None,
        });
        let Some(target) = target else {
            cleanup_owned_request(session.target.as_ref(), session.request_id);
            session.target = None;
            session.last_renewed = None;
            session.started_at = None;
            let _ = self.runtime.set_enabled(false);
            self.store_view(SpectrumViewStatus::NoPair, None, None);
            return false;
        };
        let analysis_mode = self.runtime.analysis_mode();
        let channel_mode = self.runtime.channel_mode();
        let definition_changed = session.target.as_ref() != Some(&target)
            || session.analysis_mode != analysis_mode
            || session.channel_mode != channel_mode;
        let rearm_required = analysis_mode == AnalysisViewMode::Perceptual
            && self.runtime.take_perceptual_rearm_required();
        if definition_changed || rearm_required {
            cleanup_owned_request(session.target.as_ref(), session.request_id);
            session.request_id = Uuid::new_v4();
            session.target = Some(target.clone());
            session.last_renewed = None;
            session.started_at = None;
            session.analysis_mode = analysis_mode;
            session.channel_mode = channel_mode;
            session.state_epoch_samples = None;
            let _ = self.runtime.set_enabled(false);
            if analysis_mode == AnalysisViewMode::Perceptual {
                let _ = self.runtime.set_perceptual_state_epoch(None);
            }
        }
        let now = Instant::now();
        if session
            .last_renewed
            .is_none_or(|last| now.duration_since(last) >= REQUEST_RENEW_INTERVAL)
        {
            if renew_request(
                session,
                post_instance_id,
                &target,
                self.sample_rate,
                unix_ms_now(),
            )
            .is_err()
            {
                let _ = self.runtime.set_enabled(false);
                self.store_view(SpectrumViewStatus::Unavailable, None, None);
                return false;
            }
            session.last_renewed = Some(now);
        }
        if !self.runtime.set_enabled(true) {
            cleanup_owned_request(Some(&target), session.request_id);
            session.last_renewed = None;
            self.store_view(SpectrumViewStatus::Unavailable, None, None);
            return false;
        }
        if analysis_mode == AnalysisViewMode::Perceptual && session.state_epoch_samples.is_none() {
            self.store_view(SpectrumViewStatus::WarmingUp, None, None);
            let ready = read_ready(&target.instance_dir).filter(|ready| {
                ready.matches(
                    session.request_id,
                    &target.pre_instance_id,
                    self.sample_rate,
                    unix_ms_now(),
                )
            });
            let Some(ready) = ready else {
                return true;
            };
            if ready.rearm_required() {
                cleanup_owned_request(Some(&target), session.request_id);
                session.request_id = Uuid::new_v4();
                session.last_renewed = None;
                let _ = self.runtime.set_perceptual_state_epoch(None);
                return true;
            }
            let Some(local_end) = self.runtime.latest_presentation_end() else {
                return true;
            };
            let aperture = i64::from(self.sample_rate / crate::PERCEPTUAL_PRESENTATION_HZ);
            let Some(epoch) = common_future_epoch(local_end, ready.observed_end(), aperture) else {
                let _ = self.runtime.set_enabled(false);
                self.store_view(SpectrumViewStatus::Unavailable, None, None);
                return false;
            };
            if !self.runtime.set_perceptual_state_epoch(Some(epoch)) {
                let _ = self.runtime.set_enabled(false);
                self.store_view(SpectrumViewStatus::Unavailable, None, None);
                return false;
            }
            session.state_epoch_samples = Some(epoch);
            session.last_renewed = None;
            session.started_at = Some(now);
            if renew_request(
                session,
                post_instance_id,
                &target,
                self.sample_rate,
                unix_ms_now(),
            )
            .is_err()
            {
                let _ = self.runtime.set_enabled(false);
                self.store_view(SpectrumViewStatus::Unavailable, None, None);
                return false;
            }
            session.last_renewed = Some(now);
            return true;
        }
        if session.started_at.is_none() {
            session.started_at = Some(now);
        }
        match analysis_mode {
            AnalysisViewMode::Spectrum => {
                let local = self.runtime.try_history();
                let remote = read_snapshot(&target.instance_dir)
                    .filter(|snapshot| snapshot.request_id == session.request_id)
                    .map(|snapshot| snapshot.history);
                store_joined_spectrum(self, session, now, local.as_ref(), remote.as_ref());
            }
            AnalysisViewMode::Perceptual => {
                let local = self.runtime.try_perceptual_history();
                let remote = read_perceptual_snapshot(&target.instance_dir)
                    .filter(|snapshot| snapshot.request_id == session.request_id)
                    .map(|snapshot| snapshot.history);
                store_joined_perceptual(self, session, now, local.as_ref(), remote.as_ref());
            }
        }
        true
    }

    pub fn shutdown(&self) {
        self.post_visible.store(false, Ordering::Release);
        self.exchange_worker.shutdown_and_join();
        if let Ok(mut session) = self.post_session.lock() {
            if let Some(session) = session.take() {
                cleanup_owned_request(session.target.as_ref(), session.request_id);
            }
        }
        if let Ok(mut session) = self.pre_session.lock() {
            if let Some(session) = session.take() {
                let _ = fs::remove_file(snapshot_path(&session.instance_dir));
                let _ = fs::remove_file(perceptual_snapshot_path(&session.instance_dir));
                remove_ready(&session.instance_dir);
            }
        }
        let _ = self.runtime.set_enabled(false);
        self.release_analysis_lease();
    }

    fn ensure_analysis_lease(&self) -> std::io::Result<bool> {
        self.analysis_lease
            .lock()
            .map_err(|_| std::io::Error::other("analysis lease lock poisoned"))?
            .try_acquire()
    }

    fn release_analysis_lease(&self) {
        if let Ok(mut lease) = self.analysis_lease.lock() {
            lease.release();
        }
    }
}

fn snapshot_path(instance_dir: &Path) -> PathBuf {
    instance_dir.join("spectrum").join("pre.bin")
}

fn perceptual_snapshot_path(instance_dir: &Path) -> PathBuf {
    instance_dir.join("spectrum").join("pre_perceptual.bin")
}

fn renew_request(
    session: &PostSession,
    post_instance_id: &str,
    target: &SpectrumTarget,
    sample_rate: u32,
    now_unix_ms: i64,
) -> std::io::Result<()> {
    write_request(
        &target.instance_dir,
        &AnalysisRequest {
            schema: REQUEST_SCHEMA.to_string(),
            request_id: session.request_id.to_string(),
            requested_by_post_instance_id: post_instance_id.to_string(),
            target_pre_instance_id: target.pre_instance_id.clone(),
            sample_rate,
            analysis_mode: session.analysis_mode as u8,
            channel_mode: session.channel_mode as u8,
            state_epoch_samples: session.state_epoch_samples,
            expires_at_unix_ms: now_unix_ms.saturating_add(REQUEST_LEASE_MS),
        },
    )
}

fn cleanup_owned_request(target: Option<&SpectrumTarget>, request_id: Uuid) {
    let Some(target) = target else { return };
    let path = request_path(&target.instance_dir);
    if read_request(&target.instance_dir)
        .is_some_and(|request| request.request_id == request_id.to_string())
    {
        let _ = fs::remove_file(path);
        let _ = fs::remove_file(snapshot_path(&target.instance_dir));
        let _ = fs::remove_file(perceptual_snapshot_path(&target.instance_dir));
        remove_ready(&target.instance_dir);
    }
}

fn unix_ms_now() -> i64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .ok()
        .and_then(|duration| i64::try_from(duration.as_millis()).ok())
        .unwrap_or(0)
}

#[cfg(test)]
#[path = "spectrum_exchange_tests.rs"]
mod tests;
