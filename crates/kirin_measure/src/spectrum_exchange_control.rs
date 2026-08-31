use super::*;

impl SpectrumCoordinator {
    pub(crate) fn active_analysis_mode(&self) -> AnalysisViewMode {
        self.runtime.analysis_mode()
    }

    /// Message/control thread only. Filesystem work remains deferred to the existing IO thread.
    pub fn set_post_visible(&self, visible: bool) {
        let previous = self.post_visible.swap(visible, Ordering::AcqRel);
        if visible && !previous {
            let mut session = match self.post_session.lock() {
                Ok(session) => session,
                Err(poisoned) => poisoned.into_inner(),
            };
            *session = Some(self.new_post_session());
        } else if !visible {
            // Serialize the close edge with an in-flight exchange tick before releasing the
            // process-wide lease. A new POST can never overlap the previous owner's worker.
            let _tick_guard = match self.post_session.lock() {
                Ok(session) => session,
                Err(poisoned) => poisoned.into_inner(),
            };
            self.disable_analysis_runtimes();
            self.release_analysis_lease();
            self.store_view(SpectrumViewStatus::Hidden, None, None);
            self.store_attack_view(AttackPairViewSnapshot::default());
        }
        self.exchange_worker.notify();
    }

    /// UI/control thread only. Optional analysis modes never run in parallel within one POST.
    pub fn set_post_analysis_mode(&self, mode: AnalysisViewMode) -> bool {
        let mut session = match self.post_session.lock() {
            Ok(session) => session,
            Err(poisoned) => poisoned.into_inner(),
        };
        if session.is_none() && self.post_visible() {
            *session = Some(self.new_post_session());
        }
        if !self.runtime.set_analysis_mode(mode) {
            return false;
        }
        self.disable_analysis_runtimes();
        let status = if self.post_visible() {
            SpectrumViewStatus::WarmingUp
        } else {
            SpectrumViewStatus::Hidden
        };
        self.store_view(status, None, None);
        self.store_attack_view(AttackPairViewSnapshot {
            status,
            ..Default::default()
        });
        self.exchange_worker.notify();
        true
    }

    /// UI/control thread only. The next isolated exchange tick renews the exact request; this
    /// edge itself performs no filesystem access.
    pub fn set_post_channel_mode(&self, mode: SpectrumChannelMode) -> bool {
        // Serialize the mode edge with POST exchange publication. Without this guard, one tick
        // could clone an old exact pair, then publish it after the UI had already selected a new
        // channel definition and cleared the visible frame.
        let mut session = match self.post_session.lock() {
            Ok(session) => session,
            Err(poisoned) => poisoned.into_inner(),
        };
        if session.is_none() && self.post_visible() {
            *session = Some(self.new_post_session());
        }
        if !self.runtime.set_channel_mode(mode) {
            return false;
        }
        let status = if self.post_visible() {
            SpectrumViewStatus::WarmingUp
        } else {
            SpectrumViewStatus::Hidden
        };
        self.store_view(status, None, None);
        self.exchange_worker.notify();
        true
    }

    pub fn shutdown(&self) {
        self.post_visible.store(false, Ordering::Release);
        self.exchange_worker.shutdown_and_join();
        {
            let mut post_session = match self.post_session.lock() {
                Ok(session) => session,
                Err(poisoned) => poisoned.into_inner(),
            };
            let _ = post_session.take();
        }
        {
            let mut pre_session = match self.pre_session.lock() {
                Ok(session) => session,
                Err(poisoned) => poisoned.into_inner(),
            };
            let _ = pre_session.take();
        }
        // The normal close edge already notified the exchange worker to remove its exact request.
        // If Windows is retaining that filesystem operation, teardown must not start another
        // blocking read/remove. Any unpublished request expires after its bounded lease.
        self.disable_analysis_runtimes();
        self.release_analysis_lease();
    }

    pub(super) fn new_post_session(&self) -> PostSession {
        PostSession {
            request_id: Uuid::new_v4(),
            target: None,
            last_renewed: None,
            last_renewal_attempt: None,
            started_at: None,
            last_presented_at: None,
            last_presented_end_samples: None,
            analysis_mode: self.runtime.analysis_mode(),
            channel_mode: self.runtime.channel_mode(),
            state_epoch_samples: None,
        }
    }

    pub(super) fn disable_analysis_runtimes(&self) {
        let _ = self.runtime.set_enabled(false);
        if let Some(runtime) = self.attack_runtime.as_ref() {
            let _ = runtime.set_enabled(false);
        }
    }

    pub(super) fn set_active_runtime_enabled(&self, mode: AnalysisViewMode, enabled: bool) -> bool {
        match mode {
            AnalysisViewMode::Attack => {
                let _ = self.runtime.set_enabled(false);
                self.attack_runtime
                    .as_ref()
                    .is_some_and(|runtime| runtime.set_enabled(enabled))
            }
            _ => {
                if let Some(runtime) = self.attack_runtime.as_ref() {
                    let _ = runtime.set_enabled(false);
                }
                self.runtime.set_enabled(enabled)
            }
        }
    }
}
