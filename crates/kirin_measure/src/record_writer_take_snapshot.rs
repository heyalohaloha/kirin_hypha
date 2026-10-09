//! Selected Record take, native WAV range and immutable audio-prefix assembly.
use super::*;

pub fn apply_record_take_snapshot(ctx: &mut RecordingCtx, tracker: Option<&RecordTakeTracker>) {
    let Some(tracker) = tracker else {
        return;
    };
    ctx.presentation_latency_at_close = tracker.presentation_latency();
    ctx.selected_capture_epoch = None;
    ctx.record_audio_prefix = None;
    ctx.presentation_range = None;

    let snapshot = tracker.snapshot(ctx.record_generation);
    let raw_range = snapshot.and_then(|snapshot| {
        snapshot
            .host_start_position_samples
            .zip(snapshot.host_end_position_samples)
    });
    if let Some((raw_start, raw_end)) = raw_range {
        ctx.writer
            .set_raw_host_clock_range(Some(crate::plugin_data::HostClockRange {
                start_position_samples: raw_start,
                end_position_samples: raw_end,
            }));
    }

    // Exact BWF presentation coordinates are the first selection authority. They are compared to
    // producer-converted capture spans in the same native sample rate; latency is never selected
    // or applied again here. If no span proves the complete BWF range, retain the producer's
    // immutable take epoch instead of searching for a longer post-bounce pass.
    let bwf_range = ctx
        .writer
        .data()
        .expected_wav
        .as_ref()
        .and_then(|expected| {
            if expected.expected_sample_rate != ctx.writer.data().sample_rate {
                return None;
            }
            let start = i64::try_from(expected.wav_time_reference_samples?).ok()?;
            let end = start.checked_add(i64::try_from(expected.expected_duration_samples).ok()?)?;
            let epoch = tracker.capture_epoch_containing_presentation_range(
                ctx.record_generation,
                start,
                expected.expected_duration_samples,
            )?;
            Some((epoch, start, end))
        });
    if let Some((epoch, start, end)) = bwf_range {
        ctx.selected_capture_epoch = Some(epoch);
        ctx.presentation_range = Some((start, end));
        if let Some((raw_start, raw_end)) =
            tracker.raw_host_range_for_capture_epoch_presentation_range(epoch, start, end)
        {
            ctx.writer
                .set_raw_host_clock_range(Some(crate::plugin_data::HostClockRange {
                    start_position_samples: raw_start,
                    end_position_samples: raw_end,
                }));
        }
    } else if let Some(epoch) = tracker.selected_capture_epoch(ctx.record_generation) {
        ctx.selected_capture_epoch = Some(epoch);
        if let Some((raw_start, raw_end)) = raw_range {
            let expected_render_duration = ctx
                .writer
                .data()
                .expected_wav
                .as_ref()
                .filter(|expected| {
                    expected.wav_time_reference_samples.is_none()
                        && expected.expected_sample_rate == ctx.writer.data().sample_rate
                })
                .map(|expected| expected.expected_duration_samples);
            ctx.presentation_range = expected_render_duration
                .and_then(|duration| {
                    tracker.presentation_range_for_latency_epoch_chain(epoch, raw_start, duration)
                })
                .or_else(|| {
                    tracker.presentation_range_for_capture_epoch(epoch, raw_start, raw_end)
                });
        }
    }
    ctx.record_audio_prefix = ctx
        .selected_capture_epoch
        .and_then(|epoch| tracker.record_audio_prefix(epoch, ctx.record_generation));
    ctx.clean_take = snapshot;
}
