use super::*;
use crate::record_take::RecordTakeBlock;
use crate::record_writer::{drain_record_trace_queue, new_record_trace_queue};
use crate::{AuxiliaryClockSamples, AuxiliaryClockSource};

fn callback(tracker: &RecordTakeTracker, position: i64, frames: u64, offline: bool, wrap: bool) {
    tracker.note_block(RecordTakeBlock {
        generation: 1,
        recording: true,
        rendered: true,
        playing: true,
        offline,
        position_valid: true,
        position_samples: position,
        num_frames: frames,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    tracker.stage_host_loop_wrap(wrap);
    tracker.note_capture_window_with_clocks_boundary(
        true,
        position,
        frames,
        CaptureClockSource::ProjectTimeline,
        PresentationLatencySamples::default(),
        AuxiliaryClockSamples {
            source: AuxiliaryClockSource::AaxNative,
            samples: Some(tracker.captured_frames_total() as i64),
        },
        false,
    );
}

#[test]
fn an_offline_take_after_realtime_passes_gets_its_own_trace_timeline() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, 1_368_000, 72_000, false, false);
    let first_epoch = tracker.selected_capture_epoch(1).unwrap();
    let first = capture_plan::capture_chunk_plan(&tracker, 0, 144_000, 2, 48_000);
    tracker.note_keep_chunk(first.keep_chunk(1, 72_000, RecordChunkRoute::Take, true));
    for _ in 0..2 {
        let before = tracker.captured_frames_total();
        callback(&tracker, 0, 1_440_000, false, true);
        let plan = capture_plan::capture_chunk_plan(&tracker, before, 2_880_000, 2, 48_000);
        tracker.note_keep_chunk(plan.keep_chunk(1, 1_440_000, RecordChunkRoute::KeepPass, true));
    }
    let before = tracker.captured_frames_total();
    callback(&tracker, 0, 1_440_000, true, false);
    let selected = tracker.selected_capture_epoch(1).unwrap();
    assert_ne!(selected, first_epoch);
    let plan = capture_plan::capture_chunk_plan(&tracker, before, 9_600, 2, 48_000);
    let queue = new_record_trace_queue();
    let result = MeasureResult {
        computed: true,
        lufs_m: Some(-12.0),
        true_peak: Some(-3.0),
        crest: Some(9.0),
        ..Default::default()
    };
    let mut core = VecDeque::from([PendingRecordCore {
        frames_48k: 72_000,
        native_frames: 72_000,
        position_samples: Some(1_440_000),
        raw_host_position_samples: Some(1_440_000),
        capture_epoch: Some(first_epoch),
        clock_source: CaptureClockSource::ProjectTimeline,
        presentation_latency: plan.presentation_latency,
        observed_silence_floor: false,
        result: result.clone(),
    }]);
    // Seed a nonempty old pass, including residual cursors, to catch partial axis resets.
    let (mut origin_frames, mut origin_native, mut offset, mut native_offset) = (0, 0, 42, 44);
    let mut origin_position = Some(1_368_000);
    let mut epoch = Some(first_epoch);
    let mut anchor = Some(first_epoch);
    let (mut next_trace, mut next_psb) = (1_500, 1_500);
    let mut grid = Some(1_440_000);
    let mut grid_end = Some(1_444_800);
    let mut resampler = TraceResampler::new(48_000, 2).unwrap();
    let mut phase = PhaseDChannelStream::new(FieldType::Free, 2);
    let (mut phase_pending, mut phases, mut latest_pd) = (Vec::new(), VecDeque::new(), None);
    let timeline = TraceTakeBoundary {
        queue: &queue,
        tracker: &tracker,
        generation: 1,
        sample_rate: 48_000,
        n_channels: 2,
        origin_frames: &mut origin_frames,
        origin_native_frames: &mut origin_native,
        origin_position: &mut origin_position,
        anchor_epoch: &mut anchor,
        capture_epoch: &mut epoch,
        offset_frames: &mut offset,
        offset_native_frames: &mut native_offset,
        next_trace_ms: &mut next_trace,
        next_psb_ms: &mut next_psb,
        grid_cursor: &mut grid,
        next_grid_end: &mut grid_end,
        resampler: &mut resampler,
        phase_pending: &mut phase_pending,
        phase_d: &mut phase,
        latest_pd: &mut latest_pd,
        core: &mut core,
        phases: &mut phases,
    }
    .prepare(true, true, Some(selected), plan, before);
    tracker.note_keep_chunk(plan.keep_chunk(1, 1_440_000, RecordChunkRoute::Take, true));
    for slot in 1..=300 {
        let position = slot * 4_800;
        assert!(maybe_push_record_trace(
            &queue,
            timeline,
            &mut RecordTraceCursor {
                next_trace_ms: &mut next_trace,
                next_psb_ms: &mut next_psb
            },
            48_000,
            RecordTraceObserved {
                frames_48k: before + position,
                native_frames: Some(before + position),
                position_samples: Some(position as i64),
                raw_host_position_samples: Some(position as i64),
                capture_epoch: Some(selected),
                clock_source: CaptureClockSource::ProjectTimeline,
                presentation_latency: plan.presentation_latency,
                observed_silence_floor: false,
            },
            &result,
        ));
    }
    let samples = drain_record_trace_queue(&queue);
    assert!(
        samples
            .iter()
            .any(|sample| sample.capture_epoch == Some(first_epoch) && sample.t_ms == 1_500),
        "close the old tail before changing its axis"
    );
    let take: Vec<_> = samples
        .iter()
        .filter(|sample| sample.capture_epoch == Some(selected))
        .collect();
    assert_eq!(take.len(), 300);
    assert_eq!(take[0].t_ms, 100);
    assert_eq!(take.last().unwrap().t_ms, 30_000);
    assert!(take.windows(2).all(|pair| pair[0].t_ms < pair[1].t_ms));
    assert_eq!(take[0].position_samples, Some(4_800));
    assert_eq!(take.last().unwrap().position_samples, Some(1_440_000));
    assert_sparse_resolution_preserves_the_offline_take(&samples);
    let passes = tracker.keep_passes(1);
    assert_eq!(passes.iter().filter(|pass| pass.in_trace).count(), 1);
    assert_eq!(
        passes[0].trace_excluded_reason.as_deref(),
        Some("replaced_by_offline_take")
    );
}

