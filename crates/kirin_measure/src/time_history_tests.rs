use super::*;

fn point(history: &mut MeterHistory, observed: u64, value: Option<f64>) {
    history.push(
        11,
        1,
        1,
        observed,
        (Some(observed as i64), CaptureClockSource::ProjectTimeline),
        &MeasureResult {
            lufs_m: value,
            psr: value,
            ..Default::default()
        },
        MeterHistoryAux::default(),
    );
}

#[test]
fn two_hours_at_one_hz_needs_two_representable_export_buckets() {
    let mut history = MeterHistory::new();
    history.set_step_frames(4800);
    for i in 1..=72000 {
        point(
            &mut history,
            i * 4800,
            Some(if i <= 36000 { 10.0 } else { 20.0 }),
        );
    }
    assert_eq!(
        history.time_range(MeterHistoryResolution::Hz1, 0, 345600000, 1),
        Err(TimeHistoryCountOverflow)
    );
    let rows = history
        .time_range(MeterHistoryResolution::Hz1, 0, 345600000, 2)
        .unwrap();
    assert_eq!(
        rows.iter().map(|r| r.observation_count).collect::<Vec<_>>(),
        [36000, 36000]
    );
    assert_eq!(
        rows.iter().map(|r| r.valid_count[0]).collect::<Vec<_>>(),
        [36000, 36000]
    );
    assert_eq!(
        rows.iter().map(|r| r.lufs_m.mean).collect::<Vec<_>>(),
        [Some(10.0), Some(20.0)]
    );
    let legacy = history.recent_decimated(MeterHistoryResolution::Hz1, 7200, 1);
    assert_eq!(legacy[0].observation_count, 65535);
    assert_eq!(legacy[0].lufs_m.mean, Some(15.0));
    assert!(history
        .time_range(MeterHistoryResolution::Hz1, 0, 345600000, 0)
        .unwrap()
        .is_empty());
}

#[test]
fn none_and_missing_slots_break_buckets_and_decimation_never_bridges_them() {
    let mut history = MeterHistory::new();
    history.set_step_frames(100);
    point(&mut history, 100, Some(10.0));
    point(&mut history, 200, None);
    point(&mut history, 300, Some(30.0));
    point(&mut history, 500, Some(50.0));
    let legacy = history.recent(MeterHistoryResolution::Hz1, 10);
    assert_eq!(legacy.len(), 1);
    assert_eq!(legacy[0].observation_count, 4);
    assert_eq!(legacy[0].segment_id, 0);
    assert!(!legacy[0].connects_previous);
    let entries = history
        .time_range(MeterHistoryResolution::Hz1, 0, 500, 1200)
        .unwrap();
    assert_eq!(entries.len(), 4);
    assert_eq!(
        entries.iter().map(|p| p.lufs_m.mean).collect::<Vec<_>>(),
        vec![Some(10.0), None, Some(30.0), Some(50.0)]
    );
    assert!(entries
        .windows(2)
        .all(|p| p[0].segment_id != p[1].segment_id));
    assert!(entries.iter().all(|p| !p.connects_previous));
    assert_eq!(entries[1].valid_count[0], 0);
    let last = history
        .time_range(MeterHistoryResolution::Hz1, 0, 500, 1)
        .unwrap();
    assert_eq!(last.len(), 1);
    assert_eq!(last[0].lufs_m.mean, Some(50.0));
    let clipped = history
        .time_range(MeterHistoryResolution::Hz1, 300, 500, 1200)
        .unwrap();
    assert_eq!(clipped.len(), 2);
    assert_eq!(clipped[0].lufs_m.mean, Some(30.0));
    assert_eq!(clipped[1].lufs_m.mean, Some(50.0));
    assert!(!clipped[1].connects_previous);
}

