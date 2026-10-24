//! A Keep that went on into later passes keeps its TRACE to the first pass. When the listed
//! passes explain the rest of the record (an exported WAV of the whole Keep), the TRACE is
//! judged on its own pass and a normal LOOP wrap degrades nothing; an unexplained span still does.
use super::super::tests::{
    expected_wav_metadata, full_measure_result, isolated_base, make_ctx_with_sample_rate,
    read_output_for_final, trace_sample_frames_with_native,
};
use super::*;
use crate::plugin_data::{PluginDataFile, Role};
use crate::record_take::{KeepPass, KEEP_PASS_LOOP_WRAP};

const RATE: u64 = 48_000;
/// The loop end of the tracked Keeps, in host samples.
const LOOP_END: u64 = 600 * 512;
/// One pass of a six-second loop.
const PASS: u64 = 6 * RATE;

fn pass(in_trace: bool, frames: u64) -> KeepPass {
    KeepPass {
        host_start_position_samples: Some(0),
        host_end_position_samples: Some(frames as i64),
        frames,
        summary_frames: frames,
        in_trace,
        trace_excluded_reason: (!in_trace).then(|| KEEP_PASS_LOOP_WRAP.to_string()),
    }
}

/// A two-pass Keep exported as one WAV; the TRACE measures the first pass only.
fn close_two_pass_keep(later_pass_frames: u64, session: &str) -> PluginDataFile {
    let base = isolated_base();
    let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), RATE as u32);
    ctx.writer.set_record_session_id(Some(session.to_string()));
    ctx.writer
        .set_expected_wav(Some(expected_wav_metadata(RATE as u32, 2 * PASS, session)));
    ctx.record_generation = 1;
    mark_trace_time(&mut ctx, 6_000, PASS, Some(PASS));
    let measure = full_measure_result();
    for t_ms in (100_u64..=6_000).step_by(FRAME_INTERVAL_MS as usize) {
        let native = t_ms * RATE / 1_000;
        let mut sample =
            trace_sample_frames_with_native(native, Some(native), measure.clone(), t_ms == 100);
        sample.position_samples = Some(native as i64);
        sample.raw_host_position_samples = Some(native as i64);
        ctx.trace_samples.push(sample);
    }
    ctx.trace_sample_count = ctx.trace_samples.len();
    ctx.writer
        .set_keep_passes(vec![pass(true, PASS), pass(false, later_pass_frames)]);
    let final_path = ctx.final_path.clone();
    writer_close(ctx);
    read_output_for_final(&final_path)
}

#[test]
fn a_normal_loop_wrap_under_an_exported_wav_degrades_nothing() {
    let loaded = close_two_pass_keep(PASS, "keep-two-passes-explained");
    assert!(
        !loaded.integrity_degraded,
        "a normal LOOP wrap: {:?}",
        loaded.integrity_reasons
    );
    assert_eq!(loaded.keep_passes.len(), 2);
    assert_eq!(loaded.frames.last().map(|frame| frame.t_ms), Some(6_000));
    // The TRACE is judged on its own pass; the WAV keeps the whole Keep.
    let diagnostics = loaded.trace_diagnostics.expect("TRACE diagnostics");
    assert_eq!(
        (diagnostics.expected_frame_count, diagnostics.missing_slots),
        (60, 0)
    );
    let take = loaded.bounce_take.expect("bounce take");
    assert_eq!((take.duration_samples, take.end_t_ms), (2 * PASS, 12_000));
}

#[test]
fn a_span_the_passes_do_not_explain_still_degrades_the_record() {
    // The WAV runs six seconds past everything the Keep accepted.
    let loaded = close_two_pass_keep(RATE, "keep-two-passes-unexplained");
    assert!(loaded.integrity_degraded);
    assert!(
        loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "frame_timeline_gap" || reason == "raw_trace_timeline_gap"),
        "{:?}",
        loaded.integrity_reasons
    );
}

