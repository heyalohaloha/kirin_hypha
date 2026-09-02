use super::*;
use crate::plugin_data::{BounceTake, Role};

fn frame(t_ms: u64, value: f64) -> Frame {
    Frame {
        t_ms,
        n_prime: None,
        n_prime_total: None,
        sharpness: None,
        lufs_m: value,
        true_peak: value,
        crest: value.abs(),
        psr: None,
    }
}

fn observation(t_ms: u64, raw: i64, output: u32, epoch: u64) -> TraceClockObservation {
    TraceClockObservation {
        frame: frame(t_ms, t_ms as f64),
        producer_position_samples: raw.checked_sub(i64::from(output)),
        raw_host_position_samples: Some(raw),
        capture_epoch: Some(epoch),
        clock_source: Some("project_timeline".to_string()),
        presentation_latency_source: Some("vst3".to_string()),
        input_presentation_latency_samples: None,
        output_presentation_latency_samples: Some(output),
    }
}

fn data(observations: Vec<TraceClockObservation>) -> PluginDataFile {
    let mut data = PluginDataFile::new(
        "i".into(),
        "p".into(),
        "x".into(),
        Role::Pre,
        None,
        48_000,
        None,
        None,
        None,
        None,
    );
    data.trace_clock_observations = observations;
    data.raw_host_clock_range = Some(crate::plugin_data::HostClockRange {
        start_position_samples: 100_256,
        end_position_samples: 110_112,
    });
    data.bounce_take = Some(BounceTake {
        source: "render_clock_native".into(),
        time_axis: "native_samples".into(),
        alignment_status: "sample_count_ready".into(),
        sample_rate: 48_000,
        wav_start_sample: 0,
        wav_end_sample: 9_600,
        duration_samples: 9_600,
        duration_frames_48k: 9_600,
        start_t_ms: 0,
        end_t_ms: 200,
        trace_sample_count: 2,
        frame_count: 2,
        // Current producers store the output-presentation range here. The raw host range is
        // retained independently above and may have a different span when latency changes.
        host_start_position_samples: Some(100_000),
        host_end_position_samples: Some(109_600),
    });
    data
}

fn expected(start: Option<u64>) -> ExpectedWavMetadata {
    ExpectedWavMetadata {
        expected_duration_samples: 9_600,
        expected_sample_rate: 48_000,
        wav_time_reference_samples: start,
        wav_path: "/tmp/x.wav".into(),
        bounce_id: "b".into(),
        created_at_ms: 1,
        wav_file_size: Some(1),
        wav_mtime_ms: 1,
        wav_hash: Some("h".into()),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    }
}

#[test]
fn output_latency_changes_are_resolved_per_observation_across_epochs() {
    let data = data(vec![
        observation(100, 104_800, 256, 10),
        observation(200, 109_600, 512, 11),
    ]);
    let resolved = resolve_exact_side(&data, &expected(Some(100_000)), 2, 4_800)
        .expect("piecewise output latency");
    assert_eq!(resolved.model, "producer_plus_output_latency");
    assert_eq!(resolved.producer_slots, vec![104_800, 109_600]);
    assert_eq!(resolved.wav_slots, vec![4_800, 9_600]);
    assert_eq!(resolved.origin_position_samples, 100_000);
    assert_eq!(resolved.frames.len(), 2);
}

#[test]
fn unknown_latency_source_falls_back_without_erasing_observations() {
    let mut first = observation(100, 104_800, 256, 10);
    let mut second = observation(200, 109_600, 512, 11);
    first.presentation_latency_source = Some("au".to_string());
    second.presentation_latency_source = None;
    let data = data(vec![first, second]);

    let resolved = resolve_exact_side(&data, &expected(Some(100_000)), 2, 4_800)
        .expect("raw host fallback preserves both factual observations");

    assert_eq!(resolved.model, "raw_host_position");
    assert_eq!(resolved.frames.len(), 2);
    assert_eq!(resolved.wav_slots, vec![4_800, 9_600]);
    assert!(resolved.comparison_slots.is_empty());
}

#[test]
fn missing_bext_uses_presentation_origin_without_applying_latency_twice() {
    let data = data(vec![
        observation(100, 104_800, 256, 10),
        observation(200, 109_600, 512, 11),
    ]);
    let mut data = data;
    data.raw_host_clock_range = Some(crate::plugin_data::HostClockRange {
        start_position_samples: 100_000,
        end_position_samples: 109_600,
    });
    let resolved = resolve_exact_side(&data, &expected(None), 2, 4_800)
        .expect("producer presentation range resolves without BWF");
    assert_eq!(resolved.model, "producer_plus_output_latency");
    assert_eq!(resolved.producer_slots, vec![104_800, 109_600]);
}

