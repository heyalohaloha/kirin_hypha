//! Exact DAW-sample join for the always-on POST−PRE TIME history.
//!
//! PRE publishes a bounded tail from its Meter Session on the existing IO thread. POST reads the
//! exact latched PRE path and subtracts only points whose presentation source and sample endpoint
//! are unique on both sides. Missing, repeated, malformed, or cross-runtime facts stay absent.

use std::collections::{HashMap, HashSet, VecDeque};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::{Arc, Mutex};

use serde::{Deserialize, Serialize};

#[path = "meter_pair_observation.rs"]
mod pair_observation;
#[path = "meter_history_publisher.rs"]
mod publisher;
use pair_observation::DeltaHistoryState;
#[path = "chain_observation.rs"]
pub mod chain;
#[path = "meter_chain_join.rs"]
mod chain_join;
#[path = "meter_content_wire.rs"]
mod content_wire;
use content_wire::ContentWirePoint;

use crate::meter_history::MeterHistory;
use crate::plugin_data::MeasurementLayout;
use crate::{
    CaptureClockSource, MeasureResult, MeterHistoryAux, MeterHistoryEntry, MeterHistoryResolution,
    MeterSession,
};

pub const METER_HISTORY_EXCHANGE_FILE: &str = "meter_history.json";
/// One same-version PRE/POST envelope for TIME and qualified content observations. An older
/// peer is rejected as a unit; no cross-version comparison or partial TIME claim is inferred.
pub const METER_HISTORY_EXCHANGE_SCHEMA: u8 = 7;
pub const METER_HISTORY_EXCHANGE_POINTS: usize = 32;
const LOCAL_JOIN_POINTS: usize = METER_HISTORY_EXCHANGE_POINTS * 2;
const MAX_EXCHANGE_BYTES: u64 = 64 * 1024;
const JOINED_POINT_CAPACITY: usize = crate::HISTORY_10_HZ_CAPACITY;

/// The host/API evidence behind the optional exact comparison clock. A wrapper source alone
/// does not identify its host's PDC semantics. Unknown remains the default for every caller.
pub const CLOCK_POLICY_UNKNOWN: u8 = 0;
pub const CLOCK_POLICY_STUDIO_PRO_812_WINDOWS_VST3: u8 = 1;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MeterHistoryTarget {
    pub pre_instance_id: String,
    pub pre_json: PathBuf,
    pub instance_dir: PathBuf,
    post_binding: Option<PostBindingProvenance>,
}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
struct PostBindingProvenance {
    pair_owner_id: String,
    post_instance_id: String,
    generation: u64,
    claimed_at_bits: u64,
}

impl MeterHistoryTarget {
    pub fn from_pre_json(pre_instance_id: String, pre_json: &Path) -> Option<Self> {
        (pre_json.file_name()?.to_str()? == "pre.json").then_some(Self {
            pre_instance_id,
            instance_dir: pre_json.parent()?.to_path_buf(),
            pre_json: pre_json.to_path_buf(),
            post_binding: None,
        })
    }

    /// Bind a confirmed PRE locator to the exact POST engine selection. A repeated selection
    /// of the same PRE path is a new comparison, not a continuation of the previous history.
    pub fn with_post_binding(
        mut self,
        pair_owner_id: &str,
        post_instance_id: &str,
        generation: u64,
        claimed_at: f64,
    ) -> Option<Self> {
        if !crate::path_identity::is_path_safe_component(pair_owner_id)
            || !crate::path_identity::is_path_safe_component(post_instance_id)
            || generation == 0
            || !claimed_at.is_finite()
            || claimed_at <= 0.0
        {
            return None;
        }
        self.post_binding = Some(PostBindingProvenance {
            pair_owner_id: pair_owner_id.into(),
            post_instance_id: post_instance_id.into(),
            generation,
            claimed_at_bits: claimed_at.to_bits(),
        });
        Some(self)
    }
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq)]
struct WirePoint {
    generation: u64,
    run_id: u64,
    observed_frames: u64,
    endpoint_samples: i64,
    source: u8,
    lufs_m: Option<f64>,
    lufs_s: Option<f64>,
    true_peak: Option<f64>,
    correlation: Option<f64>,
    plr: Option<f64>,
}