/// The producer side of a realtime two-pass Keep: the take, a LOOP wrap, and the pass journal the
/// Measure Thread wrote, as the IO Thread finds them at close.
fn two_pass_tracker(first: u64, later: u64) -> crate::record_take::RecordTakeTracker {
    use crate::record_take::{
        CaptureClockSource, KeepChunk, PresentationLatencySamples, RecordTakeBlock,
    };
    use crate::{AuxiliaryClockSamples, AuxiliaryClockSource};
    let tracker = crate::record_take::RecordTakeTracker::new();
    let block = 512_u64;
    let mut clock = 1_000_000_i64;
    // The take may begin anywhere in the loop; it runs to the loop end, then the loop wraps.
    for (lap, (start, frames)) in [(LOOP_END - first, first), (0, later)]
        .into_iter()
        .enumerate()
    {
        for index in 0..frames / block {
            let position = (start + index * block) as i64;
            tracker.note_block(RecordTakeBlock {
                generation: 1,
                recording: true,
                rendered: true,
                playing: true,
                offline: false,
                position_valid: true,
                position_samples: position,
                num_frames: block,
                clock_start_samples: 0,
                clock_end_samples: None,
            });
            tracker.stage_host_loop_wrap(lap == 1 && index == 0);
            tracker.note_capture_window_with_clocks_boundary(
                true,
                position,
                block,
                CaptureClockSource::ProjectTimeline,
                PresentationLatencySamples::default(),
                AuxiliaryClockSamples {
                    source: AuxiliaryClockSource::Vst3Continuous,
                    samples: Some(clock),
                },
                false,
            );
            tracker.note_keep_chunk(KeepChunk {
                generation: 1,
                raw_host_start: Some(position),
                auxiliary: AuxiliaryClockSamples {
                    source: AuxiliaryClockSource::Vst3Continuous,
                    samples: Some(clock),
                },
                frames: block,
                in_trace: lap == 0,
                in_summary: true,
                after_offline_take: false,
                host_loop_wrap: lap == 1 && index == 0,
            });
            clock += block as i64;
        }
    }
    tracker
}

/// A realtime two-pass Keep closed with its tracker and an exported WAV of the whole Keep.
fn close_tracked_keep(wav_reference: Option<u64>, session: &str) -> PluginDataFile {
    const LAP: u64 = 600 * 512;
    let tracker = two_pass_tracker(LAP, LAP);
    let selected = tracker.selected_capture_epoch(1).expect("take epoch");
    let base = isolated_base();
    let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), RATE as u32);
    ctx.writer.set_record_session_id(Some(session.to_string()));
    let mut expected = expected_wav_metadata(RATE as u32, 2 * LAP, session);
    expected.wav_time_reference_samples = wav_reference;
    ctx.writer.set_expected_wav(Some(expected));
    ctx.record_generation = 1;
    mark_trace_time(&mut ctx, LAP * 1_000 / RATE, LAP, Some(LAP));
    let measure = full_measure_result();
    for slot in 1..=LAP * 10 / RATE {
        let native = slot * RATE / 10;
        let mut sample =
            trace_sample_frames_with_native(native, Some(native), measure.clone(), slot == 1);
        sample.position_samples = Some(native as i64);
        sample.raw_host_position_samples = Some(native as i64);
        sample.capture_epoch = Some(selected);
        ctx.trace_samples.push(sample);
    }
    ctx.trace_sample_count = ctx.trace_samples.len();
    crate::record_writer::apply_record_take_snapshot(&mut ctx, Some(&tracker));
    let final_path = ctx.final_path.clone();
    writer_close(ctx);
    read_output_for_final(&final_path)
}

#[test]
fn a_tracked_two_pass_keep_under_its_wav_is_complete_on_its_trace_pass() {
    for (reference, session) in [(None, "tracked-keep-render"), (Some(0), "tracked-keep-bwf")] {
        let loaded = close_tracked_keep(reference, session);
        assert!(
            !loaded.integrity_degraded,
            "{session}: {:?}",
            loaded.integrity_reasons
        );
        assert_eq!(loaded.keep_passes.len(), 2, "{session}");
        let quality = loaded.record_quality.expect("record quality");
        assert!(quality.complete, "{session}: {quality:?}");
        assert_eq!(
            loaded.trace_slot_positions.len(),
            loaded.frames.len(),
            "{session}: the TRACE keeps its canonical slots"
        );
    }
}

#[test]
fn passes_that_do_not_add_up_to_the_wav_explain_nothing() {
    use crate::record_take::trace_span_samples;
    let passes = |later| vec![pass(true, PASS), pass(false, later)];
    let rate = RATE as u32;
    // Ten laps of a short loop never played the rest of a longer exported song.
    assert_eq!(
        trace_span_samples(&passes(9 * PASS), 3 * PASS, rate),
        3 * PASS
    );
    assert_eq!(
        trace_span_samples(&passes(PASS), 2 * PASS + RATE, rate),
        2 * PASS + RATE
    );
    // The whole Keep, within one 100 ms slot, is explained.
    assert_eq!(
        trace_span_samples(&passes(PASS), 2 * PASS + RATE / 10, rate),
        PASS
    );
    assert_eq!(
        trace_span_samples(&passes(PASS), 2 * PASS - RATE / 10, rate),
        PASS
    );
}

/// Keep pressed less than a second before the loop end: the take (the TRACE pass) is short, the
/// Keep goes on through the following passes. That is no lifecycle fragment, and no WAV is needed.
const SHORT_FIRST: u64 = 88 * 512; // 0.94 s before the loop end

