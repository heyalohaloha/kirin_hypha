//! How a lanes hit maps to its band sides: the pair events while a pair is active, POST's own
//! details otherwise, each side exactly what its worker stated. The batch and the envelope poll
//! both come from here.

use kirin_measure::attack_perception::band::{
    dbfs_from_centi, AttackBand, BandArrival, BandRelease, BandSound,
};
use kirin_measure::attack_runtime::{AttackBandDetail, AttackBandResults, AttackPreBand};
use kirin_measure::{
    AttackHistory, AttackPairEventKind, AttackPairViewSnapshot, SpectrumViewStatus,
};

use super::*;

/// A lanes hit and the band details behind its two sides.
pub(super) struct Source<'a> {
    pub(super) hit: KirinAttackBandHit,
    pub(super) pre: Option<&'a AttackBandDetail>,
    pub(super) post: Option<&'a AttackBandDetail>,
}

fn kind_code(kind: AttackPairEventKind) -> u8 {
    match kind {
        AttackPairEventKind::Matched => 0,
        AttackPairEventKind::PreOnly => 1,
        AttackPairEventKind::PostOnly => 2,
        AttackPairEventKind::Ambiguous => 3,
    }
}

pub(super) fn pre_band_code(state: AttackPreBand) -> u8 {
    match state {
        AttackPreBand::Off => KIRIN_ATTACK_BAND_PRE_OFF,
        AttackPreBand::Same => KIRIN_ATTACK_BAND_PRE_SAME,
        AttackPreBand::Waiting => KIRIN_ATTACK_BAND_PRE_WAITING,
        AttackPreBand::Predates => KIRIN_ATTACK_BAND_PRE_PREDATES,
    }
}

pub(super) fn status_code(status: SpectrumViewStatus) -> u8 {
    match status {
        SpectrumViewStatus::Hidden => 0,
        SpectrumViewStatus::NoPair => 1,
        SpectrumViewStatus::WarmingUp => 2,
        SpectrumViewStatus::Active => 3,
        SpectrumViewStatus::Unavailable => 4,
        SpectrumViewStatus::InUse => 5,
    }
}

fn ms(frames: f32, sample_rate: u32) -> f32 {
    frames * 1_000.0 / sample_rate as f32
}

/// A side's record: pending until a result exists, then exactly what the worker stated.
fn to_c_side(detail: Option<&AttackBandDetail>) -> KirinAttackBandSide {
    let Some(detail) = detail else {
        return KirinAttackBandSide::default();
    };
    let Some(measure) = detail.measure else {
        return KirinAttackBandSide {
            state: KIRIN_ATTACK_BAND_SIDE_NOT_KEPT,
            ..Default::default()
        };
    };
    let rate = measure.sample_rate;
    let mut side = KirinAttackBandSide {
        peak_ms: ms(measure.peak_frames, rate),
        level_dbfs: measure.level_dbfs,
        ..Default::default()
    };
    match measure.sound {
        BandSound::Silent => side.state = KIRIN_ATTACK_BAND_SIDE_SILENT,
        BandSound::RingsOn => side.state = KIRIN_ATTACK_BAND_SIDE_RINGS_ON,
        BandSound::Rises { arrival, release } => {
            side.state = KIRIN_ATTACK_BAND_SIDE_RISES;
            match arrival {
                BandArrival::At {
                    arrival_frames,
                    attack_frames,
                } => {
                    side.arrival_state = KIRIN_ATTACK_BAND_ARRIVAL_AT;
                    side.arrival_ms = ms(arrival_frames, rate);
                    side.attack_ms = ms(attack_frames, rate);
                }
                BandArrival::Ringing => side.arrival_state = KIRIN_ATTACK_BAND_ARRIVAL_RINGING,
            }
            let (state, frames) = match release {
                BandRelease::At(frames) => (KIRIN_ATTACK_BAND_RELEASE_AT, frames),
                BandRelease::CutByNextHit => (KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT, 0.0),
                BandRelease::AtLeast(frames) => (KIRIN_ATTACK_BAND_RELEASE_AT_LEAST, frames),
            };
            side.release_state = state;
            side.release_ms = ms(frames, rate);
        }
    }
    side
}

fn absent() -> KirinAttackBandSide {
    KirinAttackBandSide {
        state: KIRIN_ATTACK_BAND_SIDE_ABSENT,
        ..Default::default()
    }
}