#[derive(Clone, Debug, Deserialize, Serialize, PartialEq)]
struct Publication {
    schema: u8,
    pre_instance_id: String,
    watch_owner_id: String,
    daw_session_id: String,
    sample_rate: u32,
    /// PRE が実際に測っている配置。**引き算が成立するのは同じ map で測った 2 本だけである。**
    /// mono の PRE と stereo の POST は、同じ音を通しても loudness で 3.01 LU ずれる
    /// （mono は 1ch として測り +3.01 dB バイアスを入れない）。その差は連鎖が加えたものではない。
    layout: MeasurementLayout,
    clock_policy: u8,
    points: Vec<WirePoint>,
    content_windows: Vec<ContentWirePoint>,
}

#[derive(Deserialize)]
struct PreIdentity {
    instance_id: String,
    #[serde(default)]
    daw_session_id: String,
    #[serde(default)]
    watch_owner_id: String,
    #[serde(default)]
    signal_state: String,
}

#[derive(Clone, Debug, Eq, Hash, PartialEq)]
struct PairKey {
    instance_id: String,
    instance_dir: PathBuf,
    owner_id: String,
    daw_session_id: String,
    post_binding: Option<PostBindingProvenance>,
}

pub struct MeterDeltaHistoryExchange {
    sample_rate: u32,
    layout: MeasurementLayout,
    clock_policy: AtomicU8,
    meter_session: Arc<Mutex<MeterSession>>,
    delta: Mutex<DeltaHistoryState>,
    publisher: Mutex<publisher::HistoryPublisher>,
}

impl MeterDeltaHistoryExchange {
    pub fn new(sample_rate: u32, meter_session: Arc<Mutex<MeterSession>>) -> Arc<Self> {
        // layout は session から読む。引数で二重に渡すと、渡し間違いが「違う map なのに一致」を
        // 作れてしまう。ここで 1 度だけ lock する（生成直後で競合しない）。
        let layout = MeasurementLayout::new(lock_recover(&meter_session).layout());
        Arc::new(Self {
            sample_rate,
            layout,
            clock_policy: AtomicU8::new(CLOCK_POLICY_UNKNOWN),
            meter_session,
            delta: Mutex::new(DeltaHistoryState::default()),
            publisher: Mutex::new(publisher::HistoryPublisher::default()),
        })
    }

    /// Non-RT setup only, before either endpoint starts publishing. An unknown value is never
    /// promoted to an observed host policy. Caller must bind this to the exact executable build
    /// and active plugin wrapper, not to the presence of a raw optional latency value.
    pub fn set_clock_policy(&self, policy: u8) {
        self.clock_policy.store(
            if policy == CLOCK_POLICY_STUDIO_PRO_812_WINDOWS_VST3 {
                policy
            } else {
                CLOCK_POLICY_UNKNOWN
            },
            Ordering::Release,
        );
    }

    fn clock_policy(&self) -> u8 {
        self.clock_policy.load(Ordering::Acquire)
    }

    pub fn service_pre_endpoint(
        &self,
        pre_instance_id: &str,
        daw_session_id: &str,
        watch_owner_id: &str,
        instance_dir: &Path,
    ) -> Result<(), String> {
        self.publisher
            .try_lock()
            .map_err(|_| "history publisher busy".to_string())?
            .publish(
                self,
                pre_instance_id,
                daw_session_id,
                watch_owner_id,
                instance_dir,
            )
    }

