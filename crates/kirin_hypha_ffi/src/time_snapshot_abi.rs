//! Independent, sized TIME ABI. The old Observatory/MeterHistory/ABI layouts do not change.
use kirin_measure::meter_session::{TimeRawPoint, TimeSourceSpan};
use kirin_measure::{MeterHistoryEntry, MeterHistoryRange};
use std::time::Instant;

pub const KIRIN_TIME_SNAPSHOT_VERSION: u32 = 2;
pub const KIRIN_TIME_HISTORY_CAPACITY: u32 = 1200;
pub const KIRIN_TIME_CURRENT_LIVE: u8 = 1;
pub const KIRIN_TIME_CURRENT_MISSING: u8 = 2;
pub const KIRIN_TIME_CURRENT_EXPIRED: u8 = 3;
pub const KIRIN_TIME_CURRENT_STOPPED: u8 = 4;
pub const KIRIN_TIME_CURRENT_WAITING: u8 = 5;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinTimeSourceSpanV2 {
    pub epoch: u64,
    pub incarnation: u64,
    pub generation: u64,
    pub token: u64,
    pub sample_rate: u32,
    pub channels: u8,
    pub reserved: [u8; 3],
}
impl From<TimeSourceSpan> for KirinTimeSourceSpanV2 {
    fn from(p: TimeSourceSpan) -> Self {
        Self {
            epoch: p.epoch,
            incarnation: p.incarnation,
            generation: p.generation,
            token: p.token,
            sample_rate: p.sample_rate,
            channels: p.channels,
            reserved: [0; 3],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KirinTimeCurrentV2 {
    pub span: KirinTimeSourceSpanV2,
    pub cutoff: u64,
    pub run: u64,
    pub endpoint: i64,
    pub completion_age_ms: f64,
    pub remaining_ms: f64,
    /// M, S, TP, PSR, PLR, CORR. Missing is NaN; bits identify finite facts.
    pub values: [f64; 6],
    pub target: u8,
    pub state: u8,
    pub clock: u8,
    pub finite_mask: u8,
    pub reserved: [u8; 4],
}
impl Default for KirinTimeCurrentV2 {
    fn default() -> Self {
        Self {
            span: Default::default(),
            cutoff: 0,
            run: 0,
            endpoint: i64::MIN,
            completion_age_ms: f64::NAN,
            remaining_ms: 0.0,
            values: [f64::NAN; 6],
            target: 0,
            state: KIRIN_TIME_CURRENT_WAITING,
            clock: 0,
            finite_mask: 0,
            reserved: [0; 4],
        }
    }
}
impl KirinTimeCurrentV2 {
    pub(super) fn raw(point: &TimeRawPoint, target: u8, active: bool, now: Instant) -> Self {
        let remaining = point.remaining(now);
        let mut result = Self {
            span: point.wire.span.into(),
            cutoff: point.wire.observed,
            run: point.wire.run,
            endpoint: point.wire.endpoint.unwrap_or(i64::MIN),
            completion_age_ms: now.saturating_duration_since(point.completed).as_secs_f64()
                * 1000.0,
            remaining_ms: remaining.as_secs_f64() * 1000.0,
            target,
            clock: point.wire.clock,
            state: if !active {
                KIRIN_TIME_CURRENT_STOPPED
            } else if remaining.is_zero() {
                KIRIN_TIME_CURRENT_EXPIRED
            } else if !point.wire.usable {
                KIRIN_TIME_CURRENT_MISSING
            } else {
                KIRIN_TIME_CURRENT_LIVE
            },
            ..Default::default()
        };
        if result.state == KIRIN_TIME_CURRENT_LIVE {
            for (i, value) in point.wire.values.iter().enumerate() {
                if let Some(value) = value.filter(|v| v.is_finite()) {
                    result.values[i] = value;
                    result.finite_mask |= 1 << i;
                }
            }
            if result.finite_mask == 0 {
                result.state = KIRIN_TIME_CURRENT_MISSING;
            }
        }
        result
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinTimeComponentV2 {
    pub current: KirinTimeCurrentV2,
    pub pre_span: KirinTimeSourceSpanV2,
    pub pre_run: u64,
    pub binding_revision: u64,
    pub locator_identity: u64,
    pub owner_identity: u64,
    pub claim_identity: u64,
    pub history_count: u32,
    pub history_hold: u8,
    pub reason: u8,
    pub reserved: [u8; 2],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinTimeSnapshotV2 {
    pub version: u32,
    pub struct_size: u32,
    pub revision: u64,
    pub local_cutoff: u64,
    pub range_start: u64,
    pub post_span: KirinTimeSourceSpanV2,
    pub binding_revision: u64,
    pub signal_state: u8,
    pub selection_intent: u8,
    pub reserved: [u8; 6],
    pub main: KirinTimeComponentV2,
    pub psr: KirinTimeComponentV2,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub struct KirinTimeSnapshotRequestV2 {
    pub version: u32,
    pub struct_size: u32,
    pub packet_size: u32,
    pub entry_size: u32,
    pub duration_frames: u64,
    pub main_target: u32,
    pub resolution: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinTimeRangeV2 {
    pub min: f64,
    pub max: f64,
    pub mean: f64,
}
impl From<MeterHistoryRange> for KirinTimeRangeV2 {
    fn from(p: MeterHistoryRange) -> Self {
        Self {
            min: p.min.unwrap_or(f64::NAN),
            max: p.max.unwrap_or(f64::NAN),
            mean: p.mean.unwrap_or(f64::NAN),
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinTimeHistoryEntryV2 {
    pub epoch: u64,
    pub generation: u64,
    pub run: u64,
    pub segment: u64,
    pub first_observed: u64,
    pub last_observed: u64,
    pub first_endpoint: i64,
    pub last_endpoint: i64,
    /// M, S, TP, CORR, PSR, with independent valid denominators.
    pub ranges: [KirinTimeRangeV2; 5],
    pub clip_events: [u32; 6],
    pub valid_count: [u16; 5],
    pub total_count: u16,
    pub clock: u8,
    pub connects_previous: u8,
    pub resolution: u8,
    pub reserved: u8,
}
impl From<MeterHistoryEntry> for KirinTimeHistoryEntryV2 {
    fn from(p: MeterHistoryEntry) -> Self {
        Self {
            epoch: p.measurement_epoch,
            generation: p.generation,
            run: p.run_id,
            segment: p.segment_id,
            first_observed: p.first_observed_frames,
            last_observed: p.last_observed_frames,
            first_endpoint: p.first_timeline_endpoint_samples.unwrap_or(i64::MIN),
            last_endpoint: p.last_timeline_endpoint_samples.unwrap_or(i64::MIN),
            ranges: [
                p.lufs_m.into(),
                p.lufs_s.into(),
                p.true_peak.into(),
                p.correlation.into(),
                p.psr.into(),
            ],
            clip_events: p.clip_event_count,
            valid_count: p.valid_count,
            total_count: p.observation_count,
            clock: p.timeline_source as u8,
            connects_previous: p.connects_previous as u8,
            resolution: p.resolution as u8,
            reserved: 0,
        }
    }
}
