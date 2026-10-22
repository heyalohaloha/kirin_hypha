use super::*;
use crate::{
    AuxiliaryClockSamples, AuxiliaryClockSource, CaptureClockSource, PresentationLatencySamples,
    PresentationLatencySource, RecordTakeBlock, RecordTakeTracker,
};

#[test]
fn a_selected_anchor_keeps_producer_proven_auxiliary_continuations() {
    let tracker = RecordTakeTracker::new();
    for (position, auxiliary) in [(100_000, 0), (104_800, 4_801)] {
        tracker.note_block(RecordTakeBlock {
            generation: 1,
            recording: true,
            rendered: true,
            playing: true,
            offline: false,
            position_valid: true,
            position_samples: position,
            num_frames: 4_800,
            clock_start_samples: 0,
            clock_end_samples: None,
        });
        tracker.note_capture_window_with_clocks_boundary(
            true,
            position,
            4_800,
            CaptureClockSource::ProjectTimeline,
            PresentationLatencySamples {
                source: PresentationLatencySource::Vst3,
                input: Some(0),
                output: Some(0),
            },
            AuxiliaryClockSamples {
                source: AuxiliaryClockSource::Vst3Continuous,
                samples: Some(auxiliary),
            },
            false,
        );
    }
    let selected = tracker.selected_capture_epoch(1).unwrap();
    let first = tracker.clock_point_for_captured_frame(4_800).unwrap();
    let second = tracker.clock_point_for_captured_frame(9_600).unwrap();
    assert_ne!(first.epoch, second.epoch);
    assert!(tracker.capture_epochs_are_latency_continuation(first.epoch, second.epoch));
    assert!(tracker
        .record_audio_prefix(selected, 1)
        .unwrap()
        .matches(second));
    let mut data = data(vec![
        observation(100, 104_800, 0, first.epoch),
        observation(200, 109_600, 0, second.epoch),
    ]);
    data.trace_selected_capture_epoch = Some(selected);
    data.raw_host_clock_range = Some(crate::plugin_data::HostClockRange {
        start_position_samples: 100_000,
        end_position_samples: 109_600,
    });
    let resolved = resolve_exact_side(&data, &expected(Some(100_000)), 2, 4_800).unwrap();
    assert_eq!(resolved.frames.len(), 2);
    assert_eq!(resolved.producer_slots, [104_800, 109_600]);
    let mut strict = data.clone();
    strict
        .trace_clock_observations
        .retain(|observation| observation.capture_epoch == Some(selected));
    let strict_coverage = resolve_exact_side(&strict, &expected(Some(100_000)), 2, 4_800)
        .map_or(0, |side| side.frames.len());
    assert!(
        strict_coverage < resolved.frames.len(),
        "exact epoch equality would drop a producer-proven continuation"
    );
}

#[test]
fn observations_of_a_replaced_realtime_pass_never_block_the_offline_take() {
    replaced_pass_observations("vst3");
}

#[test]
fn audio_unit_observations_of_a_replaced_realtime_pass_never_block_the_offline_take() {
    replaced_pass_observations("audio_unit_v2");
}

fn replaced_pass_observations(source: &str) {
    let old = (1..=15).map(|slot| observation(slot * 100, 1_368_000 + slot as i64 * 4_800, 256, 1));
    let take = (1..=300).map(|slot| observation(slot * 100, slot as i64 * 4_800, 256, 4));
    let mut record = data(old.chain(take).collect());
    for observation in &mut record.trace_clock_observations {
        observation.presentation_latency_source = Some(source.into());
    }
    record.frames = record
        .trace_clock_observations
        .iter()
        .filter(|observation| observation.capture_epoch == Some(4))
        .map(|observation| observation.frame.clone())
        .collect();
    record.raw_host_clock_range = Some(crate::plugin_data::HostClockRange {
        start_position_samples: 0,
        end_position_samples: 1_440_000,
    });
    let take = record.bounce_take.as_mut().unwrap();
    take.wav_end_sample = 1_440_000;
    take.duration_samples = 1_440_000;
    take.duration_frames_48k = 1_440_000;
    take.end_t_ms = 30_000;
    take.trace_sample_count = 300;
    take.frame_count = 300;
    take.host_start_position_samples = Some(0);
    take.host_end_position_samples = Some(1_440_000);
    let mut expected = expected(Some(0));
    expected.expected_duration_samples = 1_440_000;
    // An old record has no take anchor and retains its historical sparse resolution.
    let legacy = resolve_exact_side(&record, &expected, 300, 4_800).unwrap();
    assert_eq!(legacy.model, "raw_host_position");
    assert_eq!(legacy.frames.len(), 300);
    assert!(legacy.comparison_slots.is_empty());
    record.trace_selected_capture_epoch = Some(4);
    record.record_session_id = Some("session".into());
    let resolved = resolve_exact_side(&record, &expected, 300, 4_800).unwrap();
    assert_eq!(resolved.model, "producer_plus_output_latency", "{source}");
    assert_eq!(resolved.frames.len(), 300);
    assert_eq!(resolved.comparison_slots.len(), 300);
    assert_eq!(resolved.comparison_slots.first(), Some(&4_544));
    assert_eq!(resolved.comparison_slots.last(), Some(&1_439_744));
    let mut post = record.clone();
    post.role = crate::plugin_data::Role::Post;
    let plan = crate::trace_content_clock::build_wav_start_clock_plan(
        &record, &post, &expected, 300, 4_800,
    )
    .unwrap();
    assert_eq!(
        plan.comparison_resolution,
        Some(crate::trace_content_clock::TRACE_COMPARISON_PRODUCER_CONTENT_EXACT)
    );
    assert_eq!(plan.pre_comparison_slots.len(), 300);
    assert_eq!(plan.post_comparison_slots.len(), 300);
    assert_eq!(
        record.trace_clock_observations.len(),
        315,
        "the journal stays immutable"
    );
}