    pub fn service_post_endpoint(&self, target: Option<MeterHistoryTarget>) {
        let Some(target) = target else {
            lock_recover(&self.delta).clear_pair();
            return;
        };
        lock_recover(&self.delta).clear_if_different_target(&target);
        let Ok(identity) = read_pre_identity(&target.pre_json) else {
            return;
        };
        if identity.instance_id != target.pre_instance_id || identity.watch_owner_id.is_empty() {
            return;
        }
        // An owner replacement invalidates history before the replacement publishes a file.
        lock_recover(&self.delta).bind(PairKey {
            instance_id: target.pre_instance_id.clone(),
            instance_dir: target.instance_dir.clone(),
            owner_id: identity.watch_owner_id.clone(),
            daw_session_id: identity.daw_session_id.clone(),
            post_binding: target.post_binding.clone(),
        });
        if identity.signal_state != "active" {
            return;
        }
        let Ok(publication) = read_publication(&target.instance_dir) else {
            return;
        };
        if !publication.valid_for(&identity, self.sample_rate, &self.layout) {
            return;
        }
        let Ok(session) = self.meter_session.try_lock() else {
            return;
        };
        let local = session.recent_history(MeterHistoryResolution::Hz10, LOCAL_JOIN_POINTS);
        let snapshot = session.snapshot();
        let incarnation = session.history_publication_revision().0;
        let local_content: Vec<_> = session
            .recent_content_windows(LOCAL_JOIN_POINTS)
            .into_iter()
            .map(|point| {
                ContentWirePoint::from_observation(
                    point,
                    snapshot.measurement_epoch,
                    incarnation,
                    snapshot.generation,
                )
            })
            .collect();
        drop(session);
        let mut delta = lock_recover(&self.delta);
        delta.bind(PairKey {
            instance_id: target.pre_instance_id,
            instance_dir: target.instance_dir,
            owner_id: identity.watch_owner_id,
            daw_session_id: identity.daw_session_id,
            post_binding: target.post_binding,
        });
        delta.chain.begin(
            &publication,
            &local_content,
            &snapshot,
            incarnation,
            self.clock_policy(),
        );
        delta
            .chain
            .ingest(&publication.content_windows, &local_content);
        delta.ingest(&publication.points, &local, self.sample_rate);
        delta.chain.finish();
    }

    pub fn recent(
        &self,
        resolution: MeterHistoryResolution,
        max_entries: usize,
    ) -> Vec<MeterHistoryEntry> {
        lock_recover(&self.delta)
            .history
            .recent(resolution, max_entries)
    }

    pub fn recent_decimated(
        &self,
        resolution: MeterHistoryResolution,
        max_entries: usize,
        max_output: usize,
    ) -> Vec<MeterHistoryEntry> {
        lock_recover(&self.delta)
            .history
            .recent_decimated(resolution, max_entries, max_output)
    }

    pub fn reset(&self) {
        lock_recover(&self.delta).reset();
    }

    /// Explicit comparison audition ends a chain observation binding, not a Meter Session.
    pub fn clear_chain(&self) {
        let mut delta = lock_recover(&self.delta);
        let binding = delta.chain.history.binding.wrapping_add(1).max(1);
        delta.chain.clear(binding);
    }

    /// UI-only, nonblocking; unchanged revision does not clone the bounded batch.
    pub fn chain_snapshot(
        &self,
        known_revision: u64,
        snapshot: &crate::MeterSessionSnapshot,
    ) -> Option<chain::Snapshot> {
        self.chain_snapshot_limit(known_revision, snapshot, chain::CAPACITY)
    }

    /// UI-only. Small editors request the newest point, not a 600-point history clone.
    pub fn chain_snapshot_limit(
        &self,
        known_revision: u64,
        snapshot: &crate::MeterSessionSnapshot,
        limit: usize,
    ) -> Option<chain::Snapshot> {
        let mut delta = self.delta.try_lock().ok()?;
        delta.chain.progress(snapshot);
        delta.chain.history.snapshot_limit(known_revision, limit)
    }
}

