use super::*;

/// One sample per bin keeps the window arithmetic readable.
const RATE: u32 = 1_000;

fn event(sample: i64, generation: u64) -> AttackEvent {
    AttackEvent {
        generation,
        sample_rate: RATE,
        channels: 1,
        definition_hash: [3; 32],
        event_sample: sample,
        decision_sample: sample + 40,
        value: 0.5,
    }
}

/// A bin of `frames` samples at a constant amplitude.
fn level_for(frames: i64, amplitude: f32) -> Bin {
    Bin {
        power: f64::from(amplitude).powi(2) * frames as f64,
        peak_sample: amplitude,
        peak_frame: amplitude,
        ..Bin::default()
    }
}

fn level(amplitude: f32) -> Bin {
    level_for(1, amplitude)
}

/// Head at 100..130 and a quieter body after it; `gain` scales every sample.
fn hit_bins(gain: f32, until: i64) -> AttackBins {
    let mut bins = AttackBins::new(RATE, 1);
    bins.begin_run(0, 4, None);
    for index in 0..until {
        let amplitude = match index {
            100..=129 => 0.5,
            _ => 0.05,
        };
        bins.push_level(index, level(amplitude * gain));
    }
    bins
}

#[test]
fn head_body_and_transient_read_the_content_grid() {
    let bins = hit_bins(1.0, 400);
    let body_end = bins.body_end_sample(100, None);
    assert_eq!(body_end, 230);
    let (features, shape) = bins.measure(event(100, 4), body_end).unwrap();
    assert!((features.attack_rms_dbfs + 6.020_6).abs() < 1e-3);
    assert!((features.body_rms_dbfs.unwrap() + 26.020_6).abs() < 1e-3);
    assert!((features.transient_db.unwrap() - 20.0).abs() < 1e-3);
    assert!(features.crest_db.abs() < 1e-3);
    assert_eq!(features.window_start_sample, 100);
    assert!(features.sharpness_acum.is_none());
    assert_eq!(
        (shape.start_sample, shape.event_sample, shape.end_sample),
        (80, 100, 230)
    );
    assert!(features.has_valid_layout());
}

#[test]
fn the_body_stops_at_the_next_onset_and_needs_twenty_ms() {
    let bins = hit_bins(1.0, 400);
    assert_eq!(bins.body_end_sample(100, Some(160)), 160);
    assert_eq!(bins.body_end_sample(100, Some(300)), 230);
    assert_eq!(
        bins.body_end_sample(100, Some(110)),
        130,
        "never inside the head"
    );
    let (cut, _) = bins.measure(event(100, 4), 160).unwrap();
    assert!((cut.transient_db.unwrap() - 20.0).abs() < 1e-3);
    let (short, _) = bins.measure(event(100, 4), 145).unwrap();
    assert_eq!((short.body_rms_dbfs, short.transient_db), (None, None));
    assert!(short.has_valid_layout());
    assert!(bins.measure(event(100, 4), 129).is_none());
    assert!(bins.measure(event(100, 4), 231).is_none());
}

#[test]
fn gain_moves_level_but_not_transient_or_crest() {
    let pre = hit_bins(1.0, 400);
    let post = hit_bins(2.0, 400);
    let (pre, _) = pre.measure(event(100, 4), 230).unwrap();
    let (post, _) = post.measure(event(100, 4), 230).unwrap();
    assert!((post.attack_rms_dbfs - pre.attack_rms_dbfs - 6.020_6).abs() < 1e-3);
    assert!((post.transient_db.unwrap() - pre.transient_db.unwrap()).abs() < 1e-4);
    assert!((post.crest_db - pre.crest_db).abs() < 1e-4);
}

#[test]
fn every_window_bin_must_be_retained_and_current() {
    assert!(hit_bins(1.0, 229).measure(event(100, 4), 230).is_none());
    let bins = hit_bins(1.0, 400);
    assert!(bins.measure(event(100, 5), 230).is_none());
    let mut long = AttackBins::new(RATE, 1);
    long.begin_run(0, 4, None);
    for index in 0..8_000 {
        long.push_level(index, level(0.1));
    }
    assert!(long.measure(event(100, 4), 230).is_none());
    assert!(long.measure(event(7_500, 4), 7_630).is_some());
}

#[test]
fn a_run_that_starts_mid_bin_skips_that_bin() {
    let mut bins = AttackBins::new(48_000, 1);
    bins.begin_run(10, 2, None);
    bins.push_level(0, level_for(48, 1.0));
    for index in 1..=140 {
        bins.push_level(index, level_for(48, 0.1));
    }
    let late = AttackEvent {
        sample_rate: 48_000,
        ..event(48 * 3 + 5, 2)
    };
    let (features, _) = bins.measure(late, 48 * 133).unwrap();
    assert!((features.attack_rms_dbfs + 20.0).abs() < 1e-3);
    let early = AttackEvent {
        sample_rate: 48_000,
        ..event(5, 2)
    };
    assert!(bins.measure(early, 48 * 130).is_none());
    assert!(
        bins.measure(late, 48 * 133 + 1).is_none(),
        "body end off the grid"
    );
}

