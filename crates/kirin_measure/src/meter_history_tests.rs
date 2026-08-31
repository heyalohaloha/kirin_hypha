use super::*;

fn point_value(value: f64) -> MeasureResult {
    MeasureResult {
        lufs_m: Some(value),
        lufs_s: Some(value - 1.0),
        true_peak: Some(value + 10.0),
        ..MeasureResult::default()
    }
}

#[test]
fn product_capacities_match_ten_minutes_two_hours_and_twenty_four_hours() {
    assert_eq!(HISTORY_10_HZ_CAPACITY, 6_000);
    assert_eq!(HISTORY_1_HZ_CAPACITY, 7_200);
    assert_eq!(HISTORY_0_1_HZ_CAPACITY, 8_640);
}

#[test]
fn one_second_bucket_keeps_min_max_mean_and_exact_endpoints() {
    let mut history = MeterHistory::with_config(20, 20, 20, 10, 100);
    for index in 0..10_u64 {
        history.push(
            1,
            4,
            (index + 1) * 4_800,
            (
                Some(10_000 + ((index + 1) * 4_800) as i64),
                CaptureClockSource::ProjectTimeline,
            ),
            &point_value(index as f64),
            MeterHistoryAux {
                correlation: Some(index as f64 / 10.0),
                plr: Some(10.0 + index as f64),
            },
        );
    }
    let entries = history.recent(MeterHistoryResolution::Hz1, 10);
    assert_eq!(entries.len(), 1);
    let entry = entries[0];
    assert_eq!(entry.observation_count, 10);
    assert_eq!(entry.first_observed_frames, 4_800);
    assert_eq!(entry.last_observed_frames, 48_000);
    assert_eq!(entry.first_timeline_endpoint_samples, Some(14_800));
    assert_eq!(entry.last_timeline_endpoint_samples, Some(58_000));
    assert_eq!(entry.lufs_m.min, Some(0.0));
    assert_eq!(entry.lufs_m.max, Some(9.0));
    assert_eq!(entry.lufs_m.mean, Some(4.5));
    assert_eq!(entry.plr.min, Some(10.0));
    assert_eq!(entry.plr.max, Some(19.0));
    assert_eq!(entry.plr.mean, Some(14.5));
}

#[test]
fn run_change_flushes_partial_bucket_instead_of_joining_a_seek() {
    let mut history = MeterHistory::with_config(20, 20, 20, 10, 100);
    for index in 0..4_u64 {
        history.push(
            1,
            1,
            index + 1,
            (Some(index as i64), CaptureClockSource::ProjectTimeline),
            &point_value(1.0),
            MeterHistoryAux::default(),
        );
    }
    history.push(
        1,
        2,
        5,
        (Some(50_000), CaptureClockSource::ProjectTimeline),
        &point_value(2.0),
        MeterHistoryAux::default(),
    );
    let entries = history.recent(MeterHistoryResolution::Hz1, 10);
    assert_eq!(entries.len(), 2);
    assert_eq!(entries[0].run_id, 1);
    assert_eq!(entries[0].observation_count, 4);
    assert_eq!(entries[1].run_id, 2);
    assert_eq!(entries[1].observation_count, 1);
}

#[test]
fn fixed_capacity_drops_only_the_oldest_entry_and_reset_clears_every_tier() {
    let mut history = MeterHistory::with_config(2, 2, 2, 1, 1);
    for index in 0..3_u64 {
        history.push(
            1,
            1,
            index + 1,
            (None, CaptureClockSource::Unknown),
            &point_value(index as f64),
            MeterHistoryAux::default(),
        );
    }
    let exact = history.recent(MeterHistoryResolution::Hz10, 10);
    assert_eq!(exact.len(), 2);
    assert_eq!(exact[0].last_observed_frames, 2);
    assert_eq!(exact[1].last_observed_frames, 3);
    assert_eq!(history.recent(MeterHistoryResolution::Hz1, 10).len(), 2);
    assert_eq!(history.recent(MeterHistoryResolution::Hz0_1, 10).len(), 2);
    history.reset();
    assert!(history.recent(MeterHistoryResolution::Hz10, 10).is_empty());
    assert!(history.recent(MeterHistoryResolution::Hz1, 10).is_empty());
    assert!(history.recent(MeterHistoryResolution::Hz0_1, 10).is_empty());
}

#[test]
fn full_day_tier_is_bounded_to_pixels_without_losing_endpoints_or_extrema() {
    let mut history = MeterHistory::with_config(8_640, 8_640, 8_640, 1, 1);
    for index in 0..8_640_u64 {
        history.push(
            1,
            1,
            index + 1,
            (Some(index as i64), CaptureClockSource::ProjectTimeline),
            &point_value(index as f64),
            MeterHistoryAux::default(),
        );
    }
    let display = history.recent_decimated(MeterHistoryResolution::Hz0_1, 8_640, 720);
    assert_eq!(display.len(), 720);
    assert_eq!(display.first().unwrap().first_observed_frames, 1);
    assert_eq!(display.last().unwrap().last_observed_frames, 8_640);
    assert_eq!(display.first().unwrap().lufs_m.min, Some(0.0));
    assert_eq!(display.last().unwrap().lufs_m.max, Some(8_639.0));
    assert!(display.iter().all(|entry| entry.observation_count == 12));
}

#[test]
fn decimation_keeps_every_observation_when_run_boundaries_split_buckets() {
    let mut history = MeterHistory::with_config(20, 20, 20, 10, 100);
    for index in 0..6_u64 {
        history.push(
            1,
            if index < 3 { 1 } else { 2 },
            index + 1,
            (Some(index as i64), CaptureClockSource::ProjectTimeline),
            &point_value(index as f64),
            MeterHistoryAux::default(),
        );
    }
    let display = history.recent_decimated(MeterHistoryResolution::Hz10, 6, 3);
    assert_eq!(display.len(), 3);
    assert_eq!(
        display
            .iter()
            .map(|entry| entry.observation_count)
            .sum::<u16>(),
        6
    );
    assert_eq!(display.first().unwrap().run_id, 1);
    assert_eq!(display.last().unwrap().run_id, 2);
    assert_eq!(
        display
            .iter()
            .filter_map(|entry| entry.lufs_m.min)
            .reduce(f64::min),
        Some(0.0)
    );
    assert_eq!(
        display
            .iter()
            .filter_map(|entry| entry.lufs_m.max)
            .reduce(f64::max),
        Some(5.0)
    );
}

#[test]
fn decimation_keeps_recent_runs_separate_when_runs_outnumber_pixels() {
    let mut history = MeterHistory::with_config(20, 20, 20, 10, 100);
    for index in 0..6_u64 {
        history.push(
            1,
            index + 1,
            index + 1,
            (Some(index as i64), CaptureClockSource::ProjectTimeline),
            &point_value(index as f64),
            MeterHistoryAux::default(),
        );
    }
    let display = history.recent_decimated(MeterHistoryResolution::Hz10, 6, 3);
    assert_eq!(display.len(), 3);
    assert_eq!(
        display.iter().map(|entry| entry.run_id).collect::<Vec<_>>(),
        vec![4, 5, 6]
    );
    assert!(display.iter().all(|entry| entry.observation_count == 1));
    assert_eq!(display.first().unwrap().first_observed_frames, 4);
    assert_eq!(display.last().unwrap().last_observed_frames, 6);
}