/// Every hit the lanes show, oldest first, with the band details behind each side. `unpaired`
/// is POST's own history, as the lanes read it while no pair is active.
pub(super) fn sources<'a>(
    view: &'a AttackPairViewSnapshot,
    unpaired: Option<&'a AttackHistory>,
    post_results: &'a AttackBandResults,
    band: AttackBand,
) -> Vec<Source<'a>> {
    let post_results = (post_results.band == Some(band)).then_some(post_results);
    if view.status != SpectrumViewStatus::Active {
        let Some(history) = unpaired else {
            return Vec::new();
        };
        return history
            .details()
            .map(|detail| {
                let onset = detail.event.event_sample;
                let post = post_results
                    .and_then(|results| results.own_at(onset))
                    .filter(|post| {
                        post.matches_run(
                            detail.event.generation,
                            detail.event.sample_rate,
                            detail.event.channels,
                            &detail.event.definition_hash,
                        )
                    });
                Source {
                    hit: KirinAttackBandHit {
                        event_sample: onset,
                        measured_at_sample: onset,
                        kind: KIRIN_ATTACK_BAND_KIND_POST_ALONE,
                        pre: absent(),
                        post: to_c_side(post),
                        ..Default::default()
                    },
                    pre: None,
                    post,
                }
            })
            .collect();
    }
    let pre_results = (view.pre_band == AttackPreBand::Same)
        .then_some(view.pre_band_results.as_deref())
        .flatten()
        .filter(|results| results.band == Some(band));
    let skip = view
        .pair_events
        .len()
        .saturating_sub(KIRIN_ATTACK_BAND_BATCH_CAPACITY);
    view.pair_events
        .iter()
        .skip(skip)
        .map(|pair| {
            let has_pre = matches!(
                pair.kind,
                AttackPairEventKind::Matched | AttackPairEventKind::PreOnly
            );
            let pre = pre_results
                .zip(pair.pre_event_sample.filter(|_| has_pre))
                .and_then(|(results, onset)| results.own_at(onset))
                .filter(|pre| {
                    pre.matches_run(
                        pair.pre_generation,
                        pair.sample_rate,
                        pair.channels,
                        &pair.definition_hash,
                    )
                });
            let (measured_at, post) = match (pair.kind, pre_results) {
                // POST measured at the PRE onset over the PRE tail: the same content samples.
                (AttackPairEventKind::Matched, Some(_)) => {
                    let onset = pair.pre_event_sample.unwrap_or(pair.event_sample);
                    let post = pre
                        .and_then(|pre| pre.measure)
                        .zip(post_results)
                        .and_then(|(measure, results)| {
                            results.anchored_at(onset, measure.span_end_sample)
                        })
                        .filter(|post| {
                            post.matches_run(
                                pair.post_generation,
                                pair.sample_rate,
                                pair.channels,
                                &pair.definition_hash,
                            )
                        });
                    (onset, post)
                }
                // Without PRE's band, POST's own hit is what there is to show.
                (AttackPairEventKind::Matched | AttackPairEventKind::PostOnly, _) => {
                    let onset = pair.post_event_sample.unwrap_or(pair.event_sample);
                    (
                        onset,
                        post_results
                            .and_then(|results| results.own_at(onset))
                            .filter(|post| {
                                post.matches_run(
                                    pair.post_generation,
                                    pair.sample_rate,
                                    pair.channels,
                                    &pair.definition_hash,
                                )
                            }),
                    )
                }
                _ => (pair.pre_event_sample.unwrap_or(pair.event_sample), None),
            };
            let has_post = matches!(
                pair.kind,
                AttackPairEventKind::Matched | AttackPairEventKind::PostOnly
            );
            Source {
                hit: KirinAttackBandHit {
                    event_sample: pair.event_sample,
                    measured_at_sample: measured_at,
                    kind: kind_code(pair.kind),
                    pre: if has_pre { to_c_side(pre) } else { absent() },
                    post: if has_post { to_c_side(post) } else { absent() },
                    ..Default::default()
                },
                pre,
                post,
            }
        })
        .collect()
}

pub(super) fn to_c_envelope(detail: Option<&AttackBandDetail>) -> KirinAttackBandEnvelope {
    let mut envelope = KirinAttackBandEnvelope::default();
    if let Some(measure) = detail.and_then(|detail| detail.measure) {
        for (point, value) in envelope.head_dbfs.iter_mut().zip(measure.envelope.head) {
            *point = dbfs_from_centi(value);
        }
        for (point, value) in envelope.tail_dbfs.iter_mut().zip(measure.envelope.tail) {
            *point = dbfs_from_centi(value);
        }
    }
    envelope
}
