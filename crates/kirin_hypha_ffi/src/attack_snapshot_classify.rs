//! Observation-to-scalar mapping shared by the new Single and fixed-cohort Summary.
//! Missing PCM, missing mapping and a valid unfinished request are distinct inputs.

use crate::snapshot_interval::subtract_intervals;
use crate::snapshot_types::*;
use kirin_measure::attack_perception::band::{AttackBand, BandArrival, BandRelease, BandSound};
use kirin_measure::attack_runtime::snapshot::{AttackBandObservation, AttackFinish};

pub const DELAY: usize = 0;
pub const ATT: usize = 1;
pub const REL: usize = 2;
pub const LEVEL: usize = 3;

#[derive(Clone, Copy)]
pub enum BandSideEvidence<'a> {
    Observation(&'a AttackBandObservation),
    /// Only an accepted, source-qualified live request may be Pending.
    Pending(u8),
    Missing(u8),
}

fn state(side: BandSideEvidence<'_>, lane: usize) -> KirinSnapshotScalarEvidence {
    let mut value = match side {
        BandSideEvidence::Pending(reason) => {
            KirinSnapshotScalarEvidence::unavailable(KIRIN_SCALAR_PENDING, reason)
        }
        BandSideEvidence::Missing(reason) => {
            KirinSnapshotScalarEvidence::unavailable(KIRIN_SCALAR_UNKNOWN, reason)
        }
        BandSideEvidence::Observation(observation) => {
            if !observation.valid() {
                return KirinSnapshotScalarEvidence::unavailable(
                    KIRIN_SCALAR_UNKNOWN,
                    KIRIN_REASON_SEMANTICS,
                );
            }
            let Some(measure) = observation.measure else {
                let (class, reason) = match observation.finish {
                    AttackFinish::Acquiring => (KIRIN_SCALAR_PENDING, KIRIN_REASON_WAITING_AUDIO),
                    AttackFinish::NotKept => (KIRIN_SCALAR_UNKNOWN, KIRIN_REASON_NOT_KEPT),
                    AttackFinish::Retired => (KIRIN_SCALAR_UNKNOWN, KIRIN_REASON_SOURCE_CHANGED),
                    _ => (KIRIN_SCALAR_UNKNOWN, KIRIN_REASON_NOT_KEPT),
                };
                let mut value = KirinSnapshotScalarEvidence::unavailable(class, reason);
                stamp(&mut value, observation);
                return value;
            };
            let rate = f64::from(measure.sample_rate);
            let ms = |frames: f32| f64::from(frames) * 1000.0 / rate;
            let point = |value, unit| {
                KirinSnapshotScalarEvidence::numeric(
                    KirinSnapshotInterval::point(value, unit),
                    KIRIN_REASON_NONE,
                )
            };
            let na = |reason| {
                KirinSnapshotScalarEvidence::unavailable(KIRIN_SCALAR_NOT_APPLICABLE, reason)
            };
            match (lane, measure.sound) {
                (LEVEL, BandSound::Silent) => KirinSnapshotScalarEvidence::numeric(
                    KirinSnapshotInterval {
                        lower: KirinSnapshotEndpoint::negative_infinity(),
                        upper: KirinSnapshotEndpoint::finite(-72.0, true),
                        unit: KIRIN_INTERVAL_DECIBELS,
                        reserved: [0; 7],
                    },
                    KIRIN_REASON_SILENT,
                ),
                (LEVEL, _) => point(f64::from(measure.level_dbfs), KIRIN_INTERVAL_DECIBELS),
                (_, BandSound::Silent) => na(KIRIN_REASON_SILENT),
                (_, BandSound::RingsOn) => na(KIRIN_REASON_RINGING),
                (
                    DELAY | ATT,
                    BandSound::Rises {
                        arrival: BandArrival::Ringing,
                        ..
                    },
                ) => na(KIRIN_REASON_RINGING),
                (
                    DELAY,
                    BandSound::Rises {
                        arrival: BandArrival::At { arrival_frames, .. },
                        ..
                    },
                ) => point(ms(arrival_frames), KIRIN_INTERVAL_MILLISECONDS),
                (
                    ATT,
                    BandSound::Rises {
                        arrival: BandArrival::At { attack_frames, .. },
                        ..
                    },
                ) => point(ms(attack_frames), KIRIN_INTERVAL_MILLISECONDS),
                (
                    REL,
                    BandSound::Rises {
                        release: BandRelease::At(frames),
                        ..
                    },
                ) => point(ms(frames), KIRIN_INTERVAL_MILLISECONDS),
                (
                    REL,
                    BandSound::Rises {
                        release: BandRelease::AtLeast(frames),
                        ..
                    },
                ) => KirinSnapshotScalarEvidence::numeric(
                    KirinSnapshotInterval {
                        lower: KirinSnapshotEndpoint::finite(ms(frames), true),
                        upper: KirinSnapshotEndpoint::positive_infinity(),
                        unit: KIRIN_INTERVAL_MILLISECONDS,
                        reserved: [0; 7],
                    },
                    if observation.finish == AttackFinish::AudioEnd {
                        KIRIN_REASON_AUDIO_END
                    } else {
                        KIRIN_REASON_LONG_TAIL
                    },
                ),
                (
                    REL,
                    BandSound::Rises {
                        release: BandRelease::CutByNextHit,
                        ..
                    },
                ) => KirinSnapshotScalarEvidence::unavailable(
                    KIRIN_SCALAR_UNKNOWN,
                    KIRIN_REASON_NEXT_HIT,
                ),
                _ => KirinSnapshotScalarEvidence::unavailable(
                    KIRIN_SCALAR_UNKNOWN,
                    KIRIN_REASON_SEMANTICS,
                ),
            }
        }
    };
    if let BandSideEvidence::Observation(observation) = side {
        stamp(&mut value, observation);
    }
    value
}