#[test]
fn sharpness_is_the_loudness_weighted_mean_of_the_hundred_ms_from_the_onset() {
    let mut bins = AttackBins::new(48_000, 1);
    bins.begin_run(0, 7, Some(0));
    for index in 0..200 {
        bins.push_level(index, level_for(48, 0.1));
    }
    let frame = |sample: i64, sharpness: f64, loudness: f64| SharpnessFrame {
        source_sample: sample,
        sharpness: [sharpness, 0.0],
        loudness: [loudness, 0.0],
    };
    let onset = AttackEvent {
        sample_rate: 48_000,
        ..event(48 * 10 + 7, 7)
    };
    let frames = [
        frame(48 * 9, 9.0, 5.0),   // before the window
        frame(48 * 10, 2.0, 1.0),  // inside
        frame(48 * 60, 4.0, 3.0),  // inside
        frame(48 * 70, 8.0, 0.05), // below 0.1 sone: no Sharpness
        frame(48 * 110, 9.0, 5.0), // after the window
    ];
    bins.push_sharpness(&frames, 48 * 109);
    assert!(
        !bins.ready_for(onset.event_sample),
        "frames up to bin 110 are not all in"
    );
    bins.push_sharpness(&[], 48 * 140);
    let (features, _) = bins.measure(onset, 48 * 140).unwrap();
    let expected = (2.0 * 1.0 + 4.0 * 3.0) / (1.0 + 3.0);
    assert!((f64::from(features.sharpness_acum.unwrap()) - expected).abs() < 1e-6);
}

#[test]
fn the_shape_leads_in_twenty_ms_and_reads_missing_lead_in_as_silence() {
    let mut bins = AttackBins::new(RATE, 1);
    bins.begin_run(0, 4, None);
    for index in 0..200 {
        bins.push_level(index, level(if index == 10 { 0.9 } else { 0.0 }));
    }
    let (_, shape) = bins.measure(event(10, 4), 140).unwrap();
    assert_eq!((shape.start_sample, shape.end_sample), (-10, 140));
    let peak = shape.points.iter().copied().fold(0.0, f32::max);
    assert!((peak - 0.9).abs() < 1e-6);
    assert_eq!(shape.points[0], 0.0);
}

fn frame_at(sample: i64, sharpness: f64) -> SharpnessFrame {
    SharpnessFrame {
        source_sample: sample,
        sharpness: [sharpness, 0.0],
        loudness: [1.0, 0.0],
    }
}

#[test]
fn a_frame_ahead_of_its_level_bin_waits_for_it() {
    let mut bins = AttackBins::new(48_000, 1);
    bins.begin_run(0, 7, Some(0));
    for index in 0..10 {
        bins.push_level(index, level_for(48, 0.1));
    }
    // The chunk ended inside bin 10, which is not complete at this flush.
    bins.push_sharpness(
        &[frame_at(48 * 9, 1.0), frame_at(48 * 10, 3.0)],
        48 * 10 + 7,
    );
    for index in 10..200 {
        bins.push_level(index, level_for(48, 0.1));
    }
    bins.push_sharpness(&[], 48 * 200);
    let (features, _) = bins
        .measure(
            AttackEvent {
                sample_rate: 48_000,
                ..event(48 * 10, 7)
            },
            48 * 140,
        )
        .unwrap();
    assert_eq!(features.sharpness_acum, Some(3.0));
    assert!(bins.waiting.is_empty());
}

#[test]
fn sharpness_starts_at_the_first_whole_bin_after_the_epoch() {
    // 44.1 kHz: 44-sample bins, and the first 100 ms chunk starts at 4,410 inside bin 100.
    let mut bins = AttackBins::new(44_100, 1);
    bins.begin_run(4_400, 7, Some(4_410));
    for index in 100..300 {
        bins.push_level(index, level_for(44, 0.1));
    }
    let frames = (0..400)
        .map(|frame| frame_at(4_410 + frame * 22, 2.0))
        .collect::<Vec<_>>();
    bins.push_sharpness(&frames, 44 * 300);
    let at = |bin: i64| {
        let onset = AttackEvent {
            sample_rate: 44_100,
            ..event(44 * bin, 7)
        };
        bins.measure(onset, 44 * (bin + 130))
            .unwrap()
            .0
            .sharpness_acum
    };
    assert_eq!(at(100), None, "bin 100 has no frames before 4,410");
    assert_eq!(at(101), Some(2.0));
}
