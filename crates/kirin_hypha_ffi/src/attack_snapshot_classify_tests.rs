use super::*;
use kirin_measure::attack_perception::band::{AttackBandMeasure, BandEnvelope, BandSpanEnd};
use kirin_measure::AttackEvent;

fn band() -> AttackBand {
    AttackBand::from_index(5).unwrap()
}
fn observation(sound: BandSound, level: f32) -> AttackBandObservation {
    AttackBandObservation {
        event: AttackEvent {
            generation: 1,
            sample_rate: 48_000,
            channels: 2,
            definition_hash: [2; 32],
            event_sample: 10_000,
            decision_sample: 12_048,
            value: 0.5,
        },
        requested_end: 24_400,
        actual_end: 24_400,
        finish: AttackFinish::Full,
        revision: 5,
        measure: Some(AttackBandMeasure {
            band: band(),
            sample_rate: 48_000,
            channels: 2,
            event_sample: 10_000,
            span_end_sample: 24_400,
            span_end: BandSpanEnd::Window,
            peak_frames: 480.0,
            level_dbfs: level,
            sound,
            envelope: BandEnvelope::default(),
        }),
        head_valid: [1; 96],
        tail_valid: [1; 64],
    }
}
fn rises(release: BandRelease) -> BandSound {
    BandSound::Rises {
        arrival: BandArrival::At {
            arrival_frames: 48.0,
            attack_frames: 240.0,
        },
        release,
    }
}

#[test]
fn solo_attack_release_level_are_measured_delay_is_no_pair() {
    let obs = observation(rises(BandRelease::At(4800.0)), -16.5);
    let lanes = classify_band_lanes(
        KIRIN_TARGET_POST,
        band(),
        BandSideEvidence::Missing(KIRIN_REASON_NO_PAIR),
        BandSideEvidence::Observation(&obs),
        Err(KIRIN_REASON_NO_PAIR),
    );
    assert_eq!(lanes[DELAY].class, KIRIN_SCALAR_NOT_APPLICABLE);
    assert_eq!(lanes[DELAY].reason, KIRIN_REASON_NO_PAIR);
    assert_eq!(lanes[ATT].interval.lower.value, 5.0);
    assert_eq!(lanes[REL].interval.lower.value, 100.0);
    assert_eq!(lanes[LEVEL].interval.lower.value, -16.5);
    assert!(lanes.iter().all(KirinSnapshotScalarEvidence::is_valid));
}

#[test]
fn silent_floor_is_bound_both_silent_delta_is_not_applicable() {
    let silent = observation(BandSound::Silent, -120.0);
    let solo = classify_band_lanes(
        0,
        band(),
        BandSideEvidence::Missing(1),
        BandSideEvidence::Observation(&silent),
        Err(1),
    );
    assert_eq!(solo[ATT].class, KIRIN_SCALAR_NOT_APPLICABLE);
    assert_eq!(solo[REL].class, KIRIN_SCALAR_NOT_APPLICABLE);
    assert_eq!(solo[LEVEL].class, KIRIN_SCALAR_BOUND);
    assert_eq!(solo[LEVEL].interval.upper.value, -72.0);
    let pair = classify_band_lanes(
        1,
        band(),
        BandSideEvidence::Observation(&silent),
        BandSideEvidence::Observation(&silent),
        Ok(17),
    );
    assert_eq!(pair[LEVEL].class, KIRIN_SCALAR_NOT_APPLICABLE);
    assert_eq!(pair[LEVEL].reason, KIRIN_REASON_BOTH_SILENT);
    let rise = observation(rises(BandRelease::At(4800.0)), -20.0);
    let pair = classify_band_lanes(
        1,
        band(),
        BandSideEvidence::Observation(&silent),
        BandSideEvidence::Observation(&rise),
        Ok(17),
    );
    assert_eq!(pair[LEVEL].interval.lower.value, 52.0);
    assert_eq!(
        pair[LEVEL].interval.upper,
        KirinSnapshotEndpoint::positive_infinity()
    );
    let inverse = classify_band_lanes(
        KIRIN_TARGET_DELTA,
        band(),
        BandSideEvidence::Observation(&rise),
        BandSideEvidence::Observation(&silent),
        Ok(17),
    );
    assert_eq!(inverse[LEVEL].interval.upper.value, -52.0);
    assert_eq!(
        inverse[LEVEL].interval.lower,
        KirinSnapshotEndpoint::negative_infinity()
    );
}