#[test]
fn the_first_take_chunk_anchors_to_selection_without_replacing_its_prefix() {
    let tracker = RecordTakeTracker::new();
    callback(&tracker, 95_200, 4_800, false, false);
    callback(&tracker, 100_000, 4_800, false, false);
    let selected = tracker.selected_capture_epoch(1).unwrap();
    let plan = capture_plan::capture_chunk_plan(&tracker, 4_800, 9_600, 2, 48_000);
    tracker.note_keep_chunk(plan.keep_chunk(1, 4_800, RecordChunkRoute::Take, true));
    let queue = new_record_trace_queue();
    let (mut origin, mut native, mut offset, mut native_offset) = (0, 0, 0, 0);
    let mut position = Some(95_200);
    let mut anchor = None;
    // The temporary capture cursor can still describe a pre-roll prefix at the first take chunk.
    let mut epoch = None;
    let (mut trace_ms, mut psb_ms) = (100, 100);
    let (mut grid, mut grid_end) = (None, None);
    let mut resampler = TraceResampler::new(48_000, 2).unwrap();
    let mut phase = PhaseDChannelStream::new(FieldType::Free, 2);
    let (mut phase_pending, mut phases, mut latest_pd, mut core) =
        (Vec::new(), VecDeque::new(), None, VecDeque::new());
    let timeline = TraceTakeBoundary {
        queue: &queue,
        tracker: &tracker,
        generation: 1,
        sample_rate: 48_000,
        n_channels: 2,
        origin_frames: &mut origin,
        origin_native_frames: &mut native,
        origin_position: &mut position,
        anchor_epoch: &mut anchor,
        capture_epoch: &mut epoch,
        offset_frames: &mut offset,
        offset_native_frames: &mut native_offset,
        next_trace_ms: &mut trace_ms,
        next_psb_ms: &mut psb_ms,
        grid_cursor: &mut grid,
        next_grid_end: &mut grid_end,
        resampler: &mut resampler,
        phase_pending: &mut phase_pending,
        phase_d: &mut phase,
        latest_pd: &mut latest_pd,
        core: &mut core,
        phases: &mut phases,
    }
    .prepare(true, true, Some(selected), plan, 4_800);
    assert_eq!(anchor, Some(selected));
    assert_eq!(
        timeline.origin_native_frames, 0,
        "preserve the prefix's sample axis"
    );
    assert_eq!(trace_ms, 100);
    let passes = tracker.keep_passes(1);
    assert!(
        passes.is_empty(),
        "an unreplaced single pass needs no exclusion journal"
    );
}

