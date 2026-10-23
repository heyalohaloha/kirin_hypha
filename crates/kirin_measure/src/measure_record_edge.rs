//! Record entry and exit on the measurement worker.
use super::*;

pub(super) struct RecordEdge<'a> {
    pub(super) consumer: &'a mut MeasureSampleConsumer,
    pub(super) raw_pre_roll: &'a mut RawPreRollHistory,
    pub(super) chunk_f64: &'a mut Vec<f64>,
    pub(super) native_frames_total: &'a mut u64,
    pub(super) consumed_samples: &'a mut u64,
    pub(super) watch_resume: &'a mut Option<WatchResume>,
    pub(super) watch_consumed_samples: &'a mut u64,
    pub(super) watch_native_frames_total: &'a mut u64,
    pub(super) parked_watch_consumer: &'a mut Option<MeasureSampleConsumer>,
    pub(super) meter_session_suspended: &'a mut bool,
    pub(super) record_display_generation: &'a mut Option<u64>,
    pub(super) engine: &'a mut MeasureEngine,
    pub(super) record_engines: &'a mut RecordMeasureEngines,
    pub(super) phase_d: &'a mut PhaseDChannelStream,
    pub(super) resampler: &'a mut Option<ResamplerTo48k>,
    pub(super) trace_resampler: &'a mut TraceResampler,
    pub(super) latest_pd: &'a mut Option<crate::phase_d::stream::PhaseDResult>,
    pub(super) phase_d_slot_pending: &'a mut Vec<f64>,
    pub(super) record_core_pending: &'a mut VecDeque<PendingRecordCore>,
    pub(super) record_phase_pending: &'a mut VecDeque<crate::phase_d::stream::PhaseDResult>,
    pub(super) record_grid_cursor: &'a mut Option<i64>,
    pub(super) record_next_grid_end: &'a mut Option<i64>,
    pub(super) record_origin_position_samples: &'a mut Option<i64>,
    pub(super) record_capture_epoch: &'a mut Option<u64>,
    pub(super) latest_record_measure: &'a mut Option<MeasureResult>,
    pub(super) record_prefix_pending: &'a mut bool,
    pub(super) record_prefix_samples: &'a mut VecDeque<f32>,
    pub(super) record_prefix_owns_history_storage: &'a mut bool,
    pub(super) record_origin_frames: &'a mut u64,
    pub(super) record_origin_native_frames: &'a mut u64,
    pub(super) next_record_trace_ms: &'a mut u64,
    pub(super) next_record_psb_ms: &'a mut u64,
    pub(super) record_trace_frame_offset_48k: &'a mut u64,
    pub(super) record_trace_native_frame_offset: &'a mut u64,
    pub(super) last_capture_position_end: &'a mut Option<i64>,
    pub(super) measure_sequence: &'a mut u64,
    pub(super) record_sm: &'a Arc<RecordStateMachine>,
    pub(super) record_take_tracker: &'a Arc<RecordTakeTracker>,
    pub(super) record_ingress: &'a Arc<crate::RecordIngress>,
    pub(super) signal_state: &'a Arc<AtomicU8>,
    pub(super) meter_session: &'a Option<Arc<Mutex<MeterSession>>>,
    pub(super) meter_session_publication: &'a Option<Arc<MeterSessionPublication>>,
    pub(super) session_summary: &'a Arc<Mutex<Option<SessionSummary>>>,
    pub(super) record_trace_queue: &'a RecordTraceQueue,
    pub(super) result: &'a Arc<Mutex<MeasureResult>>,
    pub(super) prev_recording: bool,
    pub(super) is_recording: bool,
    pub(super) sample_rate: u32,
    pub(super) n_channels: usize,
    pub(super) measure_chunk_samples: usize,
    pub(super) playback_pass_id: u64,
}

pub(super) enum RecordEdgeOutcome {
    Ready,
    Retry,
    Skipped,
}

