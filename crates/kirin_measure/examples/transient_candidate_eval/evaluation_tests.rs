use super::*;
use crate::contract::PeakRule;
use crate::input::MidiNote;

fn label(time_secs: f64, pitches: &[u8]) -> LabelEvent {
    let notes = pitches
        .iter()
        .map(|pitch| MidiNote {
            time_secs,
            pitch: *pitch,
            velocity: 100,
        })
        .collect::<Vec<_>>();
    LabelEvent {
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
    let (duplicate, _) = score_track(&[0.0, 0.010], &[label(0.0, &[36])], 1.0);
    assert_eq!(
        (duplicate.tp, duplicate.fp, duplicate.duplicate_fp),
        (1, 1, 1)
    );
    let (merged, _) = score_track(&[0.0], &[label(0.0, &[36]), label(0.010, &[42])], 1.0);
    assert_eq!((merged.tp, merged.fn_count, merged.merged_fn), (1, 1, 1));
}

#[test]
fn kick_only_and_hat_only_exclude_mixed_compounds() {
    let labels = [
        label(0.0, &[36]),
        label(0.1, &[42, 46]),
        label(0.2, &[36, 42]),
        label(0.3, &[36, 38]),
    ];
    let (counts, _) = score_track(&[0.0, 0.1, 0.2, 0.3], &labels, 1.0);
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
    assert_eq!(pick_peaks(&frames, 1_000, rule), [0.030]);
    let mut later = frames;
    later[3].event_sample = 31;
    assert_eq!(pick_peaks(&later, 1_000, rule), [0.0, 0.031]);
}

#[test]
fn zero_padded_first_frame_observes_leading_impulse() {
    let mut samples = vec![0.0_f32; 2_048];
    samples[0] = 1.0;
    let frames = analyze_frames(&samples, 44_100, TransientOdfKind::Mel32).unwrap();
    assert_eq!(frames.first().unwrap().event_sample, 0);
    assert!(frames.first().unwrap().value > 0.0);
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
    assert_eq!(pick_peaks(&frames, 1_000, rule), [0.0]);
}