#[test]
fn both_partial_boundaries_use_exact_prefix_and_missing_retention_is_a_gap() {
    let mut history = MeterHistory::new();
    history.set_step_frames(100);
    for i in 1..=10 {
        point(
            &mut history,
            i * 100,
            Some(if i > 5 { 100.0 } else { i as f64 }),
        );
    }
    let prefix = history
        .time_range(MeterHistoryResolution::Hz1, 200, 500, 1200)
        .unwrap();
    assert_eq!(prefix.len(), 1);
    assert_eq!(prefix[0].first_observed_frames, 200);
    assert_eq!(prefix[0].last_observed_frames, 500);
    assert_eq!(prefix[0].lufs_m.max, Some(5.0));
    assert_eq!(prefix[0].lufs_m.mean, Some(3.5));
    assert_eq!(prefix[0].valid_count[0], 4);
    let main = history
        .time_range(MeterHistoryResolution::Hz1, 0, 1000, 1200)
        .unwrap();
    assert_eq!(main[0].lufs_m.max, Some(100.0));
    let mut small = MeterHistory::with_config(2, 20, 20, 10, 100);
    small.set_step_frames(100);
    for i in 1..=10 {
        point(&mut small, i * 100, Some(i as f64));
    }
    assert!(small
        .time_range(MeterHistoryResolution::Hz1, 200, 500, 1200)
        .unwrap()
        .is_empty());
}

#[test]
fn valid_denominators_and_1200_budget_have_independent_expected_values() {
    let mut history = MeterHistory::new();
    history.set_step_frames(100);
    point(&mut history, 100, Some(10.0));
    point(&mut history, 200, Some(40.0));
    let mut rows = history.recent(MeterHistoryResolution::Hz10, 2);
    rows[1].observation_count = 3;
    rows[1].valid_count[0] = 2;
    let aggregate = reduce_time_history(&rows, 1, MeterHistoryResolution::Hz1).unwrap();
    assert_eq!(aggregate[0].observation_count, 4);
    assert_eq!(aggregate[0].valid_count[0], 3);
    assert_eq!(aggregate[0].lufs_m.mean, Some(30.0));
    let mut many = MeterHistory::new();
    many.set_step_frames(100);
    for i in 1..=2000 {
        point(&mut many, i * 100, (i % 2 == 0).then_some(i as f64));
    }
    let bounded = many
        .time_range(MeterHistoryResolution::Hz10, 0, 200000, 1500)
        .unwrap();
    assert_eq!(bounded.len(), 1200);
    assert_eq!(bounded[0].first_observed_frames, 80100);
    assert!(bounded
        .windows(2)
        .all(|p| p[0].segment_id != p[1].segment_id));
    let coarse = many
        .time_range(MeterHistoryResolution::Hz1, 0, 200000, 1500)
        .unwrap();
    assert_eq!(coarse.len(), 1200);
    assert_eq!(coarse[0].first_observed_frames, 80100);
    assert!(coarse.iter().all(|p| p.segment_id != 0));
}

