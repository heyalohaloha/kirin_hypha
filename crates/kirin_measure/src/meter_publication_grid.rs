//! Meter Session publication boundaries. Unknown clocks keep the received-frame cadence.
use super::*;

impl MeterClockTracker {
    pub(crate) fn publication_segment_frames(&self, maximum: u64, step: u64, buffered: u64) -> u64 {
        let Some(first) = self.pending.front() else {
            return maximum;
        };
        if self
            .pending
            .iter()
            .all(|span| span.next_position_samples.is_none())
        {
            return maximum.min(step.saturating_sub(buffered).max(1));
        }
        let grid = first
            .next_position_samples
            .and_then(|position| {
                let step = i64::try_from(step).ok().filter(|step| *step > 0)?;
                Some((step - position.rem_euclid(step)) as u64)
            })
            .unwrap_or_else(|| step.saturating_sub(buffered).max(1));
        let mut same_run = 0_u64;
        let mut transition = false;
        for span in &self.pending {
            if span.run_id != first.run_id || span.kind != first.kind {
                transition = true;
                break;
            }
            same_run = same_run.saturating_add(span.remaining_frames);
            if same_run >= maximum {
                break;
            }
        }
        maximum
            .min(grid)
            .min(if transition { same_run } else { maximum })
            .max(1)
    }

    pub(crate) fn complete_publication_segment(&mut self, frames: u64, step: u64) -> bool {
        let clock = self.consume_observation(frames);
        // Finish the old partial cell before entering another run. Its coverage cannot
        // qualify a fresh window; all later cells end on the new run's native grid.
        self.pending.front().is_some_and(|next| {
            next.run_id != clock.run_id
                && (clock.timeline_endpoint_samples.is_some()
                    || next.next_position_samples.is_some())
        }) || clock
            .timeline_endpoint_samples
            .is_some_and(|end| end.rem_euclid(step as i64) == 0)
    }
}