#[test]
fn unknown_and_na_have_priority_over_pending() {
    let pending = BandSideEvidence::Pending(KIRIN_REASON_WAITING_SERVICE);
    let missing = BandSideEvidence::Missing(KIRIN_REASON_NOT_KEPT);
    let unknown = classify_band_lanes(1, band(), missing, pending, Ok(1));
    assert!(unknown
        .iter()
        .all(|e| e.class == KIRIN_SCALAR_UNKNOWN && e.reason == KIRIN_REASON_NOT_KEPT));
    let silent = observation(BandSound::Silent, -120.0);
    let na = classify_band_lanes(
        1,
        band(),
        BandSideEvidence::Observation(&silent),
        pending,
        Err(KIRIN_REASON_MAPPING),
    );
    assert_eq!(na[ATT].class, KIRIN_SCALAR_NOT_APPLICABLE);
    assert_eq!(na[LEVEL].class, KIRIN_SCALAR_UNKNOWN);
}

#[test]
fn next_hit_is_unknown_and_release_lower_bounds_subtract_to_all_real() {
    let mut next = observation(rises(BandRelease::CutByNextHit), -20.0);
    next.measure.as_mut().unwrap().span_end = BandSpanEnd::NextHit;
    let bound = observation(rises(BandRelease::AtLeast(4800.0)), -20.0);
    let solo = classify_band_lanes(
        0,
        band(),
        BandSideEvidence::Missing(1),
        BandSideEvidence::Observation(&next),
        Err(1),
    );
    assert_eq!(solo[REL].class, KIRIN_SCALAR_UNKNOWN);
    assert_eq!(solo[REL].reason, KIRIN_REASON_NEXT_HIT);
    let pair = classify_band_lanes(
        1,
        band(),
        BandSideEvidence::Observation(&bound),
        BandSideEvidence::Observation(&bound),
        Ok(1),
    );
    assert_eq!(pair[REL].class, KIRIN_SCALAR_BOUND);
    assert!(pair[REL].interval.is_all_real());
}

#[test]
fn hidden_arrival_does_not_hide_valid_release_and_level() {
    let ringing = observation(
        BandSound::Rises {
            arrival: BandArrival::Ringing,
            release: BandRelease::At(4800.0),
        },
        -20.0,
    );
    let solo = classify_band_lanes(
        0,
        band(),
        BandSideEvidence::Missing(1),
        BandSideEvidence::Observation(&ringing),
        Err(1),
    );
    assert_eq!(solo[ATT].class, KIRIN_SCALAR_NOT_APPLICABLE);
    assert_eq!(solo[REL].class, KIRIN_SCALAR_EXACT);
    assert_eq!(solo[LEVEL].class, KIRIN_SCALAR_EXACT);
}

#[test]
fn canonical_d8_each_arrival_is_subtracted_first_and_rel_is_peak_relative() {
    let mut deltas = Vec::new();
    for (pre_ms, post_ms) in [(0.0, 10.0), (10.0, 0.0), (11.0, 12.0)] {
        let make = |arrival_ms: f32, peak_ms: f32, release_ms: f32| {
            let mut value = observation(
                BandSound::Rises {
                    arrival: BandArrival::At {
                        arrival_frames: arrival_ms * 48.0,
                        attack_frames: 240.0,
                    },
                    release: BandRelease::At(release_ms * 48.0),
                },
                -20.0,
            );
            value.measure.as_mut().unwrap().peak_frames = peak_ms * 48.0;
            value
        };
        let pre = make(pre_ms, pre_ms + 5.0, 100.0);
        let post = make(post_ms, post_ms + 5.0, 105.0);
        let lanes = classify_band_lanes(
            KIRIN_TARGET_DELTA,
            band(),
            BandSideEvidence::Observation(&pre),
            BandSideEvidence::Observation(&post),
            Ok(7),
        );
        assert_eq!(lanes[REL].interval.lower.value, 5.0);
        deltas.push(lanes[DELAY].interval);
    }
    assert_eq!(
        crate::snapshot_interval::median_interval(&deltas),
        Some(KirinSnapshotInterval::point(
            1.0,
            KIRIN_INTERVAL_MILLISECONDS
        ))
    );
}
