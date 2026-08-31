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
        tracker.push_frame(sample, None).unwrap();
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
        tracker
            .push_frame((position == 20) as u8 as f32, None)
            .unwrap();
    }
    let detail = tracker.capture(event(20, 1, 1)).unwrap();
    assert!(detail.features.contrast_floor_limited);
}

#[test]
fn discontinuity_away_from_source_origin_does_not_invent_context() {
    let mut tracker = AttackDetailTracker::new(1_000, 1);
    assert!(tracker.begin_block(500, 2));
    for position in 500..570 {
        tracker
            .push_frame((position == 520) as u8 as f32, None)
            .unwrap();
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
        tracker.push_frame(left, Some(0.0)).unwrap();
    }
    let detail = tracker.capture(event(120, 7, 2)).unwrap();
    assert!((detail.features.attack_rms_dbfs + 9.0309).abs() < 0.001);
}

#[test]
fn generation_and_future_aperture_must_match() {
    let mut tracker = AttackDetailTracker::new(1_000, 1);
    assert!(tracker.begin_block(0, 3));
    for _ in 0..130 {
        tracker.push_frame(0.1, None).unwrap();
    }
    assert_eq!(tracker.capture(event(120, 4, 1)), None);
    assert_eq!(tracker.capture(event(120, 3, 1)), None);
}

#[test]
fn ten_millisecond_waveform_bins_use_real_stereo_power() {
    let mut tracker = AttackDetailTracker::new(1_000, 2);
    assert!(tracker.begin_block(0, 8));
    let mut emitted = None;
    for _ in 0..10 {
        emitted = tracker.push_frame(1.0, Some(0.0)).unwrap().or(emitted);
    }
    let point = emitted.unwrap();
    assert_eq!(point.start_sample, 0);
    assert_eq!(point.end_sample, 10);
    assert!((point.peak_linear - std::f32::consts::FRAC_1_SQRT_2).abs() < 0.000_1);
    assert!((point.rms_dbfs + 3.0103).abs() < 0.001);
}

#[test]
fn detail_shape_preserves_the_event_position_and_peak() {
    let mut tracker = AttackDetailTracker::new(1_000, 1);
    assert!(tracker.begin_block(0, 9));
    for position in 0..180 {
        let sample = if position == 120 { 0.9 } else { 0.0 };
        tracker.push_frame(sample, None).unwrap();
    }
    let detail = tracker.capture(event(120, 9, 1)).unwrap();
    assert_eq!(detail.shape.start_sample, 20);
    assert_eq!(detail.shape.event_sample, 120);
    assert_eq!(detail.shape.end_sample, 150);
    assert!(detail.shape.points.iter().copied().fold(0.0, f32::max) > 0.8);
}

#[test]
fn queued_detail_waits_for_the_exact_sharpness_aperture() {
    const RATE: u32 = 48_000;
    let mut tracker = AttackDetailTracker::new(RATE, 1);
    assert!(tracker.begin_block(0, 11));
    let event = AttackEvent {
        generation: 11,
        sample_rate: RATE,
        channels: 1,
        definition_hash: [9; 32],
        event_sample: 8_000,
        decision_sample: 9_500,
        value: 0.5,
    };
    for position in 0..9_500 {
        let phase = std::f32::consts::TAU * 6_000.0 * position as f32 / RATE as f32;
        tracker.push_frame(phase.sin() * 0.3, None).unwrap();
    }
    tracker.queue_event(event);
    assert!(tracker.capture_next_ready().is_none());
    for position in 9_500..9_600 {
        let phase = std::f32::consts::TAU * 6_000.0 * position as f32 / RATE as f32;
        tracker.push_frame(phase.sin() * 0.3, None).unwrap();
    }
    let detail = tracker.capture_next_ready().unwrap();
    assert!(detail
        .features
        .sharpness_acum
        .is_some_and(|value| value.is_finite() && value > 0.0));
    assert!(detail.has_valid_layout());
}
