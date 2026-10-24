//! Closed Record clocks use measured absolute slots until a WAV clock is known.
use super::super::tests::{
    expected_wav_metadata, full_measure_result, make_ctx_with_sample_rate,
    trace_sample_frames_with_native,
};
use super::*;
use crate::plugin_data::Role;

fn take_context(base: &Path, role: Role, rate: u32, start: i64, end: i64) -> RecordingCtx {
    let mut ctx = make_ctx_with_sample_rate(base, role, 1_700_000_000_000, rate);
    ctx.record_generation = 1;
    ctx.selected_capture_epoch = Some(1);
    ctx.presentation_range = Some((start, end));
    ctx.clean_take = Some(RecordTakeSnapshot {
        generation: 1,
        duration_samples: (end - start) as u64,
        source: RECORD_TAKE_SOURCE_WAV_CLOCK,
        host_start_position_samples: Some(start),
        host_end_position_samples: Some(end),
    });
    ctx.writer
        .set_record_session_id(Some("record-slot-grid".to_string()));
    let step = i64::from(rate) / 10;
    // Include the partial first measurement too: closing must select the correct population,
    // rather than relabeling the first N observations with later positions.
    let first_observation = start + step - start.rem_euclid(step);
    for position in (first_observation..=end).step_by(step as usize) {
        let elapsed = (position - start) as u64;
        let mut result = full_measure_result();
        result.lufs_m = Some(-40.0 + (position / step) as f64 / 2.0);
        let mut sample = trace_sample_frames_with_native(
            elapsed * TRACE_TIMEBASE_HZ / u64::from(rate),
            Some(elapsed),
            result,
            false,
        );
        sample.position_samples = Some(position);
        sample.raw_host_position_samples = Some(position);
        sample.capture_epoch = Some(1);
        ctx.trace_samples.push(sample);
    }
    ctx.trace_sample_count = ctx.trace_samples.len();
    ctx
}

fn close_take(ctx: RecordingCtx) -> PluginDataFile {
    let role_dir = ctx.final_path.parent().unwrap().to_path_buf();
    assert!(writer_close_result(ctx), "the Record file must close");
    // Complete and incomplete records can use different session-derived file names. Read the
    // one closed artifact in this isolated role directory, including the diagnostic path.
    let closed = [
        role_dir.clone(),
        role_dir.join(".pair_pending"),
        role_dir.join(".failed"),
    ]
    .into_iter()
    .filter_map(|dir| fs::read_dir(dir).ok())
    .flatten()
    .map(|entry| entry.unwrap().path())
    .filter(|path| path.extension().is_some_and(|ext| ext == "json"))
    .collect::<Vec<_>>();
    assert_eq!(closed.len(), 1, "one closed artifact: {closed:?}");
    serde_json::from_slice(&fs::read(&closed[0]).unwrap()).unwrap()
}

fn assert_closed_grid(
    role: Role,
    rate: u32,
    start: i64,
    end: i64,
    first: i64,
    expected_count: usize,
) {
    let base = tempfile::tempdir().unwrap();
    let ctx = take_context(base.path(), role, rate, start, end);
    assert!(ctx.writer.data().expected_wav.is_none());

    let closed = close_take(ctx);
    let step = i64::from(rate) / 10;
    let expected_positions: Vec<_> = (first..=end).step_by(step as usize).collect();
    assert_eq!(expected_positions.len(), expected_count);
    assert_eq!(closed.trace_slot_positions, expected_positions);
    assert_eq!(closed.frames.len(), expected_count);
    assert_eq!(closed.frames.first().unwrap().t_ms, 100);
    assert_eq!(
        closed.frames.last().unwrap().t_ms,
        expected_count as u64 * 100
    );
    for (frame, position) in closed.frames.iter().zip(&expected_positions) {
        assert_eq!(frame.lufs_m, -40.0 + (*position / step) as f64 / 2.0);
    }
    assert_eq!(
        closed.trace_time_axis.as_deref(),
        Some(crate::trace_alignment::TRACE_HOST_TIME_AXIS)
    );
    let clock = closed.trace_clock.as_ref().expect("closed absolute clock");
    assert_eq!(clock.origin_position_samples, first - step);
    assert_eq!(clock.end_position_samples, end);
    assert_eq!(clock.sample_rate, rate);
    let diagnostics = closed.trace_diagnostics.as_ref().unwrap();
    assert_eq!(diagnostics.expected_frame_count, expected_count as u64);
    assert_eq!(diagnostics.measured_frame_count, expected_count as u64);
    assert_eq!(diagnostics.missing_slots, 0);
    let quality = closed.record_quality.as_ref().unwrap();
    assert!(quality.trace_slots_complete, "{quality:?}");
    assert!(
        quality.complete,
        "{quality:?}; {:?}",
        closed.integrity_reasons
    );
    assert_eq!(quality.missing_trace_slots, 0);
    assert!(closed.expected_wav.is_none());
}