#[test]
fn missing_bext_raw_minus_output_uses_the_same_presentation_origin() {
    let mut first = observation(100, 105_056, 256, 10);
    let mut second = observation(200, 110_112, 512, 11);
    first.producer_position_samples = None;
    second.producer_position_samples = None;
    let resolved = resolve_exact_side(&data(vec![first, second]), &expected(None), 2, 4_800)
        .expect("raw-minus-output presentation range resolves without BWF");
    assert_eq!(resolved.model, "raw_minus_output_latency");
    assert_eq!(resolved.producer_slots, vec![104_800, 109_600]);
}

#[test]
fn missing_bext_raw_host_model_uses_the_independent_raw_origin() {
    let mut first = observation(100, 104_800, 256, 10);
    let mut second = observation(200, 109_600, 512, 11);
    first.producer_position_samples = None;
    second.producer_position_samples = None;
    let mut data = data(vec![first, second]);
    data.raw_host_clock_range = Some(crate::plugin_data::HostClockRange {
        start_position_samples: 100_000,
        end_position_samples: 109_600,
    });
    let resolved = resolve_exact_side(&data, &expected(None), 2, 4_800)
        .expect("raw host range resolves without BWF");
    assert_eq!(resolved.model, "raw_host_position");
    assert_eq!(resolved.producer_slots, vec![104_800, 109_600]);
}

#[test]
fn missing_bext_raw_plus_output_uses_raw_origin_and_first_latency() {
    let mut first = observation(100, 104_544, 256, 10);
    let mut second = observation(200, 109_088, 512, 11);
    first.producer_position_samples = None;
    second.producer_position_samples = None;
    let mut data = data(vec![first, second]);
    data.raw_host_clock_range = Some(crate::plugin_data::HostClockRange {
        start_position_samples: 99_744,
        end_position_samples: 109_088,
    });
    let resolved = resolve_exact_side(&data, &expected(None), 2, 4_800)
        .expect("raw-plus-output range resolves without BWF");
    assert_eq!(resolved.model, "raw_plus_output_latency");
    assert_eq!(resolved.producer_slots, vec![104_800, 109_600]);
}

#[test]
fn raw_plus_output_supports_a_host_with_opposite_project_clock_semantics() {
    let mut first = observation(100, 104_544, 256, 1);
    let mut second = observation(200, 109_344, 256, 1);
    first.producer_position_samples = None;
    second.producer_position_samples = None;
    let resolved = resolve_exact_side(
        &data(vec![first, second]),
        &expected(Some(100_000)),
        2,
        4_800,
    )
    .expect("opposite host semantics");
    assert_eq!(resolved.model, "raw_plus_output_latency");
}

#[test]
fn raw_minus_output_supports_a_host_reporting_pre_presentation_positions() {
    let mut first = observation(100, 105_056, 256, 1);
    let mut second = observation(200, 109_856, 256, 1);
    first.producer_position_samples = None;
    second.producer_position_samples = None;
    let resolved = resolve_exact_side(
        &data(vec![first, second]),
        &expected(Some(100_000)),
        2,
        4_800,
    )
    .expect("pre-presentation host semantics");
    assert_eq!(resolved.model, "raw_minus_output_latency");
}

#[test]
fn chronological_fallback_keeps_later_latency_epochs_visible() {
    let data = data(vec![observation(100, 1, 0, 1), observation(200, 2, 64, 2)]);
    let frames = chronological_fallback_frames(&data, 2);
    assert_eq!(frames.len(), 2);
    assert_eq!(frames[0].t_ms, 100);
    assert_eq!(frames[1].t_ms, 200);
}

#[test]
fn chronological_fallback_keeps_complete_baked_take_when_journal_has_extra_epochs() {
    let mut data = data(vec![
        observation(100, 104_800, 512, 1),
        observation(100, 104_800, 512, 2),
        observation(200, 109_600, 512, 2),
    ]);
    data.frames = vec![frame(100, -31.0), frame(200, -29.0)];

    let frames = chronological_fallback_frames(&data, 2);

    assert_eq!(frames.len(), 2);
    assert_eq!(frames[0].lufs_m, -31.0);
    assert_eq!(frames[1].lufs_m, -29.0);
}