fn assert_sparse_resolution_preserves_the_offline_take(
    samples: &[crate::record_writer::RecordTraceSample],
) {
    use crate::plugin_data::{BounceTake, Frame, PluginDataFile, Role, TraceClockObservation};
    let mut data = PluginDataFile::new(
        "fixture".into(),
        "project".into(),
        "pre".into(),
        Role::Pre,
        None,
        48_000,
        None,
        None,
        None,
        None,
    );
    data.trace_clock_observations = samples
        .iter()
        .filter(|sample| sample.kind == crate::record_writer::RecordTraceKind::Measured)
        .map(|sample| TraceClockObservation {
            frame: Frame {
                t_ms: sample.t_ms,
                n_prime: sample.result.n_prime,
                n_prime_total: sample.result.n_prime_total,
                sharpness: sample.result.sharpness,
                lufs_m: sample.result.lufs_m.unwrap(),
                true_peak: sample.result.true_peak.unwrap(),
                crest: sample.result.crest.unwrap(),
                psr: sample.result.psr,
            },
            producer_position_samples: sample.position_samples,
            raw_host_position_samples: sample.raw_host_position_samples,
            capture_epoch: sample.capture_epoch,
            clock_source: sample.clock_source.as_str().map(str::to_string),
            presentation_latency_source: None,
            input_presentation_latency_samples: None,
            output_presentation_latency_samples: None,
        })
        .collect();
    data.bounce_take = Some(BounceTake {
        source: "render_clock_native".into(),
        time_axis: "native_samples".into(),
        alignment_status: "sample_count_ready".into(),
        sample_rate: 48_000,
        wav_start_sample: 0,
        wav_end_sample: 1_440_000,
        duration_samples: 1_440_000,
        duration_frames_48k: 1_440_000,
        start_t_ms: 0,
        end_t_ms: 30_000,
        trace_sample_count: 300,
        frame_count: 300,
        host_start_position_samples: Some(0),
        host_end_position_samples: Some(1_440_000),
    });
    let expected = crate::record_expected::ExpectedWavMetadata {
        expected_duration_samples: 1_440_000,
        expected_sample_rate: 48_000,
        wav_time_reference_samples: None,
        wav_path: "fixture.wav".into(),
        bounce_id: "offline".into(),
        created_at_ms: 1,
        wav_file_size: None,
        wav_mtime_ms: 1,
        wav_hash: None,
        consumed_at_ms: None,
        consumed_by_session_id: None,
    };
    let resolved = crate::trace_clock_resolution::resolve_exact_side(&data, &expected, 300, 4_800)
        .expect("the native producer axis resolves without presentation latency");
    assert_eq!(resolved.model, "producer_position");
    assert_eq!(resolved.frames.len(), 300);
    assert_eq!(resolved.wav_slots.first(), Some(&4_800));
    assert_eq!(resolved.wav_slots.last(), Some(&1_440_000));
}
