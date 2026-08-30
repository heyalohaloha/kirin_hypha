use super::*;
use crate::contract::PeakRule;
use crate::input::MidiNote;

fn label(time_micros: u64, pitches: &[u8]) -> LabelEvent {
    let time_secs = time_micros as f64 / 1_000_000.0;
    let notes = pitches
        .iter()
        .map(|pitch| MidiNote {
            time_secs,
            pitch: *pitch,
            velocity: 100,
        })
        .collect::<Vec<_>>();
    LabelEvent {
        time_micros,
        time_secs,
        kick: pitches.contains(&36),
        hat: pitches.iter().any(|pitch| HAT_NOTES.contains(pitch)),
        pitches: pitches.to_vec(),
        note_count: notes.len(),
        notes,
    }
}

fn frame(event_sample: i64, value: f32) -> TransientOdfFrame {
    TransientOdfFrame {
        support_start_samples: event_sample - 10,
        support_end_samples: event_sample + 10,
        event_sample,
        mel_flux: value,
        complex_odf: 0.0,
        value,
    }
}

#[test]
fn duplicate_predictions_and_merged_labels_are_diagnostic_counts() {
    let (duplicate, _) = score_track(&[0, 10], &[label(0, &[36])], 1_000, 1.0).unwrap();
    assert_eq!(
        (duplicate.tp, duplicate.fp, duplicate.duplicate_fp),
        (1, 1, 1)
    );
    let (merged, _) =
        score_track(&[0], &[label(0, &[36]), label(10_000, &[42])], 1_000, 1.0).unwrap();
    assert_eq!((merged.tp, merged.fn_count, merged.merged_fn), (1, 1, 1));
}

#[test]
fn kick_only_and_hat_only_exclude_mixed_compounds() {
    let labels = [
        label(0, &[36]),
        label(100_000, &[42, 46]),
        label(200_000, &[36, 42]),
        label(300_000, &[36, 38]),
    ];
    let (counts, _) = score_track(&[0, 100, 200, 300], &labels, 1_000, 1.0).unwrap();
    assert_eq!(counts.kick_only_total, 1);
    assert_eq!(counts.hat_only_total, 1);
    assert_eq!(counts.kick_containing_total, 3);
    assert_eq!(counts.hat_containing_total, 2);
}

#[test]
fn refractory_boundary_coalesces_exactly_thirty_milliseconds() {
    let frames = [
        frame(0, 2.0),
        frame(10, 0.0),
        frame(20, 0.0),
        frame(30, 3.0),
        frame(40, 0.0),
    ];
    let rule = PeakRule::LegacyAbsolute {
        threshold: 1.0,
        radius_hops: 1,
        refractory_ms: 30.0,
    };
    assert_eq!(pick_peaks(&frames, 1_000, rule), [30]);
    let mut later = frames;
    later[3].event_sample = 31;
    assert_eq!(pick_peaks(&later, 1_000, rule), [0, 31]);
}

#[test]
fn refractory_equal_values_keep_the_earliest_integer_sample() {
    let frames = [
        frame(0, 2.0),
        frame(10, 0.0),
        frame(1_440, 2.0),
        frame(1_450, 0.0),
        frame(2_881, 2.0),
    ];
    let rule = PeakRule::LegacyAbsolute {
        threshold: 1.0,
        radius_hops: 1,
        refractory_ms: 30.0,
    };
    assert_eq!(pick_peaks(&frames, 48_000, rule), [0, 2_881]);
}

#[test]
fn zero_padded_first_frame_observes_leading_impulse() {
    let mut samples = vec![0.0_f32; 2_048];
    samples[0] = 1.0;
    let frames = analyze_frames(&samples, 44_100, TransientOdfKind::Mel32).unwrap();
    let first = frames.first().unwrap();
    assert_eq!(first.event_sample, 0);
    assert_eq!(
        (first.support_start_samples, first.support_end_samples),
        (-470, 471)
    );
    assert!(first.value > 0.0);
}

#[test]
fn odd_window_flushes_an_eof_impulse_through_its_last_support() {
    assert_eof_impulse_flush(44_100, 941);
}

#[test]
fn even_window_flushes_an_eof_impulse_through_its_last_support() {
    assert_eof_impulse_flush(48_000, 1_024);
}

#[test]
fn local_mean_uses_fixed_zero_padding_at_trace_edges() {
    let frames = [frame(0, 1.0), frame(10, 0.0), frame(20, 0.0)];
    let rule = PeakRule::LocalMean {
        delta: 0.5,
        absolute_floor: 0.1,
        pre_max_hops: 1,
        post_max_hops: 0,
        pre_avg_hops: 1,
        post_avg_hops: 0,
        refractory_ms: 30.0,
    };
    assert_eq!(pick_peaks(&frames, 1_000, rule), [0]);
}

fn assert_eof_impulse_flush(sample_rate: u32, expected_window: usize) {
    let mut samples = vec![0.0_f32; 2_048];
    *samples.last_mut().unwrap() = 1.0;
    let frames = analyze_frames(&samples, sample_rate, TransientOdfKind::Mel32).unwrap();
    let eof = samples.len() as i64;
    let last_sample = eof - 1;

    assert!(frames.iter().any(|frame| {
        frame.support_start_samples <= last_sample
            && last_sample < frame.support_end_samples
            && frame.value > 0.0
    }));
    let last = frames.last().unwrap();
    assert!(last.support_start_samples < eof);
    assert_eq!(
        last.support_end_samples - last.support_start_samples,
        expected_window as i64
    );
    assert!(last.event_sample >= eof);
}