#[test]
fn legacy_thirty_two_slots_keep_ten_ten_ten_two_and_time_splits_correlation_warmup() {
    let mut history = MeterHistory::new();
    history.set_step_frames(100);
    for slot in 1..=32 {
        history.push(
            11,
            1,
            1,
            slot * 100,
            (Some(slot as i64 * 100), CaptureClockSource::ProjectTimeline),
            &MeasureResult {
                lufs_m: Some(slot as f64),
                lufs_s: Some(slot as f64),
                true_peak: Some(slot as f64),
                psr: Some(slot as f64),
                ..Default::default()
            },
            MeterHistoryAux {
                correlation: (slot >= 30).then_some(1.0),
                ..Default::default()
            },
        );
    }
    assert_eq!(history.recent(MeterHistoryResolution::Hz10, 100).len(), 32);
    let legacy = history.recent(MeterHistoryResolution::Hz1, 100);
    assert_eq!(
        legacy
            .iter()
            .map(|p| p.observation_count)
            .collect::<Vec<_>>(),
        vec![10, 10, 10, 2]
    );
    assert_eq!(legacy[2].segment_id, 0);
    assert!(!legacy[2].connects_previous);
    assert_eq!(legacy[2].valid_count[3], 1);
    assert_eq!(legacy[3].valid_count[3], 2);
    let ten_seconds = history.recent(MeterHistoryResolution::Hz0_1, 10);
    assert_eq!(ten_seconds.len(), 1);
    assert_eq!(ten_seconds[0].observation_count, 32);
    assert_eq!(ten_seconds[0].segment_id, 0);

    let time = history
        .time_range(MeterHistoryResolution::Hz1, 0, 3200, 1200)
        .unwrap();
    assert_eq!(
        time.iter().map(|p| p.observation_count).collect::<Vec<_>>(),
        vec![10, 10, 9, 1, 2]
    );
    assert_eq!(
        time.iter().map(|p| p.correlation.mean).collect::<Vec<_>>(),
        vec![None, None, None, Some(1.0), Some(1.0)]
    );
    assert!(time.iter().all(|p| p.segment_id != 0));
    assert_ne!(time[2].segment_id, time[3].segment_id);
    assert!(!time[3].connects_previous);
    let time_ten = history
        .time_range(MeterHistoryResolution::Hz0_1, 0, 3200, 1200)
        .unwrap();
    assert_eq!(time_ten.len(), 2);
    assert_eq!(time_ten[0].observation_count, 29);
    assert_eq!(time_ten[0].lufs_m.mean, Some(15.0));
    assert_eq!(time_ten[1].observation_count, 3);
    assert_eq!(time_ten[1].lufs_m.mean, Some(31.0));
}

#[test]
fn mixed_exact_eviction_keeps_legacy_facts_but_time_cannot_invent_the_gap() {
    let mut history = MeterHistory::with_config(2, 20, 20, 10, 100);
    history.set_step_frames(100);
    for slot in 1..=10 {
        point(&mut history, slot * 100, (slot != 5).then_some(slot as f64));
    }
    let legacy = history.recent(MeterHistoryResolution::Hz1, 10);
    assert_eq!(legacy.len(), 1);
    assert_eq!(legacy[0].observation_count, 10);
    assert_eq!(legacy[0].valid_count[0], 9);
    assert_eq!(legacy[0].lufs_m.mean, Some(50.0 / 9.0));
    assert_eq!(legacy[0].segment_id, 0);
    assert!(history
        .time_range(MeterHistoryResolution::Hz1, 0, 1000, 1200)
        .unwrap()
        .is_empty());
    assert!(history
        .time_range(MeterHistoryResolution::Hz1, 900, 1000, 1200)
        .unwrap()
        .is_empty());
    assert!(
        reduce_time_history(&legacy, 1200, MeterHistoryResolution::Hz1)
            .unwrap()
            .is_empty()
    );
}

#[test]
fn mixed_partial_bucket_refines_known_none_without_the_future_suffix_mean() {
    let mut history = MeterHistory::new();
    history.set_step_frames(100);
    for slot in 1..=10 {
        point(
            &mut history,
            slot * 100,
            (slot != 5).then_some(if slot > 5 { 100.0 } else { slot as f64 }),
        );
    }
    let prefix = history
        .time_range(MeterHistoryResolution::Hz1, 200, 500, 1200)
        .unwrap();
    assert_eq!(prefix.len(), 2);
    assert_eq!(prefix[0].first_observed_frames, 200);
    assert_eq!(prefix[0].last_observed_frames, 400);
    assert_eq!(prefix[0].lufs_m.mean, Some(3.0));
    assert_eq!(prefix[0].lufs_m.max, Some(4.0));
    assert_eq!(prefix[0].valid_count[0], 3);
    assert_eq!(prefix[1].first_observed_frames, 500);
    assert_eq!(prefix[1].lufs_m.mean, None);
    assert_eq!(prefix[1].valid_count[0], 0);
    assert!(!prefix[1].connects_previous);
}

