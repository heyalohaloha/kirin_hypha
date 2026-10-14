use super::*;

pub(super) struct PreparedPostSession {
    pub(super) session: PostSession,
    pub(super) renewal: bool,
    pub(super) retired: Option<(Option<SpectrumTarget>, Uuid)>,
}

impl SpectrumCoordinator {
    pub(super) fn publish_post_request(
        &self,
        session: &PostSession,
        post_instance_id: &str,
        target: &SpectrumTarget,
    ) -> bool {
        self.publish_post_request_with_repair(session, post_instance_id, target, true)
    }

    pub(super) fn post_definition_is_current(
        &self,
        current: &PostSession,
        expected: &PostSession,
    ) -> bool {
        self.post_visible()
            && current.request_id == expected.request_id
            && current.authority_revision == expected.authority_revision
            && current.attack_origin == expected.attack_origin
            && current.target == expected.target
            && current.analysis_mode == expected.analysis_mode
            && current.channel_mode == expected.channel_mode
            && self.runtime.analysis_mode() == expected.analysis_mode
            && self.runtime.channel_mode() == expected.channel_mode
            && self.pair_authority_revision.load(Ordering::Acquire) == expected.authority_revision
    }

    pub(super) fn cleanup_retired_post_request(&self, expected: &PostSession) {
        let slot = self.post_session.lock().unwrap_or_else(|e| e.into_inner());
        if self.post_visible()
            && slot
                .as_ref()
                .is_some_and(|current| current.request_id == expected.request_id)
        {
            return; // Another epoch of this owner must never be removed by its stale clone.
        }
        drop(slot);
        cleanup_owned_request(expected.target.as_ref(), expected.request_id);
    }

    fn publish_post_request_with_repair(
        &self,
        session: &PostSession,
        post_instance_id: &str,
        target: &SpectrumTarget,
        allow_repair: bool,
    ) -> bool {
        if !self.post_session_is_current(session) {
            self.cleanup_retired_post_request(session);
            return false;
        }
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
        let completed_at = Instant::now();
        let completed_at_unix_ms = unix_ms_now();
        let published_live_lease = result.is_ok()
            && completed_at_unix_ms <= issued_at_unix_ms.saturating_add(REQUEST_LEASE_MS);
        let mut slot = match self.post_session.lock() {
            Ok(slot) => slot,
            Err(poisoned) => poisoned.into_inner(),
        };
        let current = slot.as_mut().filter(|current| {
            self.post_definition_is_current(current, session)
                && current.state_epoch_samples == session.state_epoch_samples
        });
        match (result.is_ok(), published_live_lease, current) {
            (_, true, Some(current)) => {
                self.note_sent_attack_band(attack_band);
                current.last_renewed = Some(completed_at);
                self.exchange_worker.record_published_update();
                true
            }
            (true, false, current) | (true, true, current @ None) => {
                if let Some(current) = current {
                    let factual_lease = current.last_renewed.is_some_and(|renewed| {
                        completed_at.duration_since(renewed) < PRESENTATION_HOLD
                    });
                    if !factual_lease {
                        self.store_view(SpectrumViewStatus::Unavailable, None, None);
                    }
                }
                // A delayed unarmed publication can finish after this same owner commits
                // its single epoch or reopens with a new ID. An expired same-epoch write
                // can also replace a fresh lease. Restore the current descriptor once;
                // never delete shared slots or reset the successor's runtime.
                let repair = allow_repair
                    .then(|| {
                        slot.as_ref()
                            .filter(|current| {
                                self.post_definition_is_current(current, current)
                                    && current.target.as_ref() == Some(target)
                            })
                            .cloned()
                    })
                    .flatten();
                drop(slot);
                if let Some(current) = repair {
                    let _ = self.publish_post_request_with_repair(
                        &current,
                        post_instance_id,
                        target,
                        false,
                    );
                } else {
                    self.cleanup_retired_post_request(session);
                }
                false
            }
            (false, false, Some(current)) => {
                let factual_lease = current.last_renewed.is_some_and(|renewed| {
                    completed_at.duration_since(renewed) < PRESENTATION_HOLD
                });
                if !factual_lease {
                    self.store_view(SpectrumViewStatus::Unavailable, None, None);
                }
                false
            }
            (false, _, None) => false,
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