#[test]
fn anchored_resolution_keeps_a_missing_middle_slot_absent() {
    let mut expected = expected(Some(100_000));
    expected.expected_duration_samples = 14_400;
    let resolved = resolve_exact_side(
        &data(vec![
            observation(100, 104_800, 256, 10),
            observation(300, 114_400, 256, 10),
        ]),
        &expected,
        3,
        4_800,
    )
    .expect("two factual anchored slots");

    assert_eq!(resolved.wav_slots, vec![4_800, 14_400]);
    assert_eq!(
        resolved
            .frames
            .iter()
            .map(|frame| frame.t_ms)
            .collect::<Vec<_>>(),
        vec![100, 300]
    );
}

#[test]
fn large_pdc_preserves_pre_origin_content_observations_and_exact_clock() {
    const ORIGIN: i64 = 10_080_000;
    const SAMPLE_RATE: u32 = 96_000;
    const SLOT_SAMPLES: i64 = 9_600;
    const OUTPUT_LATENCY: u32 = 52_722;
    const DURATION: u64 = 4_500_000;
    const EXPECTED_FRAMES: usize = 468;

    let observations = (0..474)
        .map(|index| {
            let comparison_relative = -48_000 + i64::from(index) * SLOT_SAMPLES;
            let producer = ORIGIN + comparison_relative;
            let aligned = producer + i64::from(OUTPUT_LATENCY);
            TraceClockObservation {
                frame: frame((index as u64 + 1) * 100, index as f64),
                producer_position_samples: Some(producer),
                raw_host_position_samples: Some(aligned),
                capture_epoch: Some(1),
                clock_source: Some("project_timeline".to_string()),
                presentation_latency_source: Some("audio_unit_v2".to_string()),
                input_presentation_latency_samples: None,
                output_presentation_latency_samples: Some(OUTPUT_LATENCY),
            }
        })
        .collect::<Vec<_>>();
    let mut data = PluginDataFile::new(
        "music-pre".into(),
        "project".into(),
        "session".into(),
        Role::Pre,
        None,
        SAMPLE_RATE,
        None,
        None,
        None,
        None,
    );
    data.trace_clock_observations = observations;
    let expected = ExpectedWavMetadata {
        expected_duration_samples: DURATION,
        expected_sample_rate: SAMPLE_RATE,
        wav_time_reference_samples: Some(ORIGIN as u64),
        wav_path: "/tmp/music.wav".into(),
        bounce_id: "music-bounce".into(),
        created_at_ms: 1,
        wav_file_size: Some(1),
        wav_mtime_ms: 1,
        wav_hash: Some("music-hash".into()),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    };

    let resolved = resolve_exact_side(&data, &expected, EXPECTED_FRAMES, SLOT_SAMPLES)
        .expect("later in-WAV producer observations remain exact");

    assert_eq!(resolved.model, "producer_plus_output_latency");
    assert_eq!(resolved.comparison_slots.len(), EXPECTED_FRAMES);
    assert_eq!(resolved.comparison_slots.first(), Some(&-38_400));
    assert_eq!(resolved.comparison_slots.last(), Some(&4_444_800));
    assert_eq!(resolved.wav_slots.len(), EXPECTED_FRAMES);
    assert_eq!(resolved.wav_slots.first(), Some(&14_322));
    assert_eq!(resolved.wav_slots.last(), Some(&4_497_522));
    assert_eq!(resolved.raw_host_slots, resolved.producer_slots);
    assert_eq!(resolved.frames.len(), EXPECTED_FRAMES);
    assert_eq!(resolved.frames.first().map(|frame| frame.lufs_m), Some(1.0));
    assert_eq!(
        resolved.frames.last().map(|frame| frame.lufs_m),
        Some(468.0)
    );
}

#[test]
fn overflow_never_creates_a_false_clock_match() {
    let data = data(vec![observation(100, i64::MAX, 0, 1)]);
    assert!(resolve_exact_side(&data, &expected(Some(u64::MAX)), 1, 4_800).is_none());
}

#[test]
fn long_generation_selection_advances_through_candidates_once() {
    const FRAME_COUNT: usize = 6_000;
    let observations = (1..=FRAME_COUNT)
        .map(|index| {
            let position = i64::try_from(index).unwrap() * 4_800;
            observation(index as u64 * 100, position, 0, 1)
        })
        .collect::<Vec<_>>();
    let (frames, producer_slots, wav_slots) =
        select_sparse_monotonic_frames(&observations, 0, FRAME_COUNT, 4_800, ClockModel::Producer)
            .expect("ten-minute generation");
    assert_eq!(frames.len(), FRAME_COUNT);
    assert_eq!(frames.last().map(|frame| frame.t_ms), Some(600_000));
    assert_eq!(producer_slots.last(), Some(&28_800_000));
    assert_eq!(wav_slots.last(), Some(&28_800_000));
}