#[test]
fn external_mixed_bucket_is_rejected_without_joining_surviving_neighbours() {
    let mut history = MeterHistory::new();
    history.set_step_frames(100);
    for slot in 1..=3 {
        point(&mut history, slot * 100, Some(slot as f64));
    }
    let mut rows = history.recent(MeterHistoryResolution::Hz10, 3);
    assert_eq!(rows[0].segment_id, rows[2].segment_id);
    rows[1].segment_id = 0;
    assert!(reduce_time_history(&rows, 1, MeterHistoryResolution::Hz1)
        .unwrap()
        .is_empty());
}

#[test]
fn exact_cursor_boundaries_keep_retained_suffix_and_ignore_non_slot_cutoff() {
    let mut history = MeterHistory::with_config(2, 20, 20, 10, 100);
    history.set_step_frames(100);
    for slot in 1..=10 {
        point(&mut history, slot * 100, Some(slot as f64));
    }
    let suffix = history
        .time_range(MeterHistoryResolution::Hz1, 900, 1000, 1200)
        .unwrap();
    assert_eq!(suffix.len(), 1);
    assert_eq!(suffix[0].observation_count, 2);
    assert_eq!(suffix[0].first_observed_frames, 900);
    assert_eq!(suffix[0].last_observed_frames, 1000);
    assert_eq!(suffix[0].lufs_m.mean, Some(9.5));
    let between_slots = history
        .time_range(MeterHistoryResolution::Hz1, 850, 950, 1200)
        .unwrap();
    assert_eq!(between_slots.len(), 1);
    assert_eq!(between_slots[0].observation_count, 1);
    assert_eq!(between_slots[0].first_observed_frames, 900);
    assert_eq!(between_slots[0].last_observed_frames, 900);
    assert_eq!(between_slots[0].lufs_m.mean, Some(9.0));
    assert!(history
        .time_range(MeterHistoryResolution::Hz1, 800, 1000, 1200)
        .unwrap()
        .is_empty());
    history.exact.pop_back();
    assert!(history
        .time_range(MeterHistoryResolution::Hz1, 900, 1000, 1200)
        .unwrap()
        .is_empty());
    // The numerical cutoff is newer than the cursor, but its last required slot is retained.
    let only_oldest = history
        .time_range(MeterHistoryResolution::Hz1, 850, 950, 1200)
        .unwrap();
    assert_eq!(only_oldest.len(), 1);
    assert_eq!(only_oldest[0].lufs_m.mean, Some(9.0));
}

#[test]
fn unrelated_cursor_identities_do_not_retire_middle_source_exact_facts() {
    let mut history = MeterHistory::with_config(20, 20, 20, 10, 100);
    history.set_step_frames(100);
    let mut push = |epoch, run, observed, value| {
        history.push(
            epoch,
            1,
            run,
            observed,
            (Some(observed as i64), CaptureClockSource::ProjectTimeline),
            &MeasureResult {
                lufs_m: value,
                psr: value,
                ..Default::default()
            },
            MeterHistoryAux::default(),
        );
    };
    push(10, 1, 10000, Some(99.0));
    push(11, 2, 100, Some(1.0));
    push(11, 2, 200, None);
    push(11, 2, 300, Some(3.0));
    push(11, 2, 400, Some(4.0));
    push(12, 3, 1, Some(99.0));
    let retained = history
        .time_range(MeterHistoryResolution::Hz1, 100, 400, 1200)
        .unwrap();
    assert_eq!(retained.len(), 3);
    assert!(retained
        .iter()
        .all(|p| p.measurement_epoch == 11 && p.run_id == 2));
    assert_eq!(retained[0].lufs_m.mean, Some(1.0));
    assert_eq!(retained[1].lufs_m.mean, None);
    assert_eq!(retained[2].observation_count, 2);
    assert_eq!(retained[2].lufs_m.mean, Some(3.5));
}
