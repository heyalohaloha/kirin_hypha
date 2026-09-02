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

use crate::meter_history::MeterHistory;
use crate::{
    CaptureClockSource, MeasureResult, MeterHistoryAux, MeterHistoryEntry, MeterHistoryResolution,
    MeterSession,
};

pub const METER_HISTORY_EXCHANGE_FILE: &str = "meter_history.json";
pub const METER_HISTORY_EXCHANGE_SCHEMA: u8 = 2;
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

#[derive(Clone, Debug, Deserialize, Serialize)]
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

#[derive(Clone, Debug, Deserialize, Serialize)]
struct Publication {
    schema: u8,
    pre_instance_id: String,
    watch_owner_id: String,
    daw_session_id: String,
    sample_rate: u32,
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

#[derive(Clone, Copy, Debug, Eq, Hash, PartialEq)]
struct JoinedPoint {
    generation: u64,
    observed_frames: u64,
}

#[derive(Default)]
struct DeltaHistoryState {
    generation: u64,
    next_run_id: u64,
    current_source_run: Option<(u64, u64, u64, u64)>,
    last_joined_axis: Option<(u64, i64, u8)>,
    pair: Option<PairKey>,
    history: MeterHistory,
    joined_order: VecDeque<JoinedPoint>,
    joined: HashSet<JoinedPoint>,
    consumed_pre_order: VecDeque<JoinedPoint>,
    consumed_pre: HashSet<JoinedPoint>,
}

impl DeltaHistoryState {
    fn bind(&mut self, pair: PairKey) {
        if self.pair.as_ref() == Some(&pair) {
            return;
        }
        self.pair = Some(pair);
        self.discard();
    }

    fn clear_pair(&mut self) {
        if self.pair.take().is_some() {
            self.discard();
        }
    }

    fn clear_if_different_target(&mut self, target: &MeterHistoryTarget) {
        if self.pair.as_ref().is_some_and(|pair| {
            pair.instance_id != target.pre_instance_id || pair.instance_dir != target.instance_dir
        }) {
            self.clear_pair();
        }
    }

    fn reset(&mut self) {
        self.discard();
    }

    fn discard(&mut self) {
        self.generation = self.generation.wrapping_add(1).max(1);
        self.next_run_id = 0;
        self.current_source_run = None;
        self.last_joined_axis = None;
        self.history.reset();
        self.joined_order.clear();
        self.joined.clear();
        self.consumed_pre_order.clear();
        self.consumed_pre.clear();
    }

    fn ingest(&mut self, pre: &[WirePoint], post: &[MeterHistoryEntry], sample_rate: u32) {
        let available_pre = pre.iter().filter(|point| {
            !self.consumed_pre.contains(&JoinedPoint {
                generation: point.generation,
                observed_frames: point.observed_frames,
            })
        });
        let pre_counts = key_counts(available_pre.clone().map(wire_key));
        let post_counts = key_counts(
            post.iter()
                .filter(|point| {
                    !self.joined.contains(&JoinedPoint {
                        generation: point.generation,
                        observed_frames: point.last_observed_frames,
                    })
                })
                .filter_map(history_key),
        );
        let pre_by_key: HashMap<_, _> = available_pre
            .map(|point| (wire_key(point), point))
            .collect();

        for post_point in post {
            let joined_id = JoinedPoint {
                generation: post_point.generation,
                observed_frames: post_point.last_observed_frames,
            };
            if self.joined.contains(&joined_id) {
                continue;
            }
            let Some(key) = history_key(post_point) else {
                continue;
            };
            if pre_counts.get(&key) != Some(&1) || post_counts.get(&key) != Some(&1) {
                continue;
            }
            let Some(pre_point) = pre_by_key.get(&key) else {
                continue;
            };
            if self
                .last_joined_axis
                .is_some_and(|(observed, _, _)| post_point.last_observed_frames <= observed)
            {
                continue;
            }
            let source_run = (
                pre_point.generation,
                pre_point.run_id,
                post_point.generation,
                post_point.run_id,
            );
            let expected_step = u64::from(sample_rate / 10).max(1);
            let continuous = self
                .last_joined_axis
                .is_some_and(|(observed, endpoint, source)| {
                    post_point.last_observed_frames.saturating_sub(observed) == expected_step
                        && post_point
                            .last_timeline_endpoint_samples
                            .is_some_and(|current| {
                                current.saturating_sub(endpoint) == expected_step as i64
                            })
                        && source == key.0
                });
            if self.current_source_run != Some(source_run) || !continuous {
                self.current_source_run = Some(source_run);
                self.next_run_id = self.next_run_id.wrapping_add(1).max(1);
            }
            let delta = MeasureResult {
                lufs_m: difference(post_point.lufs_m.mean, pre_point.lufs_m),
                lufs_s: difference(post_point.lufs_s.mean, pre_point.lufs_s),
                true_peak: difference(post_point.true_peak.mean, pre_point.true_peak),
                ..MeasureResult::default()
            };
            self.history.push(
                self.generation.max(1),
                self.next_run_id,
                post_point.last_observed_frames,
                (
                    post_point.last_timeline_endpoint_samples,
                    post_point.timeline_source,
                ),
                &delta,
                MeterHistoryAux {
                    correlation: difference(post_point.correlation.mean, pre_point.correlation),
                    plr: difference(post_point.plr.mean, pre_point.plr),
                    clip_event_count: [0; 2],
                },
            );
            self.last_joined_axis = Some((post_point.last_observed_frames, key.1, key.0));
            self.remember_joined(joined_id);
            self.remember_consumed_pre(JoinedPoint {
                generation: pre_point.generation,
                observed_frames: pre_point.observed_frames,
            });
        }
    }

