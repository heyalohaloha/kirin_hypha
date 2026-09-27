use std::sync::Mutex;

use super::*;

fn event(sample: i64, generation: u64, sample_rate: u32) -> AttackEvent {
    AttackEvent {
        generation,
        sample_rate,
        channels: 1,
        definition_hash: [9; 32],
        event_sample: sample,
        decision_sample: sample + 31,
        value: 0.5,
    }
}

/// 1 kHz keeps one sample per bin; the Phase D model does not run at this rate.
fn tracker_with(samples: impl Fn(i64) -> f32, count: i64, generation: u64) -> AttackDetailTracker {
    let mut tracker = AttackDetailTracker::new(1_000, 1);
    assert!(tracker.begin_block(0, generation));
    for position in 0..count {
        tracker.push_frame(samples(position), None).unwrap();
    }
    tracker
}

fn hit(position: i64) -> f32 {
    if (120..150).contains(&position) {
        0.5
    } else {
        0.05
    }
}

#[test]
fn a_hit_publishes_its_head_then_completes_once_its_body_end_is_decided() {
    let shared = Mutex::new(AttackBins::new(1_000, 1));
    let mut tracker = tracker_with(hit, 400, 4);
    tracker.queue_event(event(120, 4, 1_000));
    let head = tracker.flush(&shared);
    assert_eq!(
        head.len(),
        1,
        "the head is measured before any later onset is decided"
    );
    assert!(!head[0].features.complete);
    assert_eq!(head[0].features.transient_db, None);
    assert!(head[0].has_valid_layout());
    tracker.note_decided_before(249);
    assert!(
        tracker.flush(&shared).is_empty(),
        "the head is published once"
    );
    tracker.note_decided_before(250);
    let details = tracker.flush(&shared);
    assert_eq!(details.len(), 1);
    let features = details[0].features;
    assert!(features.complete);
    assert!((features.transient_db.unwrap() - 20.0).abs() < 1e-3);
    assert_eq!(features.body_end_sample, 250);
    assert_eq!(features.attack_rms_dbfs, head[0].features.attack_rms_dbfs);
    assert!(details[0].has_valid_layout());
}

#[test]
fn a_hit_whose_audio_stops_keeps_its_head() {
    // The transport stops 60 ms after the onset: no more bins or decisions ever arrive.
    let shared = Mutex::new(AttackBins::new(1_000, 1));
    let mut tracker = tracker_with(hit, 180, 4);
    tracker.queue_event(event(120, 4, 1_000));
    tracker.note_decided_before(150);
    let head = tracker.flush(&shared);
    assert_eq!(head.len(), 1);
    assert!(!head[0].features.complete);
    assert!((head[0].features.attack_rms_dbfs + 6.020_6).abs() < 1e-3);
    assert_eq!(
        (head[0].shape.start_sample, head[0].shape.end_sample),
        (100, 180)
    );
    assert!(tracker.flush(&shared).is_empty());
}

#[test]
fn the_body_ends_at_the_next_queued_onset() {
    let shared = Mutex::new(AttackBins::new(1_000, 1));
    let mut tracker = tracker_with(hit, 500, 4);
    tracker.queue_event(event(120, 4, 1_000));
    tracker.queue_event(event(170, 4, 1_000));
    tracker.note_decided_before(400);
    let details = tracker.flush(&shared);
    assert_eq!(details.len(), 2);
    assert_eq!(details[0].features.body_end_sample, 170);
    assert_eq!(details[1].features.body_end_sample, 300);
}

#[test]
fn stereo_frames_keep_their_power_identity() {
    let shared = Mutex::new(AttackBins::new(1_000, 2));
    let mut tracker = AttackDetailTracker::new(1_000, 2);
    assert!(tracker.begin_block(0, 7));
    for position in 0..300 {
        tracker.push_frame(hit(position), Some(0.0)).unwrap();
    }
    tracker.queue_event(AttackEvent {
        channels: 2,
        ..event(120, 7, 1_000)
    });
    tracker.note_decided_before(300);
    let details = tracker.flush(&shared);
    assert!((details[0].features.attack_rms_dbfs + 9.030_9).abs() < 1e-3);
    assert!((details[0].features.sample_peak_dbfs + 6.020_6).abs() < 1e-3);
}

