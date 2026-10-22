//! Record->Watch tight drain: the ring left at the edge enters the Record engines before the
//! final summary, then the seal advances (B-132).
use super::capture_plan::CaptureChunkPlan;
use super::*;

pub(super) struct DrainRingSession<'a> {
    pub(super) consumer: &'a mut MeasureSampleConsumer,
    /// Phase D's resampler for the TRACE; the live engine's never takes the tail.
    pub(super) trace_resampler: &'a mut TraceResampler,
    pub(super) trace_engine: &'a mut MeasureEngine,
    pub(super) summary_engine: &'a mut MeasureEngine,
    pub(super) session_summary: &'a Arc<Mutex<Option<SessionSummary>>>,
    pub(super) meter_session: Option<&'a Arc<Mutex<MeterSession>>>,
    pub(super) meter_session_publication: Option<&'a Arc<MeterSessionPublication>>,
    pub(super) meter_active: bool,
    pub(super) chunk_f64: &'a mut Vec<f64>,
    pub(super) phase_d: &'a mut PhaseDChannelStream,
    pub(super) phase_d_slot_pending: &'a mut Vec<f64>,
    pub(super) latest_pd: &'a mut Option<crate::phase_d::stream::PhaseDResult>,
    pub(super) record_core_pending: &'a mut VecDeque<PendingRecordCore>,
    pub(super) record_phase_pending: &'a mut VecDeque<crate::phase_d::stream::PhaseDResult>,
    pub(super) sample_rate: u32,
    pub(super) n_channels: usize,
    pub(super) native_frames_total: &'a mut u64,
    pub(super) consumed_samples: &'a mut u64,
    pub(super) record_take_tracker: &'a RecordTakeTracker,
    pub(super) last_capture_position_end: &'a mut Option<i64>,
    pub(super) finalize_capture: bool,
    /// Seal-wait progress for every drained chunk; tests without a Record state pass None.
    pub(super) record_sm: Option<&'a RecordStateMachine>,
    /// The main loop's current epoch of the selected take when the Record edge arrived. The tail
    /// continues the take from it, never from the capture ring, which a long Keep outlives.
    pub(super) take_epoch: Option<u64>,
}

