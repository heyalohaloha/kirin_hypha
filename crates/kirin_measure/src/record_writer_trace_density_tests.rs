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
fn two_pass_tracker(pass: u64) -> crate::record_take::RecordTakeTracker {
    use crate::record_take::{
        CaptureClockSource, KeepChunk, PresentationLatencySamples, RecordTakeBlock,
    };
    use crate::{AuxiliaryClockSamples, AuxiliaryClockSource};
    let tracker = crate::record_take::RecordTakeTracker::new();
    let block = 512_u64;
    let mut clock = 1_000_000_i64;
    for lap in 0..2 {
        for index in 0..pass / block {
            let position = (index * block) as i64;
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
    let tracker = two_pass_tracker(LAP);
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