pub(super) fn handle_record_edge(edge: RecordEdge<'_>) -> RecordEdgeOutcome {
    let RecordEdge {
        consumer,
        raw_pre_roll,
        chunk_f64,
        native_frames_total,
        consumed_samples,
        watch_resume,
        watch_consumed_samples,
        watch_native_frames_total,
        parked_watch_consumer,
        record_display_generation,
        meter_session_suspended,
        engine,
        record_engines,
        phase_d,
        resampler,
        trace_resampler,
        latest_pd,
        phase_d_slot_pending,
        record_core_pending,
        record_phase_pending,
        record_grid_cursor,
        record_next_grid_end,
        record_origin_position_samples,
        record_capture_epoch,
        latest_record_measure,
        record_prefix_pending,
        record_prefix_samples,
        record_prefix_owns_history_storage,
        record_origin_frames,
        record_origin_native_frames,
        next_record_trace_ms,
        next_record_psb_ms,
        record_trace_frame_offset_48k,
        record_trace_native_frame_offset,
        last_capture_position_end,
        measure_sequence,
        record_sm,
        record_take_tracker,
        record_ingress,
        signal_state,
        meter_session,
        meter_session_publication,
        session_summary,
        record_trace_queue,
        result,
        prev_recording,
        is_recording,
        sample_rate,
        n_channels,
        measure_chunk_samples,
        playback_pass_id,
    } = edge;
    if !prev_recording && is_recording {
        let record_generation = record_sm.generation();
        let record_consumer = match claim_record_lane(record_ingress, record_generation) {
            RecordLaneEntry::Attached(consumer) => consumer,
            RecordLaneEntry::Abandoned => return RecordEdgeOutcome::Skipped,
            RecordLaneEntry::Pending => {
                record_sm.wait_for_measure_work(true);
                return RecordEdgeOutcome::Retry;
            }
        };
        // Consume every already-admitted Watch sample into the bounded raw history before
        // parking the lane. PRE may receive the Record control edge one callback after
        // POST; dropping this tail is the exact 7,676-sample class of missing prefix that
        // raw pre-roll exists to recover.
        record_sm.begin_measure_preparation(record_generation);
        let drained = meter_feed_gate::drain_watch_with_progress(
            consumer,
            record_take_tracker,
            raw_pre_roll,
            chunk_f64,
            native_frames_total,
            consumed_samples,
            n_channels,
            sample_rate,
            measure_chunk_samples,
            signal_state,
            meter_session.as_ref(),
            meter_session_publication.as_ref(),
            watch_resume,
            None,
            |samples| record_sm.note_measure_preparation_samples(samples),
        );
        if drained.fed {
            *meter_session_suspended = false;
        }
        *watch_consumed_samples = *consumed_samples;
        *watch_native_frames_total = *native_frames_total;
        *parked_watch_consumer = Some(std::mem::replace(consumer, record_consumer));
        if let Some(session) = meter_session.as_ref() {
            let session =
                crate::sync_recovery::lock_recover(session, "Record delta admission floor");
            record_sm.mark_record_delta_boundary(record_generation, &session);
        }
        *record_display_generation = Some(record_generation);
        record_sm.mark_record_display_measure_started(record_generation);
        *consumed_samples = 0;
        *native_frames_total = 0;
        engine.reset();
        record_engines.reset();
        phase_d.reset();
        reset_resamplers(resampler, trace_resampler);
        *latest_pd = None;
        phase_d_slot_pending.clear();
        record_core_pending.clear();
        record_phase_pending.clear();
        *record_grid_cursor = None;
        *record_next_grid_end = None;
        // Bind only after the first fully-admitted Record callback exposes its immutable
        // producer epoch and global capture origin. The prefix is raw audio selected by
        // that timestamp; no Watch result enters either Record engine.
        *record_origin_position_samples = None;
        *record_capture_epoch = None;
        *latest_record_measure = None;
        *record_prefix_pending = true;
        *watch_resume = None;
        record_prefix_samples.clear();
        *record_prefix_owns_history_storage = false;
        *record_origin_frames =
            native_frames_to_48k(record_engines.trace().total_frames(), sample_rate);
        *record_origin_native_frames = *native_frames_total;
        *next_record_trace_ms = 0;
        *next_record_psb_ms = 0;
        clear_record_trace_queue(record_trace_queue);
        *record_trace_frame_offset_48k = 0;
        *record_trace_native_frame_offset = 0;
        if let Ok(mut g) = session_summary.lock() {
            *g = None;
        }
        record_sm.mark_measure_ready(record_generation);
        log::info!("[MeasureThread] engine reset on Watch→Record transition");
    } else if prev_recording && !is_recording {
        if parked_watch_consumer.is_none() {
            return RecordEdgeOutcome::Ready;
        }
        // Record停止時はtight-drain → 最終finalize → session_summary書込みを行う。
        // 「停止前に drain 済み」の clean 録音では ring 空 → finalize は同一サンプル集合を
        // 再抽出するだけ（parity abs=0 維持）。完了後に seal を 1 前進させ、IO の bake arm が
        // post-drain 確定スナップショットを読むhandshakeを成立させる。
        let record_generation = record_display_generation
            .take()
            .unwrap_or_else(|| record_sm.generation());
        let capture_finished =
            record_ingress.finish_capture_from_measure(record_generation, Duration::from_secs(10));
        let (trace_engine, summary_engine) = record_engines.parts();
        let drained = capture_finished
            && drain_ring_into_session(
                DrainRingSession {
                    consumer: &mut *consumer,
                    trace_resampler: &mut *trace_resampler,
                    trace_engine,
                    summary_engine,
                    session_summary,
                    meter_session: meter_session.as_ref(),
                    meter_session_publication: meter_session_publication.as_ref(),
                    meter_active: load_signal_state(signal_state) == SignalState::Active,
                    chunk_f64: &mut *chunk_f64,
                    phase_d: &mut *phase_d,
                    phase_d_slot_pending: &mut *phase_d_slot_pending,
                    latest_pd: &mut *latest_pd,
                    record_core_pending: &mut *record_core_pending,
                    record_phase_pending: &mut *record_phase_pending,
                    sample_rate,
                    n_channels,
                    native_frames_total: &mut *native_frames_total,
                    consumed_samples: &mut *consumed_samples,
                    record_take_tracker,
                    last_capture_position_end: &mut *last_capture_position_end,
                    finalize_capture: true,
                    record_sm: Some(record_sm),
                    take_epoch: *record_capture_epoch,
                },
                Some(RecordTraceDrain {
                    queue: record_trace_queue,
                    timeline: RecordTraceTimeline {
                        generation: record_generation,
                        origin_frames_48k: *record_origin_frames,
                        origin_native_frames: *record_origin_native_frames,
                        offset_frames_48k: *record_trace_frame_offset_48k,
                        offset_native_frames: *record_trace_native_frame_offset,
                        origin_position_samples: *record_origin_position_samples,
                    },
                    cursor: RecordTraceCursor {
                        next_trace_ms: next_record_trace_ms,
                        next_psb_ms: next_record_psb_ms,
                    },
                }),
            );
        let completed_trace = push_record_trace_to_record_take_clock(
            record_trace_queue,
            record_generation,
            record_take_tracker,
            &mut RecordTraceCursor {
                next_trace_ms: next_record_trace_ms,
                next_psb_ms: next_record_psb_ms,
            },
            sample_rate,
        );
        // Read this Keep's capture frames before the lane retires: a failed drain drops
        // the spool, after which its queued remainder reads as zero.
        let keep_resume = after_keep_resume(
            record_ingress.capture_origin_for_measure(record_generation),
            *consumed_samples,
            consumer.slots(),
            n_channels,
            record_take_tracker.captured_frames_total(),
        );
        if drained {
            let final_summary = *crate::sync_recovery::lock_recover(
                session_summary,
                "MeasureThread final Record display summary",
            );
            record_sm.finalize_record_display(record_generation, final_summary);
            record_sm.bump_seal();
            record_ingress.mark_drained_from_measure(record_generation);
            log::info!(
                "[MeasureThread] Record→Watch tight-drain complete (seal bumped, clock_trace_completed={})",
                completed_trace
            );
        } else {
            record_sm.mark_record_display_unavailable(record_generation);
            let terminalized = record_ingress.mark_failed_from_measure(record_generation);
            log::warn!(
                "[MeasureThread] Record→Watch drain incomplete — seal NOT bumped (IO bake → integrity_degraded, ingress_terminalized={terminalized})"
            );
        }
        if *record_prefix_owns_history_storage {
            raw_pre_roll.reclaim_prefix_storage(std::mem::take(record_prefix_samples));
        } else {
            record_prefix_samples.clear();
            raw_pre_roll.clear();
        }
        *record_prefix_owns_history_storage = false;
        *record_prefix_pending = false;
        if let Some(watch_consumer) = parked_watch_consumer.take() {
            let _finished_record_consumer = std::mem::replace(consumer, watch_consumer);
            *consumed_samples = *watch_consumed_samples;
            *native_frames_total = *watch_native_frames_total;
            *watch_resume = keep_resume;
        }
        // Phase D belongs to the bounded Record producer. Do not carry its final snapshot
        // back into Watch, where the public contract is LUFS-M / TP / Crest only.
        phase_d.reset();
        *latest_pd = None;
        phase_d_slot_pending.clear();
        record_core_pending.clear();
        record_phase_pending.clear();
        let mut guard =
            crate::sync_recovery::lock_recover(result, "MeasureThread Record-to-Watch phase reset");
        clear_phase_d_fields(&mut guard);
        stamp_shared_measure_result(&mut guard, measure_sequence, playback_pass_id);
    }
    RecordEdgeOutcome::Ready
}

enum RecordLaneEntry {
    Attached(MeasureSampleConsumer),
    Abandoned,
    Pending,
}

fn claim_record_lane(ingress: &crate::RecordIngress, generation: u64) -> RecordLaneEntry {
    match ingress.take_consumer_for_measure(generation) {
        Some(consumer) => RecordLaneEntry::Attached(consumer),
        None if ingress.lane_abandoned(generation) => RecordLaneEntry::Abandoned,
        None => RecordLaneEntry::Pending,
    }
}

#[cfg(test)]
#[path = "measure_record_edge_tests.rs"]
mod tests;