#[test]
fn partial_start_pre_record_closes_at_the_exact_grid_boundary() {
    assert_closed_grid(Role::Pre, 96_000, 4_416, 384_000, 19_200, 39);
}

#[test]
fn partial_start_post_record_closes_at_the_exact_grid_boundary() {
    assert_closed_grid(Role::Post, 96_000, 26_432, 384_000, 38_400, 37);
}

#[test]
fn partial_start_record_grid_is_native_at_lower_and_higher_sample_rates() {
    for (rate, start, end, first, count, role) in [
        (48_000, 2_208, 192_000, 9_600, 39, Role::Pre),
        (48_000, 13_216, 192_000, 19_200, 37, Role::Post),
        (192_000, 8_832, 768_000, 38_400, 39, Role::Pre),
        (192_000, 52_864, 768_000, 76_800, 37, Role::Post),
    ] {
        assert_closed_grid(role, rate, start, end, first, count);
    }
}

#[test]
fn aligned_record_starts_keep_their_first_and_last_slots() {
    for (rate, start, end, first, count, role) in [
        (48_000, 0, 192_000, 4_800, 40, Role::Pre),
        (96_000, 9_600, 384_000, 19_200, 39, Role::Post),
        (192_000, 0, 768_000, 19_200, 40, Role::Post),
    ] {
        assert_closed_grid(role, rate, start, end, first, count);
    }
}

#[test]
fn a_missing_measured_endpoint_is_never_invented_by_grid_selection() {
    let base = tempfile::tempdir().unwrap();
    let mut ctx = take_context(base.path(), Role::Post, 96_000, 26_432, 384_000);
    ctx.trace_samples.pop();
    ctx.trace_sample_count = ctx.trace_samples.len();
    let closed = close_take(ctx);
    assert_eq!(closed.trace_slot_positions.first(), Some(&38_400));
    assert_eq!(closed.trace_slot_positions.last(), Some(&374_400));
    assert_eq!(closed.frames.len(), 36);
    assert_eq!(closed.trace_diagnostics.as_ref().unwrap().missing_slots, 1);
    let quality = closed.record_quality.as_ref().unwrap();
    assert!(!quality.trace_slots_complete);
    assert!(!quality.complete);
}

#[test]
fn an_unaligned_end_preserves_the_existing_absolute_grid() {
    let base = tempfile::tempdir().unwrap();
    let ctx = take_context(base.path(), Role::Post, 96_000, 6_461_383, 7_921_607);
    let positions = producer_take_slot_positions(&ctx, 152, 9_600).unwrap();
    assert_eq!(positions.first(), Some(&6_470_400));
    assert_eq!(positions.last(), Some(&7_920_000));
}

#[test]
fn a_known_bwf_origin_keeps_the_wav_relative_grid() {
    let base = tempfile::tempdir().unwrap();
    let mut ctx = take_context(base.path(), Role::Pre, 96_000, 4_416, 384_000);
    let mut expected = expected_wav_metadata(96_000, 379_584, "record-slot-clock");
    expected.wav_time_reference_samples = Some(4_416);
    ctx.writer.set_expected_wav(Some(expected));
    let positions = producer_take_slot_positions(&ctx, 39, 9_600).unwrap();
    assert_eq!(positions.first(), Some(&14_016));
    assert_eq!(positions.last(), Some(&378_816));
}

#[test]
fn unresolved_or_invalid_ranges_do_not_authorize_slot_positions() {
    let base = tempfile::tempdir().unwrap();
    let mut ctx = take_context(base.path(), Role::Pre, 96_000, 4_416, 384_000);
    assert!(producer_take_slot_positions(&ctx, 0, 9_600).is_none());
    assert!(producer_take_slot_positions(&ctx, 39, 0).is_none());
    assert!(producer_take_slot_positions(&ctx, 39, -9_600).is_none());
    assert!(producer_take_slot_positions(&ctx, 41, 9_600).is_none());
    ctx.presentation_range = Some((4_416, 384_001));
    assert!(producer_take_slot_positions(&ctx, 39, 9_600).is_none());
    ctx.presentation_range = None;
    assert!(producer_take_slot_positions(&ctx, 39, 9_600).is_none());
    ctx.presentation_range = Some((4_416, 384_000));
    ctx.clean_take.as_mut().unwrap().duration_samples = u64::MAX;
    assert!(producer_take_slot_positions(&ctx, 39, 9_600).is_none());
}
