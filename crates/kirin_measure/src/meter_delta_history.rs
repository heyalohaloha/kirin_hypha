//! Exact DAW-sample join for the always-on POST−PRE TIME history.
//!
//! PRE publishes a bounded tail from its Meter Session on the existing IO thread. POST reads the
//! exact latched PRE path and subtracts only points whose presentation source and sample endpoint
//! are unique on both sides. Missing, repeated, malformed, or cross-runtime facts stay absent.

use std::collections::{HashMap, HashSet, VecDeque};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::{Arc, Mutex};

use serde::{Deserialize, Serialize};

#[path = "meter_pair_observation.rs"]
mod pair_observation;
#[path = "meter_history_publisher.rs"]
mod publisher;
use pair_observation::DeltaHistoryState;

use crate::meter_history::MeterHistory;
use crate::plugin_data::MeasurementLayout;
use crate::{
    CaptureClockSource, MeasureResult, MeterHistoryAux, MeterHistoryEntry, MeterHistoryResolution,
    MeterSession,
};

pub const METER_HISTORY_EXCHANGE_FILE: &str = "meter_history.json";
/// 3 = B-968。`layout` を足し、違う map で測った 2 本を引き算しないようにした。
pub const METER_HISTORY_EXCHANGE_SCHEMA: u8 = 3;
pub const METER_HISTORY_EXCHANGE_POINTS: usize = 32;
const LOCAL_JOIN_POINTS: usize = METER_HISTORY_EXCHANGE_POINTS * 2;
const MAX_EXCHANGE_BYTES: u64 = 64 * 1024;
const JOINED_POINT_CAPACITY: usize = crate::HISTORY_10_HZ_CAPACITY;

#[derive(Clone, Debug, Eq, PartialEq)]
pub struct MeterHistoryTarget {
    pub pre_instance_id: String,
    pub pre_json: PathBuf,
    pub instance_dir: PathBuf,
}

impl MeterHistoryTarget {
    pub fn from_pre_json(pre_instance_id: String, pre_json: &Path) -> Option<Self> {
        (pre_json.file_name()?.to_str()? == "pre.json").then_some(Self {
            pre_instance_id,
            instance_dir: pre_json.parent()?.to_path_buf(),
            pre_json: pre_json.to_path_buf(),
        })
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
    points: Vec<WirePoint>,
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
}

pub struct MeterDeltaHistoryExchange {
    sample_rate: u32,
    layout: MeasurementLayout,
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
            meter_session,
            delta: Mutex::new(DeltaHistoryState::default()),
            publisher: Mutex::new(publisher::HistoryPublisher::default()),
        })
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
        if identity.instance_id != target.pre_instance_id
            || identity.watch_owner_id.is_empty()
            || identity.signal_state != "active"
        {
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
        drop(session);
        let mut delta = lock_recover(&self.delta);
        delta.bind(PairKey {
            instance_id: target.pre_instance_id,
            instance_dir: target.instance_dir,
            owner_id: identity.watch_owner_id,
        });
        delta.ingest(&publication.points, &local, self.sample_rate);
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
    }
}

fn read_pre_identity(path: &Path) -> Result<PreIdentity, String> {
    read_bounded_json(path)
}

fn read_publication(instance_dir: &Path) -> Result<Publication, String> {
    read_bounded_json(&instance_dir.join(METER_HISTORY_EXCHANGE_FILE))
}

fn read_bounded_json<T: for<'de> Deserialize<'de>>(path: &Path) -> Result<T, String> {
    let metadata = fs::metadata(path).map_err(|error| error.to_string())?;
    if metadata.len() > MAX_EXCHANGE_BYTES {
        return Err("meter history exchange exceeds byte limit".to_string());
    }
    let bytes = fs::read(path).map_err(|error| error.to_string())?;
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
