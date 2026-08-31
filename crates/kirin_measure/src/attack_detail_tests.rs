use super::*;

fn event(sample: i64, generation: u64, channels: u8) -> AttackEvent {
    AttackEvent {
        generation,
        sample_rate: 1_000,
        channels,
        definition_hash: [9; 32],
        event_sample: sample,
        decision_sample: sample + 31,
        value: 0.5,
    }
}

#[test]
fn captures_exact_context_and_attack_after_event_is_decided() {
    let mut tracker = AttackDetailTracker::new(1_000, 1);
    assert!(tracker.begin_block(0, 4));
    for position in 0..180 {
        let sample = if (120..150).contains(&position) {
            0.5
        } else {
            0.05
        };
        assert!(tracker.push_frame(sample, None));
    }
    let detail = tracker.capture(event(120, 4, 1)).unwrap();
    assert!((detail.features.contrast_db - 20.0).abs() < 0.001);
    assert_eq!(detail.features.context_frames, 100);
    assert_eq!(detail.features.attack_frames, 30);
    assert!(detail.has_valid_layout());
}

#[test]
fn source_origin_zero_padding_is_explicit() {
    let mut tracker = AttackDetailTracker::new(1_000, 1);
    assert!(tracker.begin_block(0, 1));
    for position in 0..80 {
        assert!(tracker.push_frame((position == 20) as u8 as f32, None));
    }
    let detail = tracker.capture(event(20, 1, 1)).unwrap();
    assert!(detail.features.contrast_floor_limited);
}

#[test]
fn discontinuity_away_from_source_origin_does_not_invent_context() {
    let mut tracker = AttackDetailTracker::new(1_000, 1);
    assert!(tracker.begin_block(500, 2));
    for position in 500..570 {
        assert!(tracker.push_frame((position == 520) as u8 as f32, None));
    }
    assert_eq!(tracker.capture(event(520, 2, 1)), None);
}

#[test]
fn stereo_frames_keep_their_power_identity() {
    let mut tracker = AttackDetailTracker::new(1_000, 2);
    assert!(tracker.begin_block(0, 7));
    for position in 0..180 {
        let left = if (120..150).contains(&position) {
            0.5
        } else {
            0.05
        };
        assert!(tracker.push_frame(left, Some(0.0)));
    }
    let detail = tracker.capture(event(120, 7, 2)).unwrap();
    assert!((detail.features.attack_rms_dbfs + 9.0309).abs() < 0.001);
}

#[test]
fn generation_and_future_aperture_must_match() {
    let mut tracker = AttackDetailTracker::new(1_000, 1);
    assert!(tracker.begin_block(0, 3));
    for _ in 0..130 {
        assert!(tracker.push_frame(0.1, None));
    }
    assert_eq!(tracker.capture(event(120, 4, 1)), None);
    assert_eq!(tracker.capture(event(120, 3, 1)), None);
}
