use super::*;

pub(super) struct PreparedPostSession {
    pub(super) session: PostSession,
    pub(super) renewal: bool,
    pub(super) retired: Option<(Option<SpectrumTarget>, Uuid)>,
    pub(super) reset_runtime: bool,
}

impl SpectrumCoordinator {
    pub(super) fn publish_post_request(
        &self,
        session: &PostSession,
        post_instance_id: &str,
        target: &SpectrumTarget,
    ) -> bool {
        let issued_at_unix_ms = unix_ms_now();
        let attack_band = self.post_attack_band();
        let result = renew_request(
            session,
            post_instance_id,
            target,
            self.sample_rate,
            issued_at_unix_ms,
            attack_band,
        );
        if result.is_ok() {
            self.note_sent_attack_band(attack_band);
        }
        let completed_at = Instant::now();
        let completed_at_unix_ms = unix_ms_now();
        let published_live_lease = result.is_ok()
            && completed_at_unix_ms <= issued_at_unix_ms.saturating_add(REQUEST_LEASE_MS);
        let mut slot = match self.post_session.lock() {
            Ok(slot) => slot,
            Err(poisoned) => poisoned.into_inner(),
        };
        let current = slot.as_mut().filter(|current| {
            current.request_id == session.request_id
                && current.target.as_ref() == Some(target)
                && current.analysis_mode == session.analysis_mode
                && current.channel_mode == session.channel_mode
                && current.state_epoch_samples == session.state_epoch_samples
        });
        match (published_live_lease, current) {
            (true, Some(current)) if self.post_visible() => {
                current.last_renewed = Some(completed_at);
                self.exchange_worker.record_published_update();
                true
            }
            (true, _) => {
                drop(slot);
                cleanup_owned_request(Some(target), session.request_id);
                false
            }
            (false, Some(current)) => {
                let factual_lease = current.last_renewed.is_some_and(|renewed| {
                    completed_at.duration_since(renewed) < PRESENTATION_HOLD
                });
                if !factual_lease {
                    self.store_view(SpectrumViewStatus::Unavailable, None, None);
                }
                false
            }
            (false, None) => false,
        }
    }
}

#[cfg(test)]
impl SpectrumCoordinator {
    /// Test convenience wrapper. Production supplies the pair name to the lease metadata.
    pub(crate) fn post_tick(&self, post_instance_id: &str, target: Option<SpectrumTarget>) -> bool {
        self.post_tick_for_owner(post_instance_id, target, "")
    }
}
