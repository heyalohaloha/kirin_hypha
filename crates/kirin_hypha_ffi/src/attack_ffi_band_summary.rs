//! DRUM band summary (2026-09-29): the recent hits that rise in the chosen band, summed up once so
//! the view can say, steadily, what the chain did to them.
//!
//! The hits are the lanes' hits (`sources`). The summary takes the newest eight whose band rises
//! on every side the view compares: both sides of a matched pair while POST - PRE is shown, POST
//! alone otherwise. A hit whose band only rings on, is silent or was not kept is left out and
//! counted; one still being measured is neither. Each lane gives the median of those hits, their
//! lowest and highest value, each hit's value and, for POST - PRE, how many lie on the median's
//! side of zero; a median inside what the band can tell apart is stated as such. The panes' marks
//! are the medians of each side's arrival and release end, and their envelopes the average of the
//! summed hits. Nothing here measures: every number is one the ATTACK workers stated.

use super::map::{to_c_envelope, Source};
use super::*;

pub const KIRIN_ATTACK_BAND_SUMMARY_HITS: usize = 8;
/// No summed hit has a value in the lane (DELAY without PRE, or nothing measured yet).
pub const KIRIN_ATTACK_BAND_LANE_NONE: u8 = 0;
/// The median states a difference, or POST's own value.
pub const KIRIN_ATTACK_BAND_LANE_VALUE: u8 = 1;
/// The median is inside `within`: no difference the band can tell apart (POST - PRE), or a POST
/// ATT shorter than the band's resolution, stated as that bound.
pub const KIRIN_ATTACK_BAND_LANE_WITHIN: u8 = 2;
/// A POST - PRE level inside this is no difference.
pub const KIRIN_ATTACK_BAND_LEVEL_WITHIN_DB: f32 = 0.2;
/// Why summed hits have no value in a lane, the most common reason among them: none withheld,
/// a start hidden by the previous ring-out, a fall cut by the next hit, a tail past the window.
pub const KIRIN_ATTACK_BAND_HELD_NONE: u8 = 0;
pub const KIRIN_ATTACK_BAND_HELD_RINGING: u8 = 1;
pub const KIRIN_ATTACK_BAND_HELD_NEXT_HIT: u8 = 2;
pub const KIRIN_ATTACK_BAND_HELD_LONG_TAIL: u8 = 3;

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KirinAttackBandLaneSummary {
    pub state: u8,
    /// Summed hits with a value in this lane.
    pub count: u8,
    /// Of them, those on the median's side of zero (POST - PRE with a stated difference only).
    pub agree: u8,
    /// KIRIN_ATTACK_BAND_HELD_*: why the summed hits without a value have none.
    pub withheld: u8,
    pub median: f32,
    pub low: f32,
    pub high: f32,
    /// The bound of `WITHIN`.
    pub within: f32,
    /// Each summed hit's value, oldest first; NaN where the hit has none.
    pub values: [f32; KIRIN_ATTACK_BAND_SUMMARY_HITS],
}

