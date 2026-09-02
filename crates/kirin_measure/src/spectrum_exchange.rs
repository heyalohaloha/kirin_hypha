//! Exact-pair, renewable optional-analysis exchange between a visible POST and its latched PRE.
//! Its platform transport remains isolated from existing Watch and Record schemas.

#[cfg(test)]
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime, UNIX_EPOCH};

use uuid::Uuid;

#[path = "attack_exchange_codec.rs"]
mod attack_codec;
#[path = "attack_exchange_join.rs"]
mod attack_joining;
#[path = "spectrum_exchange_codec.rs"]
pub(crate) mod codec;
#[path = "spectrum_exchange_control.rs"]
mod control;
#[path = "spectrum_exchange_join.rs"]
mod joining;
#[path = "perceptual_exchange_codec.rs"]
mod perceptual_codec;
#[path = "spectrum_exchange_post.rs"]
mod post_tick;
#[path = "spectrum_exchange_pre.rs"]
mod pre_tick;
#[path = "spectrum_exchange_view.rs"]
mod view_state;

use crate::absolute_timeline::AbsoluteTimeline;
#[cfg(test)]
use crate::analysis_exchange_protocol::request_path;
#[cfg(test)]
use crate::analysis_exchange_protocol::JSON_MAX_BYTES as REQUEST_MAX_BYTES;
use crate::analysis_exchange_protocol::{
    common_future_epoch, read_ready, read_request, remove_ready, remove_request, validated_request,
    write_ready, write_request, AnalysisReady, AnalysisRequest, REQUEST_SCHEMA,
};
use crate::analysis_lease::AnalysisLease;
use crate::perceptual::PerceptualDifference;
use crate::perceptual_difference_timeline::PerceptualDifferenceTimeline;
use crate::spectrum::{AnalysisViewMode, SpectrumChannelMode, SpectrumDifference, SpectrumFrame};
use crate::spectrum_exchange_worker::SpectrumExchangeWorker;
use crate::spectrum_runtime::{SpectrumHistory, SpectrumRuntime};
use crate::{AttackHistory, AttackPairEvent, AttackRuntime};
use attack_codec::{
    encode_attack_snapshot, read_attack_snapshot, remove_attack_snapshot, write_attack_snapshot,
};
use attack_joining::store_joined_attack;
#[cfg(test)]
use codec::{decode_snapshot, SNAPSHOT_MAX_BYTES};
use codec::{encode_snapshot, read_snapshot, remove_snapshot, write_snapshot};
#[cfg(test)]
use joining::{
    exact_perceptual_differences, newest_exact_difference, newest_exact_perceptual_difference,
};
use joining::{store_joined_perceptual, store_joined_spectrum};
use perceptual_codec::{
    encode_perceptual_snapshot, read_perceptual_snapshot, remove_perceptual_snapshot,
    write_perceptual_snapshot,
};

const REQUEST_RENEW_INTERVAL: Duration = Duration::from_millis(500);
pub(crate) const REQUEST_LEASE_MS: i64 = 1_500;
const WARMUP_LIMIT: Duration = Duration::from_secs(2);
const PRESENTATION_HOLD: Duration = Duration::from_millis(REQUEST_LEASE_MS as u64);

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
    pub spectrum_timeline: crate::SpectrumDifferenceTimeline,
    /// Local POST facts remain available when no PRE is selected or the exact pair is warming.
    /// They never substitute for `difference`, whose presence still proves an exact PRE/POST join.
    pub post_spectrum: Option<SpectrumFrame>,
    pub post_spectrum_history: SpectrumHistory,
    pub perceptual_difference: Option<PerceptualDifference>,
    pub perceptual_timeline: PerceptualDifferenceTimeline,
    pub absolute_timeline: AbsoluteTimeline,
    pub analysis_owner_names: [String; crate::ANALYSIS_SLOT_COUNT],
}

#[derive(Clone, Debug, Default)]
pub struct AttackPairViewSnapshot {
    pub status: SpectrumViewStatus,
    pub pre: Option<AttackHistory>,
    pub post: Option<AttackHistory>,
    pub pair_events: Vec<AttackPairEvent>,
}

#[derive(Clone)]
struct PostSession {
    request_id: Uuid,
    target: Option<SpectrumTarget>,
    last_renewed: Option<Instant>,
    last_renewal_attempt: Option<Instant>,
    started_at: Option<Instant>,
    last_presented_at: Option<Instant>,
    last_presented_end_samples: Option<i64>,
    analysis_mode: AnalysisViewMode,
    channel_mode: SpectrumChannelMode,
    state_epoch_samples: Option<i64>,
}

#[derive(Clone)]
struct PreSession {
    request_id: Uuid,
    last_written_end: Option<i64>,
    last_write_attempt_end: Option<i64>,
    last_write_attempt_at: Option<Instant>,
    last_ready_written_at: Option<Instant>,
    instance_dir: PathBuf,
    state_epoch_samples: Option<i64>,
}

