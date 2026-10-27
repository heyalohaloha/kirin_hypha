//! Quiet, measured slots must survive closing a PRE/POST take.
use super::tests::{expected_wav_metadata, make_ctx_with_sample_rate};
use super::*;
use crate::channel_layout::ChannelLayout;
use crate::engine::MeasureEngine;

const RATE: u32 = 48_000;
const STEP: u64 = 4_800;

fn tone(peak: f64) -> Vec<f64> {
    (0..STEP)
        .flat_map(|frame| {
            let value = peak * (std::f64::consts::TAU * 20.0 * frame as f64 / RATE as f64).sin();
            [value, value]
        })
        .collect()
}

fn observations() -> (Vec<RecordTraceSample>, f64) {
    let mut engine = MeasureEngine::new(RATE, ChannelLayout::stereo()).unwrap();
    // Fill the measurement window before this take. Readiness is tested separately below.
    engine.push(&vec![0.0; STEP as usize * 8]);
    let mut samples = Vec::new();
    let mut measured_floor_tp = None;
    for slot in 1..=120 {
        let peak = match slot {
            1..=40 => 5.9e-6,
            41..=80 => 2.0e-5,
            _ => 5.6e-5,
        };
        let pcm = tone(peak);
        engine.push_observed(&pcm, |_, result, chunk| {
            if slot == 80 {
                assert!(
                    result.lufs_m.is_none(),
                    "middle section must exercise the LUFS floor"
                );
                assert!(!samples_are_trace_silence_floor(chunk));
                measured_floor_tp = result.true_peak;
            }
            if slot == 120 {
                assert!(
                    result.lufs_m.is_some(),
                    "last section must have measured loudness"
                );
            }
            let mut measured = result.clone();
            // Independent perceptual lane: only its already measured fields enter this fixture.
            measured.n_prime = Some([0.0; 20]);
            measured.sharpness = Some(0.0);
            let mut sample = RecordTraceSample::measured_observed(
                1,
                slot * 100,
                slot * STEP,
                Some(slot * STEP),
                measured,
                samples_are_trace_silence_floor(chunk),
                false,
            );
            sample.position_samples = Some((slot * STEP) as i64);
            sample.raw_host_position_samples = sample.position_samples;
            sample.capture_epoch = Some(1);
            sample.clock_source = CaptureClockSource::ProjectTimeline;
            sample.presentation_latency = PresentationLatencySamples {
                source: crate::record_take::PresentationLatencySource::Vst3,
                input: Some(0),
                output: Some(0),
            };
            samples.push(sample);
        });
    }
    (
        samples,
        measured_floor_tp.expect("floor must have a measured peak"),
    )
}

fn read_records(dir: &Path, records: &mut Vec<(PathBuf, PluginDataFile)>) {
    for entry in fs::read_dir(dir).unwrap() {
        let path = entry.unwrap().path();
        if path.is_dir() {
            read_records(&path, records);
        } else if path.extension().is_some_and(|ext| ext == "json") {
            if let Ok(data) = serde_json::from_slice::<PluginDataFile>(&fs::read(&path).unwrap()) {
                records.push((path, data));
            }
        }
    }
}

#[test]
fn quiet_floor_take_keeps_every_slot_and_commits_pair() {
    let base = tempfile::tempdir().unwrap();
    assert!(base.path().starts_with(std::env::temp_dir()));
    let (samples, measured_floor_tp) = observations();
    assert!(measured_floor_tp.is_finite());
    assert!((measured_floor_tp + 94.0).abs() < 0.2);
    let mut expected = expected_wav_metadata(RATE, 120 * STEP, "quiet-floor-take");
    expected.wav_time_reference_samples = Some(0);
    for (role, id, pre, post) in [
        (Role::Pre, "quiet-pre", None, Some("quiet-post".to_string())),
        (
            Role::Post,
            "quiet-post",
            Some("quiet-pre".to_string()),
            None,
        ),
    ] {
        let start = expected.created_at_ms - 12_000;
        let mut ctx = make_ctx_with_sample_rate(base.path(), role, start, RATE);
        let paths = WriterPaths::build(
            base.path(),
            "quiet-project",
            id,
            role,
            &epoch_ms_to_iso8601(start),
        );
        // Remove only the helper's initial placeholder, inside this test's TempDir.
        fs::remove_file(&ctx.staging_path).unwrap();
        ctx.final_path = paths.final_path.clone();
        ctx.staging_path = paths.staging_path.clone();
        ctx.failed_path = paths.failed_path.clone();
        ctx.writer = PluginDataWriter::create(
            paths,
            "quiet-install".into(),
            "quiet-project".into(),
            id.into(),
            role,
            None,
            RATE,
            pre,
            post,
            None,
            None,
        )
        .unwrap();
        ctx.writer
            .set_record_start_wall_clock(epoch_ms_to_iso8601(start));
        ctx.writer.set_started_at_ms(start);
        ctx.writer
            .set_record_session_id(Some("quiet-floor-take".into()));
        ctx.writer.set_expected_wav(Some(expected.clone()));
        ctx.record_generation = 1;
        ctx.selected_capture_epoch = Some(1);
        ctx.presentation_range = Some((0, (120 * STEP) as i64));
        ctx.clean_take = Some(RecordTakeSnapshot {
            generation: 1,
            duration_samples: 120 * STEP,
            source: RECORD_TAKE_SOURCE_WAV_CLOCK,
            host_start_position_samples: Some(0),
            host_end_position_samples: Some((120 * STEP) as i64),
        });
        ctx.trace_samples = samples.clone();
        ctx.trace_sample_count = samples.len();
        assert!(writer_close_result(ctx));
    }
    let mut records = Vec::new();
    read_records(base.path(), &mut records);
    for role in [Role::Pre, Role::Post] {
        let (_, data) = records.iter().find(|(_, data)| data.role == role).unwrap();
        eprintln!(
            "{role:?}: frames={} diagnostics={:?} quality={:?} commit={:?}",
            data.frames.len(),
            data.trace_diagnostics,
            data.record_quality,
            data.commit_status
        );
        assert_eq!(
            data.frames.len(),
            120,
            "{role:?}: {:?}",
            data.trace_diagnostics
        );
        assert!(
            data.record_quality.as_ref().unwrap().complete,
            "{:?}",
            data.record_quality
        );
        assert_eq!(
            data.commit_status.as_deref(),
            Some("committed"),
            "{:?}",
            data.integrity_reasons
        );
        assert!(verify_checksum(data));
        for (index, frame) in data.frames.iter().enumerate() {
            assert_eq!(frame.t_ms, (index as u64 + 1) * 100);
        }
        let floor = &data.frames[79];
        let expected_tp = (measured_floor_tp * 10.0).round() / 10.0;
        eprintln!(
            "{role:?}: floor M={} TP={} Crest={} measured TP={measured_floor_tp}",
            floor.lufs_m, floor.true_peak, floor.crest
        );
        assert_eq!((floor.lufs_m, floor.crest), (-100.0, 0.0));
        assert_eq!(
            floor.true_peak, expected_tp,
            "the measured finite peak must survive persistence"
        );
        assert_ne!(floor.true_peak, TRACE_SILENCE_TRUE_PEAK_DBTP);
        let silence = &data.frames[39];
        assert_eq!(
            (silence.lufs_m, silence.true_peak, silence.crest),
            (-100.0, -100.0, 0.0)
        );
    }
    assert!(records.iter().all(|(path, _)| !path
        .components()
        .any(|part| part.as_os_str() == ".pair_pending")));
}