impl Default for KirinAttackBandLaneSummary {
    fn default() -> Self {
        Self {
            state: KIRIN_ATTACK_BAND_LANE_NONE,
            count: 0,
            agree: 0,
            withheld: KIRIN_ATTACK_BAND_HELD_NONE,
            median: f32::NAN,
            low: f32::NAN,
            high: f32::NAN,
            within: 0.0,
            values: [f32::NAN; KIRIN_ATTACK_BAND_SUMMARY_HITS],
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct KirinAttackBandSummary {
    /// The pair view's status vocabulary.
    pub status: u8,
    pub band: u8,
    pub pre_band: u8,
    /// 1: POST - PRE of matched pairs; 0: POST's own values.
    pub delta: u8,
    /// Hits summed, at most eight.
    pub count: u32,
    /// Hits between the oldest summed one and now whose band did not rise.
    pub left_out: u32,
    pub resolution_micros: u32,
    pub generation: u64,
    pub sample_rate: u32,
    pub reserved: u32,
    /// The summed hits, oldest first, by the lanes' key.
    pub event_samples: [i64; KIRIN_ATTACK_BAND_SUMMARY_HITS],
    /// DELAY, ATT, REL and LEVEL.
    pub lanes: [KirinAttackBandLaneSummary; 4],
    /// Medians, ms from the onset; NaN when no summed hit has one.
    pub pre_arrival_ms: f32,
    pub post_arrival_ms: f32,
    pub pre_release_end_ms: f32,
    pub post_release_end_ms: f32,
    /// The summed hits' average envelopes (PRE only for POST - PRE), and POST's lowest and
    /// highest point by point; NaN where there is none.
    pub pre: KirinAttackBandEnvelope,
    pub post: KirinAttackBandEnvelope,
    pub post_low: KirinAttackBandEnvelope,
    pub post_high: KirinAttackBandEnvelope,
}

fn nan_envelope() -> KirinAttackBandEnvelope {
    KirinAttackBandEnvelope {
        head_dbfs: [f32::NAN; KIRIN_ATTACK_BAND_HEAD_POINTS],
        tail_dbfs: [f32::NAN; KIRIN_ATTACK_BAND_TAIL_POINTS],
    }
}

impl Default for KirinAttackBandSummary {
    fn default() -> Self {
        Self {
            status: 0,
            band: 0,
            pre_band: KIRIN_ATTACK_BAND_PRE_OFF,
            delta: 0,
            count: 0,
            left_out: 0,
            resolution_micros: 0,
            generation: 0,
            sample_rate: 0,
            reserved: 0,
            event_samples: [0; KIRIN_ATTACK_BAND_SUMMARY_HITS],
            lanes: [KirinAttackBandLaneSummary::default(); 4],
            pre_arrival_ms: f32::NAN,
            post_arrival_ms: f32::NAN,
            pre_release_end_ms: f32::NAN,
            post_release_end_ms: f32::NAN,
            pre: nan_envelope(),
            post: nan_envelope(),
            post_low: nan_envelope(),
            post_high: nan_envelope(),
        }
    }
}

fn rises(side: &KirinAttackBandSide) -> bool {
    side.state == KIRIN_ATTACK_BAND_SIDE_RISES
}

fn arrives(side: &KirinAttackBandSide) -> bool {
    rises(side) && side.arrival_state == KIRIN_ATTACK_BAND_ARRIVAL_AT
}

fn releases(side: &KirinAttackBandSide) -> bool {
    rises(side) && side.release_state == KIRIN_ATTACK_BAND_RELEASE_AT
}

fn decided(side: &KirinAttackBandSide) -> bool {
    side.state != KIRIN_ATTACK_BAND_SIDE_PENDING && side.state != KIRIN_ATTACK_BAND_SIDE_ABSENT
}

/// Whether the hit is summed, left out, or neither (still measuring, or not comparable).
enum Standing {
    Summed,
    LeftOut,
    Neither,
}

fn standing(hit: &KirinAttackBandHit, delta: bool) -> Standing {
    if delta {
        if hit.kind != 0 || !decided(&hit.pre) || !decided(&hit.post) {
            return Standing::Neither;
        }
        return if rises(&hit.pre) && rises(&hit.post) {
            Standing::Summed
        } else {
            Standing::LeftOut
        };
    }
    if !decided(&hit.post) {
        return Standing::Neither;
    }
    if rises(&hit.post) {
        Standing::Summed
    } else {
        Standing::LeftOut
    }
}

fn median(sorted: &[f32]) -> f32 {
    let middle = sorted.len() / 2;
    if sorted.len() % 2 == 1 {
        sorted[middle]
    } else {
        0.5 * (sorted[middle - 1] + sorted[middle])
    }
}

fn median_of(values: impl Iterator<Item = f32>) -> f32 {
    let mut values = values.filter(|value| value.is_finite()).collect::<Vec<_>>();
    if values.is_empty() {
        return f32::NAN;
    }
    values.sort_by(f32::total_cmp);
    median(&values)
}

/// Why a side has no start or fall to compare, if it has none.
fn start_held(sides: &[&KirinAttackBandSide]) -> u8 {
    if sides.iter().all(|side| arrives(side)) {
        KIRIN_ATTACK_BAND_HELD_NONE
    } else {
        KIRIN_ATTACK_BAND_HELD_RINGING
    }
}

fn fall_held(sides: &[&KirinAttackBandSide]) -> u8 {
    if sides
        .iter()
        .any(|side| side.release_state == KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT)
    {
        KIRIN_ATTACK_BAND_HELD_NEXT_HIT
    } else if sides.iter().all(|side| releases(side)) {
        KIRIN_ATTACK_BAND_HELD_NONE
    } else {
        KIRIN_ATTACK_BAND_HELD_LONG_TAIL
    }
}

/// The most common reason among `held` (ties to the earlier reason).
fn most_common(held: &[u8]) -> u8 {
    let mut counts = [0_usize; 4];
    for reason in held {
        counts[usize::from(*reason).min(3)] += 1;
    }
    (1..4)
        .fold((KIRIN_ATTACK_BAND_HELD_NONE, 0), |best, reason| {
            if counts[reason] > best.1 {
                (reason as u8, counts[reason])
            } else {
                best
            }
        })
        .0
}

fn lane(
    values: [f32; KIRIN_ATTACK_BAND_SUMMARY_HITS],
    held: &[u8],
    within: f32,
    delta: bool,
    bound_below: bool,
) -> KirinAttackBandLaneSummary {
    let mut lane = KirinAttackBandLaneSummary {
        values,
        within,
        withheld: most_common(held),
        ..Default::default()
    };
    let mut present = values
        .iter()
        .copied()
        .filter(|value| value.is_finite())
        .collect::<Vec<_>>();
    if present.is_empty() {
        return lane;
    }
    present.sort_by(f32::total_cmp);
    lane.count = present.len() as u8;
    lane.median = median(&present);
    lane.low = present[0];
    lane.high = present[present.len() - 1];
    let inside = if delta {
        lane.median.abs() <= within
    } else {
        bound_below && lane.median < within
    };
    lane.state = if inside {
        KIRIN_ATTACK_BAND_LANE_WITHIN
    } else {
        KIRIN_ATTACK_BAND_LANE_VALUE
    };
    if delta && !inside {
        lane.agree = present
            .iter()
            .filter(|value| (**value > 0.0) == (lane.median > 0.0) && **value != 0.0)
            .count() as u8;
    }
    lane
}

fn average(envelopes: &[KirinAttackBandEnvelope]) -> KirinAttackBandEnvelope {
    let mut sum = KirinAttackBandEnvelope::default();
    if envelopes.is_empty() {
        return nan_envelope();
    }
    for envelope in envelopes {
        for (total, value) in sum.head_dbfs.iter_mut().zip(envelope.head_dbfs) {
            *total += value;
        }
        for (total, value) in sum.tail_dbfs.iter_mut().zip(envelope.tail_dbfs) {
            *total += value;
        }
    }
    let count = envelopes.len() as f32;
    sum.head_dbfs.iter_mut().for_each(|value| *value /= count);
    sum.tail_dbfs.iter_mut().for_each(|value| *value /= count);
    sum
}

fn extreme(
    envelopes: &[KirinAttackBandEnvelope],
    pick: fn(f32, f32) -> f32,
) -> KirinAttackBandEnvelope {
    let Some(first) = envelopes.first() else {
        return nan_envelope();
    };
    let mut result = *first;
    for envelope in &envelopes[1..] {
        for (value, other) in result.head_dbfs.iter_mut().zip(envelope.head_dbfs) {
            *value = pick(*value, other);
        }
        for (value, other) in result.tail_dbfs.iter_mut().zip(envelope.tail_dbfs) {
            *value = pick(*value, other);
        }
    }
    result
}

/// Sums up `hits` (the lanes' hits, oldest first) into `summary`. `resolution_ms` is one period
/// of the band's centre.
pub(super) fn summarise(
    summary: &mut KirinAttackBandSummary,
    hits: &[Source<'_>],
    delta: bool,
    resolution_ms: f32,
) {
    summary.delta = u8::from(delta);
    let mut summed = Vec::with_capacity(KIRIN_ATTACK_BAND_SUMMARY_HITS);
    let mut left_out = 0_u32;
    for source in hits.iter().rev() {
        if summed.len() == KIRIN_ATTACK_BAND_SUMMARY_HITS {
            break;
        }
        match standing(&source.hit, delta) {
            Standing::Summed => summed.push(source),
            Standing::LeftOut => left_out += 1,
            Standing::Neither => {}
        }
    }
    summed.reverse();
    summary.count = summed.len() as u32;
    summary.left_out = if summed.is_empty() { 0 } else { left_out };
    let mut values = [[f32::NAN; KIRIN_ATTACK_BAND_SUMMARY_HITS]; 4];
    let (mut start, mut fall) = (Vec::new(), Vec::new());
    for (index, source) in summed.iter().enumerate() {
        summary.event_samples[index] = source.hit.event_sample;
        let (pre, post) = (&source.hit.pre, &source.hit.post);
        let sides: &[&KirinAttackBandSide] = if delta { &[pre, post] } else { &[post] };
        let (start_reason, fall_reason) = (start_held(sides), fall_held(sides));
        if start_reason == KIRIN_ATTACK_BAND_HELD_NONE {
            if delta {
                values[0][index] = post.arrival_ms - pre.arrival_ms;
                values[1][index] = post.attack_ms - pre.attack_ms;
            } else {
                values[1][index] = post.attack_ms;
            }
        } else {
            start.push(start_reason);
        }
        if fall_reason == KIRIN_ATTACK_BAND_HELD_NONE {
            values[2][index] = if delta {
                post.release_ms - pre.release_ms
            } else {
                post.release_ms
            };
        } else {
            fall.push(fall_reason);
        }
        values[3][index] = if delta {
            post.level_dbfs - pre.level_dbfs
        } else {
            post.level_dbfs
        };
    }
    // DELAY is timed on the envelope's crossing, finer than a period; the others by the period.
    let delay_within = (resolution_ms / 32.0).max(0.2);
    summary.lanes = [
        lane(
            values[0],
            if delta { &start } else { &[] },
            delay_within,
            delta,
            false,
        ),
        lane(values[1], &start, resolution_ms, delta, true),
        lane(values[2], &fall, resolution_ms, delta, false),
        lane(
            values[3],
            &[],
            KIRIN_ATTACK_BAND_LEVEL_WITHIN_DB,
            delta,
            false,
        ),
    ];
    let hits_of = || summed.iter().map(|source| &source.hit);
    let end = |side: &KirinAttackBandSide| side.peak_ms + side.release_ms;
    summary.post_arrival_ms = median_of(
        hits_of()
            .filter(|hit| arrives(&hit.post))
            .map(|hit| hit.post.arrival_ms),
    );
    summary.post_release_end_ms = median_of(
        hits_of()
            .filter(|hit| releases(&hit.post))
            .map(|hit| end(&hit.post)),
    );
    if delta {
        summary.pre_arrival_ms = median_of(
            hits_of()
                .filter(|hit| arrives(&hit.pre))
                .map(|hit| hit.pre.arrival_ms),
        );
        summary.pre_release_end_ms = median_of(
            hits_of()
                .filter(|hit| releases(&hit.pre))
                .map(|hit| end(&hit.pre)),
        );
        let pre = summed
            .iter()
            .map(|source| to_c_envelope(source.pre))
            .collect::<Vec<_>>();
        summary.pre = average(&pre);
    }
    let post = summed
        .iter()
        .map(|source| to_c_envelope(source.post))
        .collect::<Vec<_>>();
    summary.post = average(&post);
    summary.post_low = extreme(&post, f32::min);
    summary.post_high = extreme(&post, f32::max);
}

#[cfg(test)]
#[path = "attack_ffi_band_summary_tests.rs"]
mod tests;
