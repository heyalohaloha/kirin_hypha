//! Exact raw joins; original POST completion time is the only comparison TTL origin.
use super::*;
use crate::meter_session::time_observation::{TimeRawPoint, TimeSourceSpan, TimeWirePoint};

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
pub(super) struct TimePublication {
    pub(super) span: TimeSourceSpan,
    pub(super) points: Vec<TimeWirePoint>,
}

impl TimePublication {
    pub(super) fn valid(&self) -> bool {
        self.span.valid()
            && self.points.len() <= METER_HISTORY_EXCHANGE_POINTS
            && self.points.iter().all(|p| p.valid(self.span))
            && self
                .points
                .windows(2)
                .all(|p| p[0].observed < p[1].observed)
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
#[repr(u8)]
pub enum TimeComparisonReason {
    #[default]
    Waiting = 1,
    Active = 2,
    Stopped = 3,
    Incompatible = 4,
    Missing = 5,
}

#[derive(Clone, Debug)]
pub struct TimeComparisonView {
    pub binding_revision: u64,
    pub pre_instance_id: String,
    pub project_hash: String,
    pub owner_id: String,
    pub post_instance_id: String,
    pub claimed_at_bits: u64,
    pub pre_span: TimeSourceSpan,
    pub post_span: TimeSourceSpan,
    pub pre_run: u64,
    pub post_run: u64,
    pub cutoff: u64,
    pub point: Option<TimeRawPoint>,
    pub reason: TimeComparisonReason,
    pub history: Vec<MeterHistoryEntry>,
}

#[derive(Default)]
pub(super) struct TimeComparisonState {
    pre_span: Option<TimeSourceSpan>,
    post_span: Option<TimeSourceSpan>,
    pre_run: u64,
    post_run: u64,
    point: Option<TimeRawPoint>,
    reason: TimeComparisonReason,
    last_joined: u64,
    last_cutoff: u64,
    last_consumed_pre: u64,
    pre_admission_floor: u64,
    post_admission_floor: u64,
    joined_run: u64,
}

impl TimeComparisonState {
    pub(super) fn fail(&mut self, reason: TimeComparisonReason) {
        self.point = None;
        self.reason = reason;
    }

    pub(super) fn ingest(
        &mut self,
        publication: Option<&TimePublication>,
        post: &[TimeRawPoint],
        history: &mut MeterHistory,
    ) {
        let Some(pre) = publication.filter(|p| p.valid()) else {
            self.fail(TimeComparisonReason::Incompatible);
            return;
        };
        let Some(latest) = post.last() else {
            self.fail(TimeComparisonReason::Waiting);
            return;
        };
        let Some(pre_latest) = pre.points.last() else {
            self.fail(TimeComparisonReason::Waiting);
            return;
        };
        if post.iter().any(|p| !p.wire.valid(latest.wire.span))
            || !latest.wire.span.valid()
            || latest.wire.span.sample_rate != pre.span.sample_rate
            || latest.wire.span.channels != pre.span.channels
        {
            self.fail(TimeComparisonReason::Incompatible);
            return;
        }
        let established = self.pre_span.is_some() && self.post_span.is_some();
        let pre_changed =
            established && (self.pre_span != Some(pre.span) || self.pre_run != pre_latest.run);
        let post_changed = established
            && (self.post_span != Some(latest.wire.span) || self.post_run != latest.wire.run);
        // A one-sided lifecycle change retires every opposite-side slot already published at
        // that boundary, including unconsumed suffixes. Matching endpoint numbers do not make
        // old playback/new-worker data contemporaneous. Fresh opposite slots can resume without
        // requiring a permanently unchanged worker to invent a new run.
        let pre_floor = if self.pre_span != Some(pre.span) {
            0
        } else if post_changed && !pre_changed {
            pre_latest.observed
        } else {
            self.pre_admission_floor
        };
        let post_floor = if self.post_span != Some(latest.wire.span) {
            0
        } else if pre_changed && !post_changed {
            latest.wire.observed
        } else {
            self.post_admission_floor
        };
        if self.pre_span != Some(pre.span) || self.post_span != Some(latest.wire.span) {
            *self = Self::default();
            history.reset();
            self.pre_span = Some(pre.span);
            self.post_span = Some(latest.wire.span);
        }
        self.pre_admission_floor = if pre_changed && post_changed {
            0
        } else {
            pre_floor
        };
        self.post_admission_floor = if pre_changed && post_changed {
            0
        } else {
            post_floor
        };
        if self.pre_run != pre_latest.run || self.post_run != latest.wire.run {
            self.point = None;
            self.joined_run = self.joined_run.wrapping_add(1).max(1);
            self.pre_run = pre_latest.run;
            self.post_run = latest.wire.run;
        }
        let pre_counts = key_counts(
            pre.points
                .iter()
                .filter(|p| p.run == self.pre_run)
                .filter_map(|p| p.exact_key()),
        );
        let post_counts = key_counts(
            post.iter()
                .filter(|p| p.wire.run == self.post_run)
                .filter_map(|p| p.wire.exact_key()),
        );
        let by_key: HashMap<_, _> = pre
            .points
            .iter()
            .filter(|p| p.run == self.pre_run)
            .filter_map(|p| Some((p.exact_key()?, p)))
            .collect();
        history.set_step_frames(((u64::from(latest.wire.span.sample_rate) + 5) / 10).max(1));
        let last_joined = self.last_joined;
        for local in post
            .iter()
            .filter(|p| p.wire.run == self.post_run && p.wire.observed > last_joined)
        {
            let Some(key) = local.wire.exact_key() else {
                continue;
            };
            if pre_counts.get(&key) != Some(&1) || post_counts.get(&key) != Some(&1) {
                continue;
            }
            let Some(remote) = by_key.get(&key) else {
                continue;
            };
            // A PRE slot is used once. A POST seek must not pair with the previous playback
            // merely because its endpoint occurs again before PRE publishes the new run.
            if remote.observed <= self.last_consumed_pre
                || remote.observed <= self.pre_admission_floor
                || local.wire.observed <= self.post_admission_floor
            {
                continue;
            }
            let values = std::array::from_fn(|i| {
                if i == 4 {
                    None
                } else {
                    difference(local.wire.values[i], remote.values[i])
                }
            });
            let joined = TimeRawPoint {
                wire: TimeWirePoint {
                    values,
                    ..local.wire
                },
                completed: local.completed,
            };
            let result = MeasureResult {
                lufs_m: values[0],
                lufs_s: values[1],
                true_peak: values[2],
                psr: values[3],
                ..Default::default()
            };
            history.push(
                local.wire.span.epoch,
                local.wire.span.generation,
                self.joined_run,
                local.wire.observed,
                (
                    local.wire.endpoint,
                    match local.wire.clock {
                        1 => CaptureClockSource::ProjectTimeline,
                        2 => CaptureClockSource::AudioRenderTimeline,
                        _ => CaptureClockSource::Unknown,
                    },
                ),
                &result,
                MeterHistoryAux {
                    correlation: values[5],
                    clip_event_count: [0; crate::meter_history::METER_HISTORY_CHANNELS],
                },
            );
            self.last_consumed_pre = remote.observed;
            self.last_joined = local.wire.observed;
            self.last_cutoff = local.wire.observed;
            self.point = Some(joined);
            self.reason = TimeComparisonReason::Active;
        }
        // An endpoint beyond the newest PRE is normal publication lag. A covered but absent,
        // duplicated or incompatible endpoint is confirmed missing; never restore a finite point.
        let valid_latest = latest.wire.exact_key().is_some_and(|key| {
            post_counts.get(&key) == Some(&1)
                && ((pre_counts.get(&key) == Some(&1)
                    && by_key.get(&key).is_some_and(|p| {
                        (p.observed > self.last_consumed_pre
                            && p.observed > self.pre_admission_floor
                            && latest.wire.observed > self.post_admission_floor)
                            || self
                                .point
                                .as_ref()
                                .is_some_and(|p| p.wire.observed == latest.wire.observed)
                    }))
                    || pre_latest
                        .exact_key()
                        .is_some_and(|end| end.0 == key.0 && end.1 < key.1))
        });
        if !valid_latest {
            self.fail(TimeComparisonReason::Missing);
        }
    }

    pub(super) fn view(
        &self,
        pair: &PairKey,
        history: &MeterHistory,
        resolution: MeterHistoryResolution,
        lower: u64,
        cutoff: u64,
        capacity: usize,
    ) -> Result<Option<TimeComparisonView>, crate::meter_history::TimeHistoryCountOverflow> {
        let Some(binding) = pair.post_binding.as_ref() else {
            return Ok(None);
        };
        let Some(project_hash) = pair
            .instance_dir
            .parent()
            .and_then(|parent| parent.file_name())
            .and_then(|name| name.to_str())
        else {
            return Ok(None);
        };
        let endpoint = self.last_cutoff.min(cutoff);
        Ok(Some(TimeComparisonView {
            binding_revision: binding.generation,
            pre_instance_id: pair.instance_id.clone(),
            project_hash: project_hash.into(),
            owner_id: binding.pair_owner_id.clone(),
            post_instance_id: binding.post_instance_id.clone(),
            claimed_at_bits: binding.claimed_at_bits,
            pre_span: self.pre_span.unwrap_or_default(),
            post_span: self.post_span.unwrap_or_default(),
            pre_run: self.pre_run,
            post_run: self.post_run,
            cutoff: self.last_cutoff,
            point: self.point.clone(),
            reason: self.reason,
            history: history.time_range(resolution, lower, endpoint, capacity)?,
        }))
    }
}

#[cfg(test)]
#[path = "time_pair_observation_tests.rs"]
mod tests;
