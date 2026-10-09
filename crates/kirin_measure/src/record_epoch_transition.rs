//! Record measurement grid ownership at a producer-qualified clock epoch boundary.
use super::capture_plan::CaptureChunkPlan;

pub(super) fn advance_record_epoch(
    latency_epoch_transition: bool,
    capture_plan: CaptureChunkPlan,
    record_next_grid_end: &mut Option<i64>,
    record_capture_epoch: &mut Option<u64>,
    record_grid_cursor: &mut Option<i64>,
) {
    if latency_epoch_transition {
        // Keep the DSP window continuous. Only the sample-coordinate mapping changed;
        // resetting here would inject artificial silence and lose 400 ms of TRACE.
        *record_next_grid_end = None;
        *record_capture_epoch = capture_plan.capture_epoch;
        *record_grid_cursor = capture_plan.position_start_samples;
    }
}

pub(super) fn record_grid_alignment(position_start: i64, sample_rate: u32) -> (usize, i64) {
    let slot_frames = (sample_rate as i64 / 10).max(1);
    let phase_frames = position_start.rem_euclid(slot_frames) as usize;
    let remaining = if phase_frames == 0 {
        slot_frames
    } else {
        slot_frames.saturating_sub(phase_frames as i64)
    };
    (phase_frames, position_start.saturating_add(remaining))
}
