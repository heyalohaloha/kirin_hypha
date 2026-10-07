//! TIME-only physical-range export; legacy retention buckets keep their original cadence.
use super::*;

impl MeterHistory {
    pub fn time_range(
        &self,
        resolution: MeterHistoryResolution,
        lower: u64,
        cutoff: u64,
        capacity: usize,
    ) -> Result<Vec<MeterHistoryEntry>, TimeHistoryCountOverflow> {
        if capacity == 0 || lower > cutoff {
            return Ok(Vec::new());
        }
        let candidates = self.recent(
            resolution,
            match resolution {
                MeterHistoryResolution::Hz10 => self.exact_capacity,
                MeterHistoryResolution::Hz1 => self.one_second.capacity + 1,
                MeterHistoryResolution::Hz0_1 => self.ten_seconds.capacity + 1,
            },
        );
        let mut selected = Vec::new();
        for point in candidates {
            if point.last_observed_frames < lower || point.first_observed_frames > cutoff {
                continue;
            }
            if point.segment_id == 0 {
                selected.extend(self.refine_mixed_bucket(point, lower, cutoff, resolution)?);
            } else if point.first_observed_frames >= lower && point.last_observed_frames <= cutoff {
                selected.push(point);
            } else {
                selected.extend(self.refine_partial_bucket(point, lower, cutoff, resolution)?);
            }
        }
        reduce_time_history(&selected, capacity, resolution)
    }

    fn exact_rows_for(
        &self,
        point: MeterHistoryEntry,
        first: u64,
        last: u64,
    ) -> impl Iterator<Item = MeterHistoryEntry> + '_ {
        // Only the same identity makes cursor coordinates comparable. A partial request asks
        // for its selected exact slots, not an already evicted part of the complete bucket.
        let retained = first <= last
            && self
                .exact
                .front()
                .zip(self.exact.back())
                .is_some_and(|(front, back)| {
                    (!same_identity(front, &point) || first >= front.first_observed_frames)
                        && (!same_identity(back, &point) || last <= back.last_observed_frames)
                });
        self.exact
            .iter()
            .take(if retained { self.exact.len() } else { 0 })
            .filter(move |exact| {
                same_identity(exact, &point)
                    && exact.first_observed_frames >= first
                    && exact.last_observed_frames <= last
            })
            .copied()
    }

    fn refine_mixed_bucket(
        &self,
        point: MeterHistoryEntry,
        lower: u64,
        cutoff: u64,
        resolution: MeterHistoryResolution,
    ) -> Result<Vec<MeterHistoryEntry>, TimeHistoryCountOverflow> {
        let exact: Vec<_> = self
            .exact_rows_for(
                point,
                point.first_observed_frames,
                point.last_observed_frames,
            )
            .collect();
        // Prove retention of every original observation, including known missing-slot gaps.
        // Requiring a complete physical grid here would erase the retained sides of a gap.
        if exact.len() != usize::from(point.observation_count)
            || exact.first().map(|p| p.first_observed_frames) != Some(point.first_observed_frames)
            || exact.last().map(|p| p.last_observed_frames) != Some(point.last_observed_frames)
            || exact
                .iter()
                .any(|p| p.segment_id == 0 || p.observation_count != 1)
        {
            return Ok(Vec::new());
        }
        let clipped: Vec<_> = exact
            .into_iter()
            .filter(|p| p.first_observed_frames >= lower && p.last_observed_frames <= cutoff)
            .collect();
        let segment_count = usize::from(!clipped.is_empty())
            + clipped
                .windows(2)
                .filter(|p| p[0].segment_id != p[1].segment_id)
                .count();
        let count = clipped.len();
        checked_decimate_history(clipped.into_iter(), count, segment_count, resolution)
    }

    fn refine_partial_bucket(
        &self,
        point: MeterHistoryEntry,
        lower: u64,
        cutoff: u64,
        resolution: MeterHistoryResolution,
    ) -> Result<Vec<MeterHistoryEntry>, TimeHistoryCountOverflow> {
        // An unmixed clipped bucket needs every expected exact slot in the selected prefix.
        // No future/unretained suffix mean or envelope survives coordinate clipping.
        if self.step_frames == 0 {
            return Ok(Vec::new());
        }
        let start = lower.max(point.first_observed_frames);
        let end = cutoff.min(point.last_observed_frames);
        let offset = start
            .saturating_sub(point.first_observed_frames)
            .div_ceil(self.step_frames);
        let first = point
            .first_observed_frames
            .saturating_add(offset.saturating_mul(self.step_frames));
        if first > end {
            return Ok(Vec::new());
        }
        let expected = (end - first) / self.step_frames + 1;
        let last = first + (expected - 1) * self.step_frames;
        let exact: Vec<_> = self
            .exact_rows_for(point, first, last)
            .filter(|p| {
                p.segment_id == point.segment_id
                    && p.first_observed_frames >= first
                    && p.last_observed_frames <= end
            })
            .collect();
        if exact.len() as u64 != expected
            || exact.first().map(|p| p.first_observed_frames) != Some(first)
            || exact.windows(2).any(|p| {
                p[1].first_observed_frames
                    .checked_sub(p[0].last_observed_frames)
                    != Some(self.step_frames)
            })
        {
            return Ok(Vec::new());
        }
        checked_decimate_history(exact.into_iter(), expected as usize, 1, resolution)
    }
}

fn same_identity(a: &MeterHistoryEntry, b: &MeterHistoryEntry) -> bool {
    a.measurement_epoch == b.measurement_epoch
        && a.generation == b.generation
        && a.run_id == b.run_id
}

/// Reduces only qualified TIME segments, never legacy mixed buckets without their exact rows.
pub fn reduce_time_history(
    entries: &[MeterHistoryEntry],
    capacity: usize,
    resolution: MeterHistoryResolution,
) -> Result<Vec<MeterHistoryEntry>, TimeHistoryCountOverflow> {
    // This adapter has no exact-retention owner. Reject an unknown mixed input rather than
    // dropping it and then connecting its surviving neighbours across an unknown gap.
    if entries.iter().any(|entry| entry.segment_id == 0) {
        return Ok(Vec::new());
    }
    let mut output = checked_decimate_history(
        entries.iter().copied(),
        entries.len(),
        capacity.min(1200),
        resolution,
    )?;
    let mut previous = None;
    for point in &mut output {
        point.connects_previous &= previous.is_some_and(|prior: MeterHistoryEntry| {
            prior.measurement_epoch == point.measurement_epoch
                && prior.generation == point.generation
                && prior.run_id == point.run_id
                && prior.segment_id == point.segment_id
        });
        previous = Some(*point);
    }
    Ok(output)
}