/// A Keep whose first pass is short and whose next pass is a whole loop, closed with an optional
/// exported WAV of `wav_samples`.
fn close_short_first_pass(wav_samples: Option<u64>, session: &str) -> PluginDataFile {
    const FIRST: u64 = SHORT_FIRST;
    let tracker = two_pass_tracker(FIRST, LOOP_END);
    let selected = tracker.selected_capture_epoch(1).expect("take epoch");
    let base = isolated_base();
    let mut ctx = make_ctx_with_sample_rate(&base, Role::Post, now_epoch_ms(), RATE as u32);
    ctx.writer.set_record_session_id(Some(session.to_string()));
    if let Some(samples) = wav_samples {
        let mut expected = expected_wav_metadata(RATE as u32, samples, session);
        // The exported WAV starts where the Keep's recording started.
        expected.wav_time_reference_samples = Some(LOOP_END - FIRST);
        ctx.writer.set_expected_wav(Some(expected));
    }
    ctx.record_generation = 1;
    let origin = (LOOP_END - FIRST) as i64;
    mark_trace_time(&mut ctx, FIRST * 1_000 / RATE, FIRST, Some(FIRST));
    let measure = full_measure_result();
    let positions = if wav_samples.is_some() {
        (1..=FIRST * 10 / RATE)
            .map(|slot| origin + (slot * RATE / 10) as i64)
            .collect::<Vec<_>>()
    } else {
        // Without a WAV origin the producer publishes the absolute 100 ms grid, including
        // the first partial observation and the exact loop endpoint.
        (264_000_i64..=307_200).step_by(4_800).collect()
    };
    for (index, position) in positions.into_iter().enumerate() {
        let native = (position - origin) as u64;
        let mut sample =
            trace_sample_frames_with_native(native, Some(native), measure.clone(), index == 0);
        sample.position_samples = Some(position);
        sample.raw_host_position_samples = Some(position);
        sample.capture_epoch = Some(selected);
        ctx.trace_samples.push(sample);
    }
    ctx.trace_sample_count = ctx.trace_samples.len();
    crate::record_writer::apply_record_take_snapshot(&mut ctx, Some(&tracker));
    writer_close(ctx);
    read_session_output(&base, session)
}

#[test]
fn a_keep_whose_first_pass_is_short_is_no_fragment_once_it_plays_on() {
    // Without a WAV, and with a WAV of exactly the passes the Keep accepted.
    for (wav, session) in [
        (None, "short-first-pass"),
        (Some(SHORT_FIRST + LOOP_END), "short-first-pass-wav"),
    ] {
        let loaded = close_short_first_pass(wav, session);
        assert_eq!(loaded.keep_passes.len(), 2, "{session}");
        assert!(
            !loaded.integrity_degraded,
            "{session}: a short first pass of a Keep that plays on: {:?}",
            loaded.integrity_reasons
        );
        assert!(
            loaded.record_quality.as_ref().unwrap().complete,
            "{session}"
        );
        if wav.is_none() {
            assert_eq!(
                loaded.trace_slot_positions,
                (268_800_i64..=307_200).step_by(4_800).collect::<Vec<_>>()
            );
        }
    }
}

#[test]
fn a_wav_that_does_not_match_the_passes_of_a_short_first_pass_keep_stays_degraded() {
    // The whole song was exported, far longer than the Keep played: never "corresponding".
    let loaded = close_short_first_pass(Some(10 * LOOP_END), "short-first-pass-long-wav");
    assert!(loaded.integrity_degraded);
    assert!(
        loaded
            .integrity_reasons
            .iter()
            .any(|reason| reason == "frame_timeline_gap" || reason == "missing_trace_slots"),
        "{:?}",
        loaded.integrity_reasons
    );
    let quality = loaded.record_quality.expect("record quality");
    assert!(!quality.complete, "{quality:?}");
}

/// The one record a closed writer left for `session`: committed, pending or failed.
fn read_session_output(base: &std::path::Path, session: &str) -> PluginDataFile {
    fn find(dir: &std::path::Path, name: &str) -> Option<std::path::PathBuf> {
        std::fs::read_dir(dir).ok()?.flatten().find_map(|entry| {
            let path = entry.path();
            if path.is_dir() {
                find(&path, name)
            } else {
                (path.file_name()?.to_str()? == name).then_some(path)
            }
        })
    }
    let path = find(base, &format!("{session}.json")).expect("the session's record");
    serde_json::from_slice(&std::fs::read(path).unwrap()).unwrap()
}

#[path = "record_writer_selected_take_tests.rs"]
mod selected_take_tests;
