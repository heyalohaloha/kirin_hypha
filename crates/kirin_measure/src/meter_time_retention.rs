//! Prepare the large V2 retention outside the delta state mutex, then admit by generation.
use super::*;

impl MeterDeltaHistoryExchange {
    pub(super) fn prepare_time_history(
        &self,
        pre: Option<&TimePublication>,
    ) -> Option<(u64, Box<MeterHistory>)> {
        self.prepare_time_history_with(pre, || Box::new(MeterHistory::new()))
    }

    fn prepare_time_history_with(
        &self,
        pre: Option<&TimePublication>,
        allocate: impl FnOnce() -> Box<MeterHistory>,
    ) -> Option<(u64, Box<MeterHistory>)> {
        if !pre.is_some_and(TimePublication::valid) {
            return None;
        }
        let ticket = {
            let delta = lock_recover(&self.delta);
            delta.time_history_preparation_ticket()?
        };
        Some((ticket, allocate()))
    }
}

#[cfg(test)]
#[path = "meter_time_retention_tests.rs"]
mod tests;
