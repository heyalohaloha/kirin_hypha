use std::path::PathBuf;

use crate::input::{
    FormalSelectionMetadata, LabelEvent, MidiLabels, MidiNote, MonoWav, Selection, WavMetadata,
};

use super::*;

#[test]
fn fixed_superflux_uses_zero_state_warmup_and_eof_flush() {
    let config = SuperFluxConfig::new(
        1_024,
        24,
        1,
        -70,
        kirin_measure::SuperFluxChannelMode::Lr,
        1,
    );
    let mut samples = vec![0.0_f32; 2_048];
    samples[0] = 1.0;
    *samples.last_mut().unwrap() = 0.5;
    let frames = analyze_superflux_frames(&samples, 44_100, config).unwrap();
    assert_eq!(frames.first().unwrap().event_sample, 0);
    assert!(frames.first().unwrap().value > 0.0);
    assert!(frames.iter().any(|frame| {
        frame.support_start_samples <= 2_047
            && frame.support_end_samples > 2_047
            && frame.value > 0.0
    }));
}

#[test]
fn formal_evaluation_stops_until_context_guarded_scoring_exists() {
    let tracks = (0_u8..5).map(track).collect::<Vec<_>>();
    let error = evaluate_formal_tracks(
        &tracks,
        FormalAnalyzer::Mel32V2,
        PeakRule::LocalMean {
            delta: 0.25,
            absolute_floor: 0.0,
            pre_max_hops: 3,
            post_max_hops: 0,
            pre_avg_hops: 12,
            post_avg_hops: 0,
            refractory_ms: 30.0,
        },
    )
    .unwrap_err();
    assert!(error.contains("not_ready_context_guard_unimplemented"));
}

#[test]
fn opened_diagnostic_rows_also_stop_at_the_context_guard_blocker() {
    let mut opened = track(0);
    opened.selection.formal = None;
    let error = evaluate_formal_tracks(
        &[opened],
        FormalAnalyzer::Mel32V2,
        PeakRule::LocalMean {
            delta: 0.25,
            absolute_floor: 0.0,
            pre_max_hops: 3,
            post_max_hops: 0,
            pre_avg_hops: 12,
            post_avg_hops: 0,
            refractory_ms: 30.0,
        },
    )
    .unwrap_err();
    assert!(error.contains("not_ready_context_guard_unimplemented"));
}

fn track(fold: u8) -> LoadedTrack {
    let sample_rate = 44_100;
    let sample_count = 2_048;
    let mut samples = vec![0.0_f32; sample_count];
    samples[882] = 1.0;
    let note = MidiNote {
        time_micros: 20_000,
        time_secs: 0.020,
        pitch: 36,
        velocity: 100,
    };
    LoadedTrack {
        selection: Selection {
            drummer: format!("drummer{fold}"),
            session: format!("session{fold}"),
            id: format!("id{fold}"),
            style: "synthetic".to_string(),
            bpm: 120.0,
            beat_type: "beat".to_string(),
            time_signature: "4-4".to_string(),
            declared_duration: sample_count as f64 / f64::from(sample_rate),
            split: "train".to_string(),
            midi: PathBuf::from(format!("synthetic-{fold}.mid")),
            audio: PathBuf::from(format!("synthetic-{fold}.wav")),
            kit_name: "synthetic-kit".to_string(),
            formal: Some(FormalSelectionMetadata {
                selection_rank: u32::from(fold) + 1,
                selection_key: format!("{fold:064x}"),
                fold,
                expected_midi_sha256: format!("{:064x}", fold + 10),
                declared_excerpt_raw_notes: 1,
                declared_excerpt_compound_events: 1,
                declared_excerpt_kick_only_events: 1,
                declared_excerpt_hat_only_events: 0,
                declared_excerpt_density_events_per_second: 1.0,
                excerpt_start_sample_44100: 0,
                excerpt_end_sample_44100: 1_323,
            }),
        },
        wav: MonoWav {
            metadata: WavMetadata {
                sample_rate,
                channels: 1,
                bits_per_sample: 16,
                sample_count,
                duration_secs: sample_count as f64 / f64::from(sample_rate),
            },
            samples,
        },
        labels: MidiLabels {
            events: vec![LabelEvent {
                time_micros: 20_000,
                time_secs: 0.020,
                kick: true,
                hat: false,
                pitches: vec![36],
                note_count: 1,
                notes: vec![note],
            }],
            raw_note_count: 1,
        },
        midi_relative: format!("synthetic-{fold}.mid"),
        audio_relative: format!("synthetic-{fold}.wav"),
        midi_sha256: format!("{:064x}", fold + 10),
        audio_sha256: format!("{:064x}", fold + 20),
        midi_size_bytes: 1,
        audio_size_bytes: 1,
        peak_abs: 1.0,
    }
}
