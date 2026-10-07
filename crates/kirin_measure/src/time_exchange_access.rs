//! Cached TIME acquisition. This UI path never discovers, reads, publishes or joins files.
use super::*;
use std::sync::atomic::AtomicU64;

impl MeterDeltaHistoryExchange {
    pub fn set_pair_authority_revision(&self, revision: u64) {
        // A delayed IO tick must not revive a revision retired by a newer selection.
        // This is a retirement high-water mark; proof admission still requires exact equality.
        self.time_authority.fetch_max(revision, Ordering::AcqRel);
    }

    pub fn time_authority_revision(&self) -> u64 {
        self.time_authority.load(Ordering::Acquire)
    }

    pub fn time_post_span_token(&self) -> u64 {
        self.time_post_span.load(Ordering::Acquire)
    }

    pub fn time_comparison(
        &self,
        resolution: MeterHistoryResolution,
        lower: u64,
        cutoff: u64,
        capacity: usize,
    ) -> Option<Result<Option<TimeComparisonView>, crate::meter_history::TimeHistoryCountOverflow>>
    {
        let delta = self.delta.try_lock().ok()?;
        let Some(pair) = delta.pair_key() else {
            return Some(Ok(None));
        };
        let Some(binding) = pair.post_binding.as_ref() else {
            return Some(Ok(None));
        };
        if binding.generation != self.time_authority_revision() {
            return Some(Ok(None));
        }
        Some(
            delta
                .time
                .view(pair, &delta.history, resolution, lower, cutoff, capacity),
        )
    }
}

pub(super) fn initial_span(session: &Arc<Mutex<MeterSession>>) -> Arc<AtomicU64> {
    lock_recover(session).time_span_token()
}
