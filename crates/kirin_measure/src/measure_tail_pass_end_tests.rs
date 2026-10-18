//! A TRACE pass that ends inside the Stop tail at every supported rate other than 48 kHz (44.1 to
//! 192 kHz): the take's last 100 ms slot is emitted although the resampler still held its last
//! samples when a later pass of the Keep began, and no audio of the later pass reaches the TRACE.
use super::super::capture_plan::capture_chunk_plan;
use super::{drain_ring_into_session, DrainRingSession, MeasureSampleConsumer, RecordTraceDrain};
use super::{RecordTraceCursor, RecordTraceTimeline};
use crate::channel_layout::ChannelLayout;
use crate::engine::MeasureEngine;
use crate::phase_d::channels::PhaseDChannelStream;
use crate::phase_d::tables::FieldType;
use crate::record_take::{
    CaptureClockSource, PresentationLatencySamples, PresentationLatencySource, RecordTakeBlock,
    RecordTakeTracker,
};
use crate::record_writer::{drain_record_trace_queue, new_record_trace_queue};
use crate::resampler::ResamplerTo48k;
use crate::{AuxiliaryClockSamples, AuxiliaryClockSource};
use std::sync::{Arc, Mutex};

const GENERATION: u64 = 1;

/// 10 ms callbacks: ten of them are one 100 ms TRACE slot.
fn block(rate: u32) -> i64 {
    i64::from(rate / 100)
}

fn callback(tracker: &RecordTakeTracker, rate: u32, counter: i64, position: i64) {
    tracker.note_block(RecordTakeBlock {
        generation: GENERATION,
        recording: true,
        rendered: true,
        playing: true,
        offline: false,
        position_valid: true,
        position_samples: position,
        num_frames: block(rate) as u64,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    tracker.note_capture_window_with_clocks_boundary(
        true,
        position,
        block(rate) as u64,
        CaptureClockSource::ProjectTimeline,
        PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(0),
        },
        AuxiliaryClockSamples {
            source: AuxiliaryClockSource::Vst3Continuous,
            samples: Some(counter),
        },
        false,
    );
}

fn tone(rate: u32, frames: usize) -> Vec<f32> {
    (0..frames)
        .flat_map(|frame| {
            let sample = 0.25 * (frame as f32 * 997.0 * std::f32::consts::TAU / rate as f32).sin();
            [sample, sample]
        })
        .collect()
}

/// The positions of the measured TRACE slots that a Stop tail of `take_blocks` take callbacks
/// and `later_blocks` callbacks of a later pass (the host's LOOP back to the start) emits.
fn tail_slots(rate: u32, take_blocks: i64, later_blocks: i64) -> Vec<i64> {
    let block = block(rate);
    let tracker = RecordTakeTracker::new();
    for index in 0..take_blocks {
        callback(&tracker, rate, index * block, index * block);
    }
    for index in 0..later_blocks {
        callback(&tracker, rate, (take_blocks + index) * block, index * block);
    }
    let take_epoch = tracker.clock_point_for_captured_frame(1).unwrap().epoch;
    let later = capture_chunk_plan(&tracker, (take_blocks * block) as u64, 2, 2, rate);
    assert_ne!(
        later.capture_epoch,
        Some(take_epoch),
        "the LOOP begins a pass"
    );

    let audio = tone(rate, ((take_blocks + later_blocks) * block) as usize);
    let (mut producer, consumer) = rtrb::RingBuffer::new(audio.len());
    for sample in &audio {
        producer.push(*sample).unwrap();
    }
    let mut consumer = MeasureSampleConsumer::watch(consumer);
    let queue = new_record_trace_queue();
    let (mut next_trace_ms, mut next_psb_ms) = (0, 0);
    let mut resampler = Some(ResamplerTo48k::new(rate, 2).unwrap());
    let mut trace_engine = MeasureEngine::new(rate, ChannelLayout::stereo()).unwrap();
    let mut summary_engine = MeasureEngine::new(rate, ChannelLayout::stereo()).unwrap();
    let summary = Arc::new(Mutex::new(None));
    let mut phase = PhaseDChannelStream::new(FieldType::Free, 2);
    let (mut chunk, mut resampled, mut phase_slots) = (Vec::new(), Vec::new(), Vec::new());
    let mut latest_phase = None;
    let mut core_pending = std::collections::VecDeque::new();
    let mut phase_pending = std::collections::VecDeque::new();
    let (mut native_frames, mut consumed_samples) = (0_u64, 0_u64);
    let mut last_position_end = None;
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
            sample_rate: rate,
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
    assert_eq!(consumed_samples, audio.len() as u64);
    drain_record_trace_queue(&queue)
        .iter()
        .filter(|sample| sample.has_measured_core())
        .filter_map(|sample| sample.position_samples)
        .collect()
}

#[test]
fn a_take_that_ends_inside_the_stop_tail_keeps_its_last_slot_at_every_rate() {
    // A take of 1.1 s ends on the 100 ms grid. At none of these rates is its length a multiple of
    // the resampler's fixed input block, so the resampler still holds part of the last slot.
    let take_blocks = 110;
    for rate in [44_100, 88_200, 96_000, 176_400, 192_000] {
        let alone = tail_slots(rate, take_blocks, 0);
        let then_later = tail_slots(rate, take_blocks, 30);
        assert_eq!(
            alone.last(),
            Some(&(take_blocks * block(rate))),
            "{rate} Hz: a take that ends the tail emits its last slot"
        );
        assert_eq!(
            then_later, alone,
            "{rate} Hz: a later pass after the take changes none of the take's TRACE slots, \
             its last included"
        );
    }
}
