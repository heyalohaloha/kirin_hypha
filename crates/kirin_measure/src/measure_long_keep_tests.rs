//! A long Keep on a host whose auxiliary clock cuts at every callback.
//!
//! Every callback is its own capture epoch, so after 4096 callbacks (about 44 s at 512 frames)
//! the selected take's first epoch has left the capture ring. The take still continues through
//! the auxiliary-only cuts: the STOP tail stays in the TRACE and a later LOOP pass still reaches
//! the Keep summary. Neither may depend on the retired ring slot.
use super::super::capture_plan::{capture_chunk_plan, chunk_route, RecordChunkRoute};
use super::{drain_ring_into_session, DrainRingSession, MeasureSampleConsumer, RecordTraceDrain};
use super::{RecordTraceCursor, RecordTraceTimeline};
use crate::capture_clock::CAPTURE_CLOCK_SPAN_CAPACITY;
use crate::channel_layout::ChannelLayout;
use crate::engine::MeasureEngine;
use crate::phase_d::channels::PhaseDChannelStream;
use crate::phase_d::tables::FieldType;
use crate::record_take::{
    CaptureClockSource, PresentationLatencySamples, PresentationLatencySource, RecordTakeBlock,
    RecordTakeTracker,
};
use crate::record_writer::{drain_record_trace_queue, new_record_trace_queue};
use crate::{AuxiliaryClockSamples, AuxiliaryClockSource};
use std::sync::{Arc, Mutex};

const SR: u32 = 48_000;
const BLOCK: i64 = 512;
const GENERATION: u64 = 1;