#[test]
fn quiet_floor_does_not_fill_an_unready_momentary_window() {
    let mut engine = MeasureEngine::new(RATE, ChannelLayout::stereo()).unwrap();
    for slot in 1..=3 {
        engine.push_observed(&tone(2.0e-5), |_, result, chunk| {
            assert!(result.lufs_m.is_none());
            assert!(!result.momentary_silence_floor);
            let sample = RecordTraceSample::measured_observed(
                1,
                slot * 100,
                slot * STEP,
                Some(slot * STEP),
                result.clone(),
                samples_are_trace_silence_floor(chunk),
                false,
            );
            assert!(!sample.has_measured_core());
            assert!(measured_trace_result_for_bake(&sample).is_none());
        });
    }
    let ready = engine.push(&tone(2.0e-5)).unwrap();
    assert!(ready.momentary_silence_floor);
    assert!(ready.lufs_m.is_none());
    engine.reset();
    assert!(!engine.push(&tone(2.0e-5)).unwrap().momentary_silence_floor);
}

#[test]
fn quiet_floor_provenance_never_repairs_nonfinite_core() {
    for invalid in [f64::NAN, f64::INFINITY, f64::NEG_INFINITY] {
        for field in 0..3 {
            let mut result = MeasureResult {
                momentary_silence_floor: true,
                ..Default::default()
            };
            match field {
                0 => result.lufs_m = Some(invalid),
                1 => result.true_peak = Some(invalid),
                _ => result.crest = Some(invalid),
            }
            let sample = RecordTraceSample::measured_observed(
                1,
                100,
                STEP,
                Some(STEP),
                result,
                false,
                false,
            );
            assert!(!sample.has_measured_core());
            assert!(measured_trace_result_for_bake(&sample).is_none());
        }
    }
}

#[test]
fn quiet_floor_conversion_preserves_other_measurements() {
    let input = MeasureResult {
        momentary_silence_floor: true,
        lufs_s: Some(-25.0),
        tp_session_max: Some(-1.0),
        psr: Some(7.0),
        n_prime: Some([0.25; 20]),
        sharpness: Some(0.3),
        ..Default::default()
    };
    let sample =
        RecordTraceSample::measured_observed(1, 100, STEP, Some(STEP), input.clone(), false, false);
    assert!(sample.has_measured_core());
    assert_eq!(sample.result.lufs_s, input.lufs_s);
    assert_eq!(sample.result.tp_session_max, input.tp_session_max);
    assert_eq!(sample.result.psr, input.psr);
    assert_eq!(sample.result.n_prime, input.n_prime);
    assert_eq!(sample.result.sharpness, input.sharpness);
}

#[test]
fn quiet_floor_keeps_finite_core_and_fills_only_missing_values() {
    for (tp, crest) in [(None, None), (Some(-94.0), Some(1.5))] {
        let result = MeasureResult {
            momentary_silence_floor: true,
            true_peak: tp,
            crest,
            ..Default::default()
        };
        let sample = RecordTraceSample::measured_observed(
            1,
            100,
            STEP,
            Some(STEP),
            result.clone(),
            false,
            false,
        );
        assert_eq!(sample.result.lufs_m, Some(TRACE_SILENCE_LUFS));
        assert_eq!(
            sample.result.true_peak,
            Some(tp.unwrap_or(TRACE_SILENCE_TRUE_PEAK_DBTP))
        );
        assert_eq!(
            sample.result.crest,
            Some(crest.unwrap_or(TRACE_SILENCE_CREST_DB))
        );
        let silence =
            RecordTraceSample::measured_observed(1, 100, STEP, Some(STEP), result, true, false);
        assert_eq!(silence.result.lufs_m, Some(TRACE_SILENCE_LUFS));
        assert_eq!(silence.result.true_peak, Some(TRACE_SILENCE_TRUE_PEAK_DBTP));
        assert_eq!(silence.result.crest, Some(TRACE_SILENCE_CREST_DB));
    }
}