fn stamp(value: &mut KirinSnapshotScalarEvidence, observation: &AttackBandObservation) {
    value.measurement_revision = observation.revision;
    value.requested_start = observation.event.event_sample;
    value.requested_end = observation.requested_end;
    value.actual_start = observation.event.event_sample;
    value.actual_end = observation.actual_end;
    value.finish = observation.finish as u8;
}

pub fn classify_band_lanes(
    target: u8,
    band: AttackBand,
    pre: BandSideEvidence<'_>,
    post: BandSideEvidence<'_>,
    proof: Result<u64, u8>,
) -> [KirinSnapshotScalarEvidence; 4] {
    let period_ms = f64::from(band.resolution_micros()) / 1000.0;
    std::array::from_fn(|lane| {
        let mut value = if target == KIRIN_TARGET_POST || target == KIRIN_TARGET_PRE {
            if lane == DELAY {
                KirinSnapshotScalarEvidence::unavailable(
                    KIRIN_SCALAR_NOT_APPLICABLE,
                    KIRIN_REASON_NO_PAIR,
                )
            } else {
                state(
                    if target == KIRIN_TARGET_POST {
                        post
                    } else {
                        pre
                    },
                    lane,
                )
            }
        } else {
            let a = state(pre, lane);
            let b = state(post, lane);
            // Confirmed inapplicability is first, then confirmed missing facts, then Pending.
            if lane == LEVEL && a.reason == KIRIN_REASON_SILENT && b.reason == KIRIN_REASON_SILENT {
                KirinSnapshotScalarEvidence::unavailable(
                    KIRIN_SCALAR_NOT_APPLICABLE,
                    KIRIN_REASON_BOTH_SILENT,
                )
            } else if a.class == KIRIN_SCALAR_NOT_APPLICABLE
                || b.class == KIRIN_SCALAR_NOT_APPLICABLE
            {
                if a.class == KIRIN_SCALAR_NOT_APPLICABLE {
                    a
                } else {
                    b
                }
            } else if let Err(reason) = proof {
                KirinSnapshotScalarEvidence::unavailable(KIRIN_SCALAR_UNKNOWN, reason)
            } else if a.class == KIRIN_SCALAR_UNKNOWN || b.class == KIRIN_SCALAR_UNKNOWN {
                if a.class == KIRIN_SCALAR_UNKNOWN {
                    a
                } else {
                    b
                }
            } else if a.class == KIRIN_SCALAR_PENDING || b.class == KIRIN_SCALAR_PENDING {
                if a.class == KIRIN_SCALAR_PENDING {
                    a
                } else {
                    b
                }
            } else if let Some(interval) = subtract_intervals(b.interval, a.interval) {
                let mut result = KirinSnapshotScalarEvidence::numeric(
                    interval,
                    if b.reason != KIRIN_REASON_NONE {
                        b.reason
                    } else {
                        a.reason
                    },
                );
                result.measurement_revision = a.measurement_revision.max(b.measurement_revision);
                result.requested_start = a.requested_start;
                result.requested_end = a.requested_end;
                result.actual_start = a.actual_start.max(b.actual_start);
                result.actual_end = a.actual_end.min(b.actual_end);
                result.finish = a.finish.max(b.finish);
                result
            } else {
                KirinSnapshotScalarEvidence::unavailable(
                    KIRIN_SCALAR_UNKNOWN,
                    KIRIN_REASON_SEMANTICS,
                )
            }
        };
        value.proof_revision = if target == KIRIN_TARGET_DELTA {
            proof.unwrap_or(0)
        } else {
            0
        };
        value.resolution = match lane {
            DELAY => (period_ms / 32.0).max(0.2),
            LEVEL => 0.2,
            _ => period_ms,
        };
        value
    })
}

#[cfg(test)]
#[path = "attack_snapshot_classify_tests.rs"]
mod tests;
