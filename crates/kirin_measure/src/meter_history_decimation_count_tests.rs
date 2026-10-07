use super::*;
use crate::CaptureClockSource;

fn row(count: u16, start: u64, mean: f64) -> MeterHistoryEntry {
    let end = start + (u64::from(count) - 1) * 4800;
    MeterHistoryEntry {
        resolution: MeterHistoryResolution::Hz0_1,
        measurement_epoch: 1,
        generation: 1,
        run_id: 1,
        observation_count: count,
        segment_id: 1,
        connects_previous: true,
        valid_count: [count, 0, 0, 0, count],
        first_observed_frames: start,
        last_observed_frames: end,
        first_timeline_endpoint_samples: Some(start as i64),
        last_timeline_endpoint_samples: Some(end as i64),
        timeline_source: CaptureClockSource::ProjectTimeline,
        clip_event_count: [0; METER_HISTORY_CHANNELS],
        lufs_m: MeterHistoryRange {
            min: Some(mean),
            max: Some(mean),
            mean: Some(mean),
        },
        lufs_s: MeterHistoryRange::default(),
        true_peak: MeterHistoryRange::default(),
        correlation: MeterHistoryRange::default(),
        psr: MeterHistoryRange {
            min: Some(mean),
            max: Some(mean),
            mean: Some(mean),
        },
    }
}

#[test]
fn exact_65535_is_representable_but_65536_is_not_and_legacy_is_unchanged() {
    let a = row(65534, 4800, 10.0);
    let b = row(1, a.last_observed_frames + 4800, 40.0);
    let exact =
        checked_decimate_history([a, b].into_iter(), 2, 1, MeterHistoryResolution::Hz0_1).unwrap();
    assert_eq!(exact[0].observation_count, 65535);
    assert_eq!(exact[0].valid_count, [65535, 0, 0, 0, 65535]);
    assert!((exact[0].lufs_m.mean.unwrap() - (655340.0 + 40.0) / 65535.0).abs() < 1.0e-12);
    let c = row(2, a.last_observed_frames + 4800, 40.0);
    assert_eq!(
        checked_decimate_history([a, c].into_iter(), 2, 1, MeterHistoryResolution::Hz0_1),
        Err(TimeHistoryCountOverflow)
    );
    let legacy = decimate_history([a, c].into_iter(), 2, 1, MeterHistoryResolution::Hz0_1);
    assert_eq!(legacy[0].observation_count, 65535);
    assert_eq!(legacy[0].valid_count[0], 65535);
    assert!((legacy[0].lufs_m.mean.unwrap() - (655340.0 + 80.0) / 65536.0).abs() < 1.0e-12);
}

#[test]
fn a_second_checked_reduction_cannot_reuse_saturated_coarse_weights() {
    let a = row(40000, 4800, 10.0);
    let b = row(30000, a.last_observed_frames + 4800, 30.0);
    let coarse =
        checked_decimate_history([a, b].into_iter(), 2, 2, MeterHistoryResolution::Hz0_1).unwrap();
    assert_eq!(
        coarse
            .iter()
            .map(|r| r.observation_count)
            .collect::<Vec<_>>(),
        [40000, 30000]
    );
    assert_eq!(
        checked_decimate_history(coarse.into_iter(), 2, 1, MeterHistoryResolution::Hz0_1),
        Err(TimeHistoryCountOverflow)
    );
}
