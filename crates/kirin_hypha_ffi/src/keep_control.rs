//! Serialized, non-audio Keep and Stop controls.
use super::*;

impl KirinHyphaEngine {
    /// Prepare this POST's selected PRE pair without blocking the host on writer readiness.
    pub fn keep(&self) -> bool {
        let control = self
            .keep_control
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        self.keep_with_optional_generation(None, &control)
    }

    pub(super) fn keep_with_optional_generation(
        &self,
        generation: Option<&CaptureGeneration>,
        control: &std::sync::MutexGuard<'_, ()>,
    ) -> bool {
        if generation.is_none() {
            clear_keep_action_notice(&self.keep_action_notice);
        }
        if self.reject_unsupported_record_action(generation.is_none()) {
            return false;
        }
        let (project_hash, post_iid, daw) = {
            let id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return false,
            };
            if id.project_hash.is_empty() || id.instance_id.is_empty() {
                return false; // 未 enable_post_writes
            }
            (
                id.project_hash.clone(),
                id.instance_id.clone(),
                id.daw_session_uuid.clone(),
            )
        };
        // Direct Keep and the broadcast receiver share the same pair/generation entry.
        let pair_target = self.pair_binding.desired_name();
        let paired_pre_target = self.pair_binding.recording_pre();
        let latched_pre = self.pair_binding.latched_pre();
        let license = self.license.refresh_for_user_action();
        resolve_and_enter_keep(
            control,
            &self.keep_control,
            license,
            &self.record_sm,
            &pair_target,
            &paired_pre_target,
            &project_hash,
            &post_iid,
            &daw,
            &latched_pre,
            &self.record_error_message, // persistent producer/I/O fault channel
            &self.keep_action_notice,
            &self.keep_phase,
            &self.keep_phase_generation_started_at_ms,
            &self.keep_record_generation,
            None,
            generation,
        )
    }

    /// Retire the preparation and release this pair back to Watch.
    pub fn stop(&self) {
        let control = self
            .keep_control
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        self.clear_keep_preparation(&control);
        // Stop preserves the exact pair identity for a later WAV Drop. Reservation release and
        // record_signal teardown still share the same resolver with broadcast Stop.
        let (project_hash, post_iid) = match self.identity.lock() {
            Ok(id) => (id.project_hash.clone(), id.instance_id.clone()),
            Err(_) => (String::new(), String::new()),
        };
        let paired_pre_target = self.pair_binding.recording_pre();
        resolve_and_exit_stop(
            &self.record_sm,
            &self.record_ingress,
            &paired_pre_target,
            &project_hash,
            &post_iid,
            Some(ReleaseReason::ManualStop),
        );
    }

    /// Retire local preparation, broadcast All Stop, and release this pair.
    pub fn stop_all(&self) {
        let control = self
            .keep_control
            .lock()
            .unwrap_or_else(|error| error.into_inner());
        self.clear_keep_preparation(&control);
        let post_iid = match self.identity.lock() {
            Ok(id) => id.instance_id.clone(),
            Err(_) => String::new(),
        };
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw = read_shared_id(&self.daw_session_id_cell);
        if !project_hash.is_empty() && !post_iid.is_empty() {
            if let Ok(p) = StoragePaths::default_platform() {
                let base = p.plugin_data_dir();
                let active =
                    kirin_measure::read_stop_target_generation(&base, &project_hash, &post_iid)
                        .ok()
                        .flatten();
                let mut wrote_generation_stop = false;
                if let Some(generation) = active {
                    if mark_generation_terminal(
                        &base,
                        &generation,
                        GenerationTerminalReason::AllStop,
                    )
                    .is_ok()
                    {
                        let projects = generation
                            .members
                            .iter()
                            .map(|member| member.project_hash.as_str())
                            .collect::<std::collections::BTreeSet<_>>();
                        wrote_generation_stop = true;
                        for ph in projects {
                            if write_stop_broadcast_for_generation(
                                &base,
                                ph,
                                &post_iid,
                                generation.daw_session_id.clone(),
                                generation.host_process_id,
                                &generation,
                            )
                            .is_err()
                            {
                                wrote_generation_stop = false;
                            }
                        }
                    }
                }
                // Legacy/no-active fallback is deliberately one known shelf. New producers never
                // rediscover an All Stop group by scanning live/history directories.
                if !wrote_generation_stop {
                    let _ = write_stop_broadcast(&base, &project_hash, &post_iid, daw.clone());
                }
            }
        }
        let (project_hash, post_iid) = match self.identity.lock() {
            Ok(id) => (id.project_hash.clone(), id.instance_id.clone()),
            Err(_) => (String::new(), String::new()),
        };
        let paired_pre_target = self.pair_binding.recording_pre();
        resolve_and_exit_stop(
            &self.record_sm,
            &self.record_ingress,
            &paired_pre_target,
            &project_hash,
            &post_iid,
            Some(ReleaseReason::AllStop),
        );
    }

    fn clear_keep_preparation(&self, _control: &std::sync::MutexGuard<'_, ()>) {
        clear_keep_action_notice(&self.keep_action_notice);
        self.keep_phase_generation_started_at_ms
            .store(0, Ordering::Release);
        self.keep_record_generation.store(0, Ordering::Release);
        self.keep_phase
            .store(KIRIN_KEEP_PHASE_IDLE, Ordering::Release);
    }
}