    fn remember_joined(&mut self, point: JoinedPoint) {
        self.joined.insert(point);
        self.joined_order.push_back(point);
        while self.joined_order.len() > JOINED_POINT_CAPACITY {
            if let Some(oldest) = self.joined_order.pop_front() {
                self.joined.remove(&oldest);
            }
        }
    }

    fn remember_consumed_pre(&mut self, point: JoinedPoint) {
        self.consumed_pre.insert(point);
        self.consumed_pre_order.push_back(point);
        while self.consumed_pre_order.len() > JOINED_POINT_CAPACITY {
            if let Some(oldest) = self.consumed_pre_order.pop_front() {
                self.consumed_pre.remove(&oldest);
            }
        }
    }
}

pub struct MeterDeltaHistoryExchange {
    sample_rate: u32,
    meter_session: Arc<Mutex<MeterSession>>,
    delta: Mutex<DeltaHistoryState>,
}

impl MeterDeltaHistoryExchange {
    pub fn new(sample_rate: u32, meter_session: Arc<Mutex<MeterSession>>) -> Arc<Self> {
        Arc::new(Self {
            sample_rate,
            meter_session,
            delta: Mutex::new(DeltaHistoryState::default()),
        })
    }

    pub fn service_pre_endpoint(
        &self,
        pre_instance_id: &str,
        daw_session_id: &str,
        watch_owner_id: &str,
        instance_dir: &Path,
    ) -> Result<(), String> {
        let points = self
            .meter_session
            .try_lock()
            .map_err(|_| "meter session busy".to_string())?
            .recent_history(MeterHistoryResolution::Hz10, METER_HISTORY_EXCHANGE_POINTS)
            .into_iter()
            .filter_map(WirePoint::from_history)
            .collect();
        let publication = Publication {
            schema: METER_HISTORY_EXCHANGE_SCHEMA,
            pre_instance_id: pre_instance_id.to_string(),
            watch_owner_id: watch_owner_id.to_string(),
            daw_session_id: daw_session_id.to_string(),
            sample_rate: self.sample_rate,
            points,
        };
        let bytes = serde_json::to_vec(&publication).map_err(|error| error.to_string())?;
        crate::atomic_file::write_bytes_atomic(
            &instance_dir.join(METER_HISTORY_EXCHANGE_FILE),
            &bytes,
        )
        .map_err(|error| error.to_string())
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
        if !publication.valid_for(&identity, self.sample_rate) {
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
    fn valid_for(&self, identity: &PreIdentity, sample_rate: u32) -> bool {
        self.schema == METER_HISTORY_EXCHANGE_SCHEMA
            && self.sample_rate == sample_rate
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