pub struct SpectrumCoordinator {
    sample_rate: u32,
    runtime: Arc<SpectrumRuntime>,
    attack_runtime: Option<Arc<AttackRuntime>>,
    post_visible: AtomicBool,
    post_session: Mutex<Option<PostSession>>,
    pre_session: Mutex<Option<PreSession>>,
    view: Mutex<SpectrumViewSnapshot>,
    attack_view: Mutex<AttackPairViewSnapshot>,
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

    pub fn new_with_attack(
        sample_rate: u32,
        runtime: Arc<SpectrumRuntime>,
        attack_runtime: Option<Arc<AttackRuntime>>,
    ) -> Arc<Self> {
        #[cfg(not(test))]
        {
            Self::new_with_lease_and_attack(
                sample_rate,
                runtime,
                attack_runtime,
                AnalysisLease::for_current_process(),
            )
        }
        #[cfg(test)]
        {
            Self::new_with_lease_and_attack(
                sample_rate,
                runtime,
                attack_runtime,
                AnalysisLease::at_path(
                    std::env::temp_dir()
                        .join("kirin")
                        .join("analysis-tests")
                        .join(format!("{}.lease", Uuid::new_v4())),
                ),
            )
        }
    }

    fn new_with_lease(
        sample_rate: u32,
        runtime: Arc<SpectrumRuntime>,
        analysis_lease: AnalysisLease,
    ) -> Arc<Self> {
        Self::new_with_lease_and_attack(sample_rate, runtime, None, analysis_lease)
    }

    fn new_with_lease_and_attack(
        sample_rate: u32,
        runtime: Arc<SpectrumRuntime>,
        attack_runtime: Option<Arc<AttackRuntime>>,
        analysis_lease: AnalysisLease,
    ) -> Arc<Self> {
        Arc::new(Self {
            sample_rate,
            runtime,
            attack_runtime,
            post_visible: AtomicBool::new(false),
            post_session: Mutex::new(None),
            pre_session: Mutex::new(None),
            view: Mutex::new(SpectrumViewSnapshot::default()),
            attack_view: Mutex::new(AttackPairViewSnapshot::default()),
            analysis_lease: Mutex::new(analysis_lease),
            exchange_worker: SpectrumExchangeWorker::new(),
        })
    }

    #[cfg(test)]
    fn new_for_test(sample_rate: u32, runtime: Arc<SpectrumRuntime>) -> Arc<Self> {
        Self::new_with_lease_and_attack(
            sample_rate,
            runtime,
            None,
            AnalysisLease::at_path(
                std::env::temp_dir()
                    .join("kirin")
                    .join("analysis-tests")
                    .join(format!("{}.lease", Uuid::new_v4())),
            ),
        )
    }

    pub fn post_visible(&self) -> bool {
        self.post_visible.load(Ordering::Acquire)
    }

    pub(crate) fn has_valid_pre_request(&self, pre_instance_id: &str, instance_dir: &Path) -> bool {
        validated_request(
            instance_dir,
            pre_instance_id,
            self.sample_rate,
            unix_ms_now(),
        )
        .is_some()
    }

    fn ensure_analysis_lease(&self, owner_name: &str) -> std::io::Result<bool> {
        match self.analysis_lease.lock() {
            Ok(mut lease) => lease.try_acquire_for(owner_name),
            Err(poisoned) => poisoned.into_inner().try_acquire_for(owner_name),
        }
    }

    fn observed_analysis_owner_names(&self) -> [String; crate::ANALYSIS_SLOT_COUNT] {
        match self.analysis_lease.lock() {
            Ok(lease) => lease.observed_owner_names(),
            Err(poisoned) => poisoned.into_inner().observed_owner_names(),
        }
    }

    fn release_analysis_lease(&self) {
        let mut lease = match self.analysis_lease.lock() {
            Ok(lease) => lease,
            Err(poisoned) => poisoned.into_inner(),
        };
        lease.release();
    }
}

fn snapshot_path(instance_dir: &Path) -> PathBuf {
    instance_dir.join("spectrum").join("pre.bin")
}

fn perceptual_snapshot_path(instance_dir: &Path) -> PathBuf {
    instance_dir.join("spectrum").join("pre_perceptual.bin")
}

fn attack_snapshot_path(instance_dir: &Path) -> PathBuf {
    instance_dir.join("attack").join("pre.bin")
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
    if read_request(&target.instance_dir)
        .is_some_and(|request| request.request_id == request_id.to_string())
    {
        remove_request(&target.instance_dir);
        remove_snapshot(&target.instance_dir);
        remove_perceptual_snapshot(&target.instance_dir);
        remove_attack_snapshot(&target.instance_dir);
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
#[path = "attack_exchange_integration_tests.rs"]
mod attack_integration_tests;
#[cfg(test)]
#[path = "spectrum_exchange_integration_tests.rs"]
mod integration_tests;
#[cfg(test)]
#[path = "spectrum_exchange_lease_tests.rs"]
mod lease_tests;
#[cfg(all(test, not(windows)))]
#[path = "spectrum_exchange_lock_tests.rs"]
mod lock_tests;
#[cfg(test)]
#[path = "spectrum_exchange_recovery_tests.rs"]
mod recovery_tests;
#[cfg(test)]
#[path = "spectrum_exchange_tests.rs"]
mod tests;