pub(super) fn drain_ring_into_session(
    ctx: DrainRingSession<'_>,
    mut record_trace: Option<RecordTraceDrain<'_>>,
) -> bool {
    // The TRACE pass is open until a later pass, a jump or the end of the tail closes it.
    let mut tail_take = true;
    let mut take_epoch = ctx.take_epoch;
    while ctx.consumer.slots() > 0 {
        let bounded_available = ctx.consumer.slots().min(measure_chunk_capacity_samples(
            ctx.sample_rate,
            ctx.n_channels,
        ));
        let plan = capture_chunk_plan(
            ctx.record_take_tracker,
            *ctx.native_frames_total,
            bounded_available,
            ctx.n_channels,
            ctx.sample_rate,
        );
        if plan.sample_count == 0 {
            break;
        }
        if let Some(record_sm) = ctx.record_sm {
            record_sm.note_measure_progress();
        }
        let generation = record_trace.as_ref().map(|trace| trace.timeline.generation);
        let route = tail_route(ctx.record_take_tracker, generation, &mut take_epoch, plan);
        let take = route == RecordChunkRoute::Take;
        let jumped = plan
            .position_start_samples
            .zip(*ctx.last_capture_position_end)
            .is_some_and(|(start, previous_end)| start != previous_end);
        // The TRACE pass ends inside the tail: close it before this chunk enters the resampler.
        if tail_take && (!take || jumped) {
            let _ = close_trace_pass(
                ctx.trace_resampler,
                ctx.n_channels,
                ctx.phase_d_slot_pending,
                ctx.phase_d,
                ctx.latest_pd,
                ctx.record_phase_pending,
                ctx.record_core_pending,
                record_trace.as_mut(),
                ctx.sample_rate,
            );
        }
        tail_take = take;
        if jumped {
            ctx.trace_engine.reset();
            // Recordのsession summaryは通常ループと同じくtransport位置の不連続を
            // またいで維持する。tight-drainだけで最後のjumpを観測してもI/Max TPを
            // 直前区間へ縮めない。TRACE/Phase Dの座標系リセットとは独立。
            ctx.phase_d.reset();
            ctx.trace_resampler.reset();
            *ctx.latest_pd = None;
            ctx.phase_d_slot_pending.clear();
            ctx.record_core_pending.clear();
            ctx.record_phase_pending.clear();
        }
        ctx.chunk_f64.clear();
        for _ in 0..plan.sample_count {
            match ctx.consumer.pop() {
                Ok(s) => {
                    *ctx.consumed_samples = (*ctx.consumed_samples).saturating_add(1);
                    ctx.chunk_f64.push(s as f64);
                }
                Err(_) => break,
            }
        }
        let chunk_native_frames = (ctx.chunk_f64.len() / ctx.n_channels) as u64;
        meter_feed_gate::MeterFeed {
            tracker: ctx.record_take_tracker,
            session: ctx.meter_session,
            publication: ctx.meter_session_publication,
            n_channels: ctx.n_channels,
            fallback_active: ctx.meter_active,
        }
        .feed(ctx.chunk_f64, *ctx.native_frames_total, plan, 0);
        *ctx.native_frames_total = (*ctx.native_frames_total).saturating_add(chunk_native_frames);
        let native_frames_after = *ctx.native_frames_total;
        let captured_frames_before = native_frames_after.saturating_sub(chunk_native_frames);
        *ctx.last_capture_position_end = plan.position_end_samples;
        // Only the take reaches Phase D, so only the take is resampled for it.
        if take {
            let chunk_48k = match ctx.trace_resampler.process(ctx.chunk_f64) {
                Ok(audio) => audio,
                Err(e) => {
                    log::warn!(
                        "[MeasureThread] drain resample error: {:?} — tail finalize skipped",
                        e
                    );
                    if let Some(trace) = record_trace.as_mut() {
                        push_record_timeline_markers_until(
                            trace,
                            ctx.sample_rate,
                            native_frames_after,
                        );
                    }
                    return false;
                }
            };
            feed_phase_d_slots(
                chunk_48k,
                ctx.n_channels,
                ctx.phase_d_slot_pending,
                ctx.phase_d,
                ctx.latest_pd,
                ctx.record_phase_pending,
                true,
            );
        }
        if let Some(trace) = record_trace.as_mut() {
            let chunk = plan.keep_chunk(
                trace.timeline.generation,
                chunk_native_frames,
                route,
                !route.live_only(),
            );
            ctx.record_take_tracker.note_keep_chunk(chunk);
            if !route.live_only() {
                let _ = ctx.summary_engine.push(ctx.chunk_f64);
            }
            if !take {
                continue;
            }
            let trace_engine_frames_before = ctx.trace_engine.total_frames();
            let trace_engine_pending_frames_before = ctx.trace_engine.pending_frames();
            let _ = ctx.trace_engine.push_observed(
                ctx.chunk_f64,
                |native_engine_frames, base_result, observed_chunk| {
                    let (observed_native_frames, clock_point) = observed_capture_endpoint(
                        plan,
                        ctx.record_take_tracker,
                        captured_frames_before,
                        trace_engine_frames_before,
                        trace_engine_pending_frames_before,
                        native_engine_frames,
                        chunk_native_frames,
                    );
                    let observed_silence_floor = samples_are_trace_silence_floor(observed_chunk);
                    ctx.record_core_pending.push_back(PendingRecordCore {
                        frames_48k: native_frames_to_48k(observed_native_frames, ctx.sample_rate),
                        native_frames: observed_native_frames,
                        position_samples: clock_point.map(|point| point.position_samples),
                        raw_host_position_samples: clock_point
                            .map(|point| point.raw_host_position_samples),
                        capture_epoch: clock_point.map(|point| point.epoch),
                        clock_source: clock_point
                            .map_or(CaptureClockSource::Unknown, |point| point.source),
                        presentation_latency: clock_point.map_or_else(
                            || ctx.record_take_tracker.presentation_latency(),
                            |point| point.presentation_latency,
                        ),
                        observed_silence_floor,
                        result: base_result.clone(),
                    });
                },
            );
            join_record_trace_slots(
                trace,
                ctx.sample_rate,
                ctx.record_core_pending,
                ctx.record_phase_pending,
            );
        } else {
            let _ = ctx.trace_engine.push(ctx.chunk_f64);
            let _ = ctx.summary_engine.push(ctx.chunk_f64);
        }
    }
    if ctx.finalize_capture
        && tail_take
        && !close_trace_pass(
            ctx.trace_resampler,
            ctx.n_channels,
            ctx.phase_d_slot_pending,
            ctx.phase_d,
            ctx.latest_pd,
            ctx.record_phase_pending,
            ctx.record_core_pending,
            record_trace.as_mut(),
            ctx.sample_rate,
        )
    {
        return false;
    }
    if let Some(trace) = record_trace.as_mut() {
        push_record_timeline_markers_until(trace, ctx.sample_rate, *ctx.native_frames_total);
    }
    let summary = ctx.summary_engine.finalize();
    match ctx.session_summary.lock() {
        Ok(mut g) => {
            *g = Some(summary);
            true
        }
        Err(e) => {
            log::warn!(
                "[MeasureThread] drain session_summary Mutex poisoned: {}",
                e
            );
            false
        }
    }
}

/// The tail keeps the main loop's routes: TRACE and Phase D only for the selected take, the Keep
/// summary also for its later realtime passes, and every chunk in the pass journal.
/// The Record generation is the drain's own, as in the main loop, never the chunk's span. The
/// take continues from the main loop's epoch through the same latency and auxiliary-only
/// continuations; the selected take's first epoch may have left the capture ring long ago.
fn tail_route(
    tracker: &RecordTakeTracker,
    generation: Option<u64>,
    take_epoch: &mut Option<u64>,
    plan: CaptureChunkPlan,
) -> RecordChunkRoute {
    let selected = generation.and_then(|generation| tracker.selected_capture_epoch(generation));
    let transition = generation
        .is_some_and(|generation| take_epoch_transition(tracker, *take_epoch, plan, generation));
    let route = chunk_route(tracker, selected, *take_epoch, plan, transition);
    if transition {
        *take_epoch = plan.capture_epoch;
    }
    route
}

#[cfg(test)]
#[path = "measure_long_keep_tests.rs"]
mod long_keep_tests;

#[cfg(test)]
#[path = "measure_tail_pass_end_tests.rs"]
mod pass_end_tests;