impl WirePoint {
    fn from_history(entry: MeterHistoryEntry) -> Option<Self> {
        Some(Self {
            generation: entry.generation,
            run_id: entry.run_id,
            observed_frames: entry.last_observed_frames,
            endpoint_samples: entry.last_timeline_endpoint_samples?,
            source: exact_source(entry.timeline_source)?,
            lufs_m: finite(entry.lufs_m.mean),
            lufs_s: finite(entry.lufs_s.mean),
            true_peak: finite(entry.true_peak.mean),
            correlation: finite(entry.correlation.mean),
            plr: finite(entry.plr.mean),
        })
    }

    fn valid(&self) -> bool {
        matches!(self.source, 1 | 2)
            && [
                self.lufs_m,
                self.lufs_s,
                self.true_peak,
                self.correlation,
                self.plr,
            ]
            .into_iter()
            .flatten()
            .all(f64::is_finite)
    }
}

impl Publication {
    fn valid_for(
        &self,
        identity: &PreIdentity,
        sample_rate: u32,
        layout: &MeasurementLayout,
    ) -> bool {
        self.schema == METER_HISTORY_EXCHANGE_SCHEMA
            && self.sample_rate == sample_rate
            && self.layout == *layout
            && self.pre_instance_id == identity.instance_id
            && self.watch_owner_id == identity.watch_owner_id
            && self.daw_session_id == identity.daw_session_id
            && self.points.len() <= METER_HISTORY_EXCHANGE_POINTS
            && self.points.iter().all(WirePoint::valid)
            && self.content_windows.len() <= METER_HISTORY_EXCHANGE_POINTS
            && self
                .content_windows
                .iter()
                .all(|point| point.valid(sample_rate))
    }
}

fn read_pre_identity(path: &Path) -> Result<PreIdentity, String> {
    read_bounded_json(path)
}

fn read_publication(instance_dir: &Path) -> Result<Publication, String> {
    read_bounded_json(&instance_dir.join(METER_HISTORY_EXCHANGE_FILE))
}

fn read_bounded_json<T: for<'de> Deserialize<'de>>(path: &Path) -> Result<T, String> {
    use std::io::Read;
    let metadata = fs::metadata(path).map_err(|error| error.to_string())?;
    if metadata.len() > MAX_EXCHANGE_BYTES {
        return Err("meter history exchange exceeds byte limit".to_string());
    }
    let mut bytes = Vec::new();
    fs::File::open(path)
        .map_err(|error| error.to_string())?
        .take(MAX_EXCHANGE_BYTES + 1)
        .read_to_end(&mut bytes)
        .map_err(|error| error.to_string())?;
    if bytes.len() as u64 > MAX_EXCHANGE_BYTES {
        return Err("meter history exchange exceeds byte limit".to_string());
    }
    serde_json::from_slice(&bytes).map_err(|error| error.to_string())
}

fn wire_key(point: &WirePoint) -> (u8, i64) {
    (point.source, point.endpoint_samples)
}

fn history_key(point: &MeterHistoryEntry) -> Option<(u8, i64)> {
    Some((
        exact_source(point.timeline_source)?,
        point.last_timeline_endpoint_samples?,
    ))
}

fn key_counts(keys: impl Iterator<Item = (u8, i64)>) -> HashMap<(u8, i64), usize> {
    let mut counts = HashMap::new();
    for key in keys {
        *counts.entry(key).or_insert(0) += 1;
    }
    counts
}

fn exact_source(source: CaptureClockSource) -> Option<u8> {
    (source != CaptureClockSource::Unknown).then_some(source as u8)
}

fn finite(value: Option<f64>) -> Option<f64> {
    value.filter(|value| value.is_finite())
}

fn difference(post: Option<f64>, pre: Option<f64>) -> Option<f64> {
    post.zip(pre)
        .map(|(post, pre)| post - pre)
        .filter(|value| value.is_finite())
}

fn lock_recover<T>(mutex: &Mutex<T>) -> std::sync::MutexGuard<'_, T> {
    mutex
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
}

#[cfg(test)]
#[path = "meter_delta_history_tests.rs"]
mod tests;

#[cfg(test)]
#[path = "meter_chain_exchange_tests.rs"]
mod chain_exchange_tests;
