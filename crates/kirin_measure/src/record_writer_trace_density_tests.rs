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
