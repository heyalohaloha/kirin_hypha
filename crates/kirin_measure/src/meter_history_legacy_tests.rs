//! Direct old-poll regressions: cadence, physical span and arithmetic stay independent of V2.
use super::*;

#[test]
fn sparse_ten_minutes_keep_the_full_legacy_span_and_every_legacy_bucket_value() {
    let mut history = MeterHistory::new();
    history.set_step_frames(4800);
    for slot in 1..=6000u64 {
        let value = ((slot - 1) % 8 < 2).then_some(slot as f64);
        history.push(
            11,
            1,
            1,
            slot * 4800,
            (
                Some(slot as i64 * 4800),
                CaptureClockSource::ProjectTimeline,
            ),
            &MeasureResult {
                lufs_m: value,
                lufs_s: value.map(|v| v - 1.0),
                true_peak: value.map(|v| v + 10.0),
                psr: value.map(|v| v + 20.0),
                ..Default::default()
            },
            MeterHistoryAux {
                correlation: value.map(|v| v / 10000.0),
                clip_event_count: [u32::from(value.is_some()); METER_HISTORY_CHANNELS],
            },
        );
    }
    let legacy = history.recent_decimated(MeterHistoryResolution::Hz10, 6000, 600);
    assert_eq!(legacy.len(), 600);
    assert_eq!(legacy[0].first_observed_frames, 4800);
    assert_eq!(legacy[599].last_observed_frames, 6000 * 4800);
    assert_eq!(
        (legacy[599].last_observed_frames - legacy[0].first_observed_frames) as f64 / 48000.0,
        599.9
    );
    assert_eq!(
        legacy
            .iter()
            .map(|p| u32::from(p.observation_count))
            .sum::<u32>(),
        6000
    );
    for (bucket, row) in legacy.iter().enumerate() {
        // Each old-poll pixel covers ten consecutive 100 ms observations. This oracle
        // computes directly from the specified 0.2 s sound / 0.6 s silence fixture.
        let values: Vec<_> = (bucket as u64 * 10 + 1..=bucket as u64 * 10 + 10)
            .filter(|slot| (slot - 1) % 8 < 2)
            .map(|slot| slot as f64)
            .collect();
        let mean = values.iter().sum::<f64>() / values.len() as f64;
        assert_eq!(row.observation_count, 10);
        assert_eq!(row.valid_count, [values.len() as u16; 5]);
        assert_eq!(
            row.clip_event_count,
            [values.len() as u32; METER_HISTORY_CHANNELS]
        );
        for (range, offset) in [
            (row.lufs_m, 0.0),
            (row.lufs_s, -1.0),
            (row.true_peak, 10.0),
            (row.psr, 20.0),
        ] {
            assert_eq!(range.min, Some(values[0] + offset));
            assert_eq!(range.max, Some(values[values.len() - 1] + offset));
            assert_eq!(range.mean, Some(mean + offset));
        }
        assert!((row.correlation.mean.unwrap() - mean / 10000.0).abs() < 1e-12);
        assert_eq!(
            row.segment_id, 0,
            "mixed old-poll pixels have no V2 continuity claim"
        );
    }
    let time = history
        .time_range(MeterHistoryResolution::Hz10, 0, 6000 * 4800, 600)
        .unwrap();
    assert_eq!(time.len(), 600);
    assert!(time[0].first_observed_frames > 4800);
    assert!(time
        .windows(2)
        .all(|p| { p[0].segment_id != p[1].segment_id && !p[1].connects_previous }));
    assert!(time
        .iter()
        .any(|p| p.valid_count[0] == 0 && p.lufs_m.mean.is_none()));

    // Coarse legacy pixels use observation-weighted means of the ten 1 Hz buckets,
    // including buckets with different finite counts, exactly as the previous poll did.
    let coarse = history.recent_decimated(MeterHistoryResolution::Hz1, 600, 60);
    assert_eq!(coarse.len(), 60);
    for (index, row) in coarse.iter().enumerate() {
        let old_mean = legacy[index * 10..index * 10 + 10]
            .iter()
            .map(|p| p.lufs_m.mean.unwrap())
            .sum::<f64>()
            / 10.0;
        assert_eq!(row.observation_count, 100);
        assert!((row.lufs_m.mean.unwrap() - old_mean).abs() < 1e-12);
    }
}

#[test]
fn legacy_and_v2_reductions_use_their_own_denominators_and_source_boundaries() {
    let mut history = MeterHistory::new();
    history.set_step_frames(100);
    for slot in 1..=2 {
        history.push(
            11,
            1,
            1,
            slot * 100,
            (Some(slot as i64 * 100), CaptureClockSource::ProjectTimeline),
            &MeasureResult {
                lufs_m: Some(slot as f64 * 10.0),
                ..Default::default()
            },
            MeterHistoryAux::default(),
        );
    }
    let mut rows = history.recent(MeterHistoryResolution::Hz10, 2);
    for row in &mut rows {
        row.observation_count = 10;
    }
    rows[0].valid_count[0] = 1;
    rows[1].valid_count[0] = 3;
    let old = decimate_history(rows.iter().copied(), 2, 1, MeterHistoryResolution::Hz1);
    let time =
        checked_decimate_history(rows.iter().copied(), 2, 1, MeterHistoryResolution::Hz1).unwrap();
    assert_eq!(old[0].observation_count, 20);
    assert_eq!(old[0].lufs_m.mean, Some(15.0));
    assert_eq!(time[0].valid_count[0], 4);
    assert_eq!(time[0].lufs_m.mean, Some(17.5));
    rows[1].measurement_epoch += 1;
    let old = decimate_history(rows.iter().copied(), 2, 1, MeterHistoryResolution::Hz1);
    let time =
        checked_decimate_history(rows.into_iter(), 2, 1, MeterHistoryResolution::Hz1).unwrap();
    assert_eq!(old[0].observation_count, 20);
    assert_eq!(old[0].first_observed_frames, 100);
    assert_eq!(time[0].observation_count, 10);
    assert_eq!(time[0].first_observed_frames, 200);
}
