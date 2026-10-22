use super::*;

fn observation(slot: u64, position: i64, epoch: u64) -> TraceClockObservation {
    TraceClockObservation {
        frame: Frame {
            t_ms: slot * 100,
            n_prime: None,
            n_prime_total: None,
            sharpness: None,
            lufs_m: -18.0,
            true_peak: -1.0,
            crest: 7.0,
            psr: None,
        },
        producer_position_samples: Some(position),
        raw_host_position_samples: Some(position),
        capture_epoch: Some(epoch),
        clock_source: Some("project_timeline".into()),
        presentation_latency_source: Some("vst3".into()),
        input_presentation_latency_samples: Some(0),
        output_presentation_latency_samples: Some(0),
    }
}

#[test]
fn selected_take_coverage_ignores_replaced_epochs_and_keeps_later_continuations() {
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
    let take = (1..=300)
        .map(|slot| observation(slot, slot as i64 * 4_800, if slot <= 150 { 4 } else { 5 }))
        .collect::<Vec<_>>();
    data.frames = take
        .iter()
        .map(|observation| observation.frame.clone())
        .collect();
    data.trace_slot_positions = (1..=300).map(|slot| slot * 4_800).collect();
    data.trace_clock = Some(TraceClock {
        basis: crate::trace_alignment::TRACE_CLOCK_BASIS.into(),
        origin_position_samples: 0,
        end_position_samples: 1_440_000,
        sample_rate: 48_000,
        sources: vec!["project_timeline".into()],
    });
    data.trace_clock_resolution = Some("producer_plus_output_latency".into());
    // These old lap endpoints overlap the final fifteen slots of the selected thirty seconds.
    data.trace_clock_observations = (1..=15)
        .map(|slot| observation(slot, 1_368_000 + slot as i64 * 4_800, 1))
        .chain(take)
        .collect();
    data.trace_selected_capture_epoch = Some(4);
    let coverage = latency_mapped_trace_coverage(&data, 1_440_000, 48_000)
        .expect("thirty seconds of selected producer observations remain unambiguous");
    assert_eq!(coverage.expected_frame_count, 300);
    assert_eq!(coverage.measured_frame_count, 300);
    assert_eq!(coverage.missing_slots, 0);
    assert_eq!(data.trace_clock_observations.len(), 315);
    data.trace_selected_capture_epoch = None;
    assert!(
        latency_mapped_trace_coverage(&data, 1_440_000, 48_000).is_none(),
        "legacy records retain their prior ambiguous-journal behavior"
    );
}