/// One realtime Record callback at `position`. The auxiliary clock alternates by one block, so
/// it never continues and every callback publishes an auxiliary-only cut.
fn record_callback(tracker: &RecordTakeTracker, index: i64, position: i64) {
    tracker.note_block(RecordTakeBlock {
        generation: GENERATION,
        recording: true,
        rendered: true,
        playing: true,
        offline: false,
        position_valid: true,
        position_samples: position,
        num_frames: BLOCK as u64,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    tracker.note_capture_window_with_clocks_boundary(
        true,
        position,
        BLOCK as u64,
        CaptureClockSource::ProjectTimeline,
        PresentationLatencySamples {
            source: PresentationLatencySource::AudioUnitV2,
            input: Some(0),
            output: Some(0),
        },
        AuxiliaryClockSamples {
            source: AuxiliaryClockSource::AudioUnitRender,
            samples: Some(position + (index % 2) * BLOCK),
        },
        false,
    );
}

/// A Keep long enough that its first epoch left the ring.
fn long_keep(callbacks: i64) -> RecordTakeTracker {
    let tracker = RecordTakeTracker::new();
    for index in 0..callbacks {
        record_callback(&tracker, index, index * BLOCK);
    }
    assert!(
        tracker
            .clock_point_for_captured_frame(BLOCK as u64)
            .is_none(),
        "the selected take's first epoch must have left the ring"
    );
    tracker
}

fn sine_stereo(frames: usize) -> Vec<f32> {
    (0..frames)
        .flat_map(|frame| {
            let sample = 0.25 * (frame as f32 * 997.0 * std::f32::consts::TAU / SR as f32).sin();
            [sample, sample]
        })
        .collect()
}

#[test]
fn a_long_keep_tail_stays_in_the_take_after_its_first_epoch_left_the_ring() {
    let callbacks = CAPTURE_CLOCK_SPAN_CAPACITY as i64 + 200;
    let tail_callbacks = 100;
    let tracker = long_keep(callbacks);
    // The main loop consumed every callback before the tail and admitted each auxiliary-only cut.
    let consumed = ((callbacks - tail_callbacks) * BLOCK) as u64;
    let take_epoch = tracker
        .clock_point_for_captured_frame(consumed)
        .unwrap()
        .epoch;

    let tail = sine_stereo((tail_callbacks * BLOCK) as usize);
    let (mut producer, consumer) = rtrb::RingBuffer::new(tail.len());
    for sample in &tail {
        producer.push(*sample).unwrap();
    }
    let mut consumer = MeasureSampleConsumer::watch(consumer);
    let queue = new_record_trace_queue();
    let (mut next_trace_ms, mut next_psb_ms) = (0, 0);
    let mut resampler = None;
    let mut trace_engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let mut summary_engine = MeasureEngine::new(SR, ChannelLayout::stereo()).unwrap();
    let summary = Arc::new(Mutex::new(None));
    let mut phase = PhaseDChannelStream::new(FieldType::Free, 2);
    let (mut chunk, mut resampled, mut phase_slots) = (Vec::new(), Vec::new(), Vec::new());
    let mut latest_phase = None;
    let mut core_pending = std::collections::VecDeque::new();
    let mut phase_pending = std::collections::VecDeque::new();
    let (mut native_frames, mut consumed_samples) = (consumed, 0_u64);
    let mut last_position_end = Some(consumed as i64);
    let ok = drain_ring_into_session(
        DrainRingSession {
            consumer: &mut consumer,
            resampler: &mut resampler,
            trace_engine: &mut trace_engine,
            summary_engine: &mut summary_engine,
            session_summary: &summary,
            meter_session: None,
            meter_session_publication: None,
            meter_active: false,
            chunk_f64: &mut chunk,
            resampled_buf: &mut resampled,
            phase_d: &mut phase,
            phase_d_slot_pending: &mut phase_slots,
            latest_pd: &mut latest_phase,
            record_core_pending: &mut core_pending,
            record_phase_pending: &mut phase_pending,
            sample_rate: SR,
            n_channels: 2,
            native_frames_total: &mut native_frames,
            consumed_samples: &mut consumed_samples,
            record_take_tracker: &tracker,
            last_capture_position_end: &mut last_position_end,
            finalize_capture: true,
            record_sm: None,
            take_epoch: Some(take_epoch),
        },
        Some(RecordTraceDrain {
            queue: &queue,
            timeline: RecordTraceTimeline {
                generation: GENERATION,
                origin_frames_48k: 0,
                origin_native_frames: 0,
                offset_frames_48k: 0,
                offset_native_frames: 0,
                origin_position_samples: Some(0),
            },
            cursor: RecordTraceCursor {
                next_trace_ms: &mut next_trace_ms,
                next_psb_ms: &mut next_psb_ms,
            },
        }),
    );
    assert!(ok);
    assert_eq!(consumed_samples, tail.len() as u64);
    // The tail is the take's own audio: no later pass, no post-bounce pass, TRACE to the end.
    assert_eq!(tracker.keep_passes(GENERATION), Vec::new());
    assert!(!tracker.keep_trace_first_pass_only(GENERATION));
    let trace = drain_record_trace_queue(&queue);
    let last_measured = trace
        .iter()
        .filter(|sample| sample.has_measured_core())
        .filter_map(|sample| sample.position_samples)
        .max();
    assert!(
        last_measured.is_some_and(|end| end + 100 * SR as i64 / 1_000 >= callbacks * BLOCK),
        "the TRACE must reach the tail's last 100 ms slot, got {last_measured:?}"
    );
}

#[test]
fn a_loop_pass_after_the_first_epoch_left_the_ring_still_reaches_the_keep_summary() {
    let callbacks = CAPTURE_CLOCK_SPAN_CAPACITY as i64 + 200;
    let tracker = long_keep(callbacks);
    let selected = tracker.selected_capture_epoch(GENERATION).unwrap();
    let consumed = (callbacks * BLOCK) as u64;
    let take_epoch = tracker
        .clock_point_for_captured_frame(consumed)
        .unwrap()
        .epoch;
    // The host wraps back to the loop start.
    record_callback(&tracker, callbacks, 0);
    let plan = capture_chunk_plan(&tracker, consumed, BLOCK as usize * 2, 2, SR);
    assert_ne!(plan.capture_epoch, Some(take_epoch));
    assert_eq!(
        chunk_route(&tracker, Some(selected), Some(take_epoch), plan, false),
        RecordChunkRoute::KeepPass
    );
}
