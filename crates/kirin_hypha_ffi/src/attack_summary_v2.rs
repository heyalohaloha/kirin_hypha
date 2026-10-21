//! Fixed-cohort Band Summary V2. The legacy band summary remains unchanged.

use crate::snapshot_interval::median_interval;
use crate::snapshot_types::*;
use crate::KirinAttackDetail;

#[path = "attack_summary_v2_aggregate.rs"]
mod aggregate;
#[path = "attack_summary_v2_all.rs"]
mod all;
#[path = "attack_summary_v2_band.rs"]
mod band;
#[path = "attack_summary_v2_cohort.rs"]
mod cohort;
#[path = "attack_summary_v2_revision.rs"]
mod revision;
#[cfg(test)]
pub(crate) use all::events as all_summary_events;
#[cfg(test)]
pub(crate) use all::representative as all_summary_representative;
#[path = "attack_summary_v2_poll.rs"]
mod poll;
pub(crate) use aggregate::{assemble_summary, SummaryEnvelope, SummaryEvent};
pub use poll::kirin_hypha_poll_attack_band_summary_v2;
pub(crate) use poll::proof_revision as attack_proof_revision;

pub const KIRIN_ATTACK_BAND_SUMMARY_V2_VERSION: u32 = 2;
pub const KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY: usize = 8;
pub const KIRIN_ATTACK_BAND_SUMMARY_V2_HEAD: usize = 96;
pub const KIRIN_ATTACK_BAND_SUMMARY_V2_TAIL: usize = 64;
pub const KIRIN_RENDER_WHOLE_POINT: u8 = 0;
pub const KIRIN_RENDER_WHOLE_INTERVAL: u8 = 1;
pub const KIRIN_RENDER_CONFIRMED_SUBSET: u8 = 2;
pub const KIRIN_RENDER_NO_SCALAR: u8 = 3;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct KirinAttackBandSummaryV2Request {
    pub version: u32,
    pub struct_size: u32,
    pub target: u8,
    pub band: u8,
    pub reserved: [u8; 6],
}