#[test]
fn stale_generations_and_hits_before_the_run_give_no_detail() {
    let shared = Mutex::new(AttackBins::new(1_000, 1));
    let mut tracker = tracker_with(hit, 400, 3);
    tracker.queue_event(event(120, 4, 1_000));
    tracker.note_decided_before(400);
    assert!(tracker.flush(&shared).is_empty());

    let mut late_run = AttackDetailTracker::new(1_000, 1);
    assert!(late_run.begin_block(500, 2));
    for _ in 500..800 {
        late_run.push_frame(0.1, None).unwrap();
    }
    late_run.queue_event(event(480, 2, 1_000));
    late_run.note_decided_before(800);
    assert!(late_run.flush(&shared).is_empty());
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

/// A decaying noise burst: the same audio wherever it is placed.
fn burst(onset: i64, position: i64) -> f32 {
    let offset = position - onset;
    if !(0..9_600).contains(&offset) {
        return 0.0;
    }
    let mut state = (offset as u64)
        .wrapping_mul(6_364_136_223_846_793_005)
        .wrapping_add(1);
    state ^= state >> 29;
    let noise = (state % 20_000) as f32 / 10_000.0 - 1.0;
    noise * 0.6 * (-(offset as f32) / 2_400.0).exp()
}

/// The run starts at 0; Phase D has settled from 14_400 (300 ms).
fn sharpness_at(onset: i64, block: usize) -> Option<f32> {
    const RATE: u32 = 48_000;
    let shared = Mutex::new(AttackBins::new(RATE, 1));
    let mut tracker = AttackDetailTracker::new(RATE, 1);
    assert!(tracker.begin_block(0, 11));
    let end = onset + 12_000;
    let mut position = 0;
    while position < end {
        for _ in 0..block {
            tracker.push_frame(burst(onset, position), None).unwrap();
            position += 1;
        }
        assert!(tracker.flush(&shared).is_empty());
    }
    tracker.queue_event(event(onset, 11, RATE));
    tracker.note_decided_before(end);
    let details = tracker.flush(&shared);
    assert_eq!(details.len(), 1);
    details[0].features.sharpness_acum
}

#[test]
fn per_hit_sharpness_follows_the_onset_not_a_fixed_grid() {
    // The fixed 100 ms grid read 0.6 to 3.0 acum for one snare moved in 10 ms steps. Moves by
    // whole 100 ms read identical Phase D input; other moves differ only by where the 100 ms
    // input chunks split the hit and by the sub-millisecond window start.
    let first = sharpness_at(24_017, 512).unwrap();
    assert!(first > 0.5, "{first}");
    for shift in [4_800, 9_600] {
        let moved = sharpness_at(24_017 + shift, 512).unwrap();
        assert!(
            (moved - first).abs() < 1e-6,
            "{first} vs {moved} at {shift}"
        );
    }
    for shift in [7, 25, 480, 961, 2_400, 4_327] {
        let moved = sharpness_at(24_017 + shift, 512).unwrap();
        assert!(
            (moved - first).abs() < 0.05,
            "{first} vs {moved} at {shift}"
        );
    }
}

#[test]
fn host_block_size_never_changes_a_value() {
    let reference = sharpness_at(24_497, 512);
    assert!(reference.is_some());
    for block in [64, 333, 4_096] {
        assert_eq!(sharpness_at(24_497, block), reference, "block {block}");
    }
}

#[test]
fn a_hit_before_phase_d_settles_has_no_sharpness() {
    // The first hit after a transport start or loop jump would read up to 0.4 acum high.
    assert_eq!(sharpness_at(9_617, 512), None);
    assert_eq!(
        sharpness_at(14_399, 512),
        None,
        "its window starts in bin 299"
    );
    assert!(sharpness_at(14_400, 512).is_some());
}

#[test]
fn a_failed_sharpness_stream_is_rebuilt_for_the_next_run() {
    let mut tracker = AttackDetailTracker::new(48_000, 1);
    assert!(tracker.begin_block(0, 5));
    tracker.sharpness = None;
    assert!(
        tracker.begin_block(0, 5),
        "the same run keeps going without it"
    );
    assert!(tracker.sharpness.is_none());
    assert!(tracker.begin_block(96_000, 5), "a jump starts a new run");
    assert!(tracker.sharpness.is_some());
}