impl KirinAttackBandSummaryV2Request {
    pub fn is_valid(&self) -> bool {
        self.version == KIRIN_ATTACK_BAND_SUMMARY_V2_VERSION
            && self.struct_size as usize == std::mem::size_of::<Self>()
            && self.target <= KIRIN_TARGET_DELTA
            && self.band <= 8
            && self.reserved == [0; 6]
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinAttackBandLaneSummaryV2 {
    /// Exact, bound, unknown, pending, not-applicable. Sum is cohort_count.
    pub class_count: [u8; 5],
    pub cohort_count: u8,
    pub whole_median_available: u8,
    pub whole_numeric_informative: u8,
    pub whole_interval: KirinSnapshotInterval,
    /// Meaningful only when exact_count > 0.
    pub exact_median: f64,
    /// Meaningful only when exact_count > 0; cutoff - this is the subset's age.
    pub exact_latest_event_sample: i64,
    pub resolution: f64,
    pub reason_count: [u8; KIRIN_REASON_CAPACITY],
    pub exact_count: u8,
    pub render_kind: u8,
    pub whole_within_resolution: u8,
    pub reserved: [u8; 5],
}

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct KirinAttackBandAveragePointV2 {
    pub participating_bits: u8,
    pub valid_count: u8,
    pub connect_previous: u8,
    pub has_pre: u8,
    pub reserved: [u8; 4],
    pub pre_mean: f64,
    pub pre_min: f64,
    pub pre_max: f64,
    pub post_mean: f64,
    pub post_min: f64,
    pub post_max: f64,
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KirinAttackBandSummaryV2 {
    pub header: KirinSnapshotHeader,
    pub cohort_count: u8,
    /// Newer content events that are still acquiring measurement or mapping evidence.
    pub newer_unfinished_count: u8,
    pub reserved: [u8; 6],
    pub events: [KirinSnapshotEventKey; KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY],
    /// Matched=0, PRE-only=1, POST-only=2, ambiguous=3, POST-alone=4.
    pub pair_kind: [u8; KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY],
    /// Oldest first; band 0 uses TRANSIENT, STRENGTH, CREST, SHARPNESS; others DELAY, ATT, REL, LEVEL.
    pub evidence: [[KirinSnapshotScalarEvidence; 4]; KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY],
    pub lanes: [KirinAttackBandLaneSummaryV2; 4],
    pub head: [KirinAttackBandAveragePointV2; KIRIN_ATTACK_BAND_SUMMARY_V2_HEAD],
    pub tail: [KirinAttackBandAveragePointV2; KIRIN_ATTACK_BAND_SUMMARY_V2_TAIL],
    /// Comparison source/clock/meaning identity, including when no completed PRE hit exists.
    pub comparison_binding: [u8; 32],
    /// Existing detail of the newest completed ALL cohort hit; never a new measurement.
    pub representative_pre: KirinAttackDetail,
    pub representative_post: KirinAttackDetail,
    pub has_representative_pre: u8,
    pub has_representative_post: u8,
    pub representative_reserved: [u8; 6],
}

impl Default for KirinAttackBandSummaryV2 {
    fn default() -> Self {
        Self {
            header: KirinSnapshotHeader::default(),
            cohort_count: 0,
            newer_unfinished_count: 0,
            reserved: [0; 6],
            events: [KirinSnapshotEventKey::default(); KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY],
            pair_kind: [0; KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY],
            evidence: [[KirinSnapshotScalarEvidence::default(); 4];
                KIRIN_ATTACK_BAND_SUMMARY_V2_CAPACITY],
            lanes: [KirinAttackBandLaneSummaryV2::default(); 4],
            head: [KirinAttackBandAveragePointV2::default(); KIRIN_ATTACK_BAND_SUMMARY_V2_HEAD],
            tail: [KirinAttackBandAveragePointV2::default(); KIRIN_ATTACK_BAND_SUMMARY_V2_TAIL],
            comparison_binding: [0; 32],
            representative_pre: KirinAttackDetail::default(),
            representative_post: KirinAttackDetail::default(),
            has_representative_pre: 0,
            has_representative_post: 0,
            representative_reserved: [0; 6],
        }
    }
}

fn summarize_lane(
    events: &[SummaryEvent],
    lane: usize,
    target: u8,
) -> Option<KirinAttackBandLaneSummaryV2> {
    let mut result = KirinAttackBandLaneSummaryV2 {
        cohort_count: events.len() as u8,
        render_kind: KIRIN_RENDER_NO_SCALAR,
        ..Default::default()
    };
    let mut intervals = Vec::with_capacity(events.len());
    let mut exact = Vec::with_capacity(events.len());
    for event in events {
        let evidence = event.lanes[lane];
        if !evidence.is_valid() {
            return None;
        }
        result.class_count[evidence.class as usize] += 1;
        result.reason_count[evidence.reason as usize] += 1;
        result.resolution = result.resolution.max(evidence.resolution);
        if evidence.has_interval == 1 && !evidence.interval.is_all_real() {
            intervals.push(evidence.interval);
        }
        if evidence.class == KIRIN_SCALAR_EXACT {
            exact.push(evidence.interval);
            if result.exact_count == 0 || event.key.event_sample > result.exact_latest_event_sample
            {
                result.exact_latest_event_sample = event.key.event_sample;
            }
            result.exact_count += 1;
        }
    }
    if let Some(median) = median_interval(&exact) {
        result.exact_median = median.lower.value;
    }
    if !intervals.is_empty() {
        let whole = median_interval(&intervals)?;
        result.whole_median_available = 1;
        result.whole_interval = whole;
        result.whole_numeric_informative = u8::from(!whole.is_all_real());
        if result.whole_numeric_informative == 1 {
            result.render_kind = if whole.is_point() {
                KIRIN_RENDER_WHOLE_POINT
            } else {
                KIRIN_RENDER_WHOLE_INTERVAL
            };
            if target == KIRIN_TARGET_DELTA {
                result.whole_within_resolution = u8::from(
                    whole.lower.extended_value() >= -result.resolution
                        && whole.upper.extended_value() <= result.resolution,
                );
            }
        }
    }
    if result.whole_numeric_informative == 0 && result.exact_count > 0 {
        result.render_kind = KIRIN_RENDER_CONFIRMED_SUBSET;
    }
    Some(result)
}

#[cfg(test)]
#[path = "attack_summary_v2_tests.rs"]
mod tests;
