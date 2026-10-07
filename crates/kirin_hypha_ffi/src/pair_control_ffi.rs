//! Non-RT pair-selection transitions shared by explicit and restored selection.
use super::*;

impl KirinHyphaEngine {
    pub(crate) fn begin_pair_reselection(&self) -> (String, String) {
        let (project_hash, post_iid) = match self.identity.lock() {
            Ok(id) => (id.project_hash.clone(), id.instance_id.clone()),
            Err(_) => (String::new(), String::new()),
        };
        if !project_hash.is_empty() && !post_iid.is_empty() {
            if let Ok(paths) = StoragePaths::default_platform() {
                let _ = mark_released_with_reason(
                    &paths.plugin_data_dir(),
                    &project_hash,
                    &post_iid,
                    ReleaseReason::ManualStop,
                );
            }
        }
        self.record_sm.exit_record();
        (project_hash, post_iid)
    }

    pub(crate) fn finish_pair_reselection(
        &self,
        transition: PairTargetTransition,
        project_hash: &str,
        post_iid: &str,
        pair_claimed_at: f64,
    ) {
        if !transition.changed {
            return;
        }
        if !project_hash.is_empty() && !post_iid.is_empty() {
            if let (Some(pre), Ok(paths)) = (
                transition.previous_pre_instance_id.as_deref(),
                StoragePaths::default_platform(),
            ) {
                reservation::release_pairing(&paths.plugin_data_dir(), project_hash, pre, post_iid);
            }
        }
        *self
            .pair_claimed_at
            .write()
            .unwrap_or_else(|poisoned| poisoned.into_inner()) = pair_claimed_at;
        self.preset_available.store(false, Ordering::Release);
        *self
            .delta_result
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner()) = DeltaResult::default();
        if let Ok(mut error) = self.record_error_message.write() {
            *error = None;
        }
    }

    /// 対 PRE 名（pair target）を設定する（B-061 3d-b / identity.name 結合を解く）。
    /// io_thread と Arc 共有のため `enable_post_writes` 後でも live に反映される。
    pub fn set_pair_target(&self, name: String) {
        // B-071: sanitize（ASCII graphic + space / max 16）で PRE 名と同一語彙に正規化する
        // 単一情報源（kirin_measure::sanitize_name）。select_target_pre は sanitized な PRE 名と
        // 照合するため、pair target も同じ正規化を通す。
        let sanitized = sanitize_name(&name);
        if !self.pair_binding.name_change_required(&sanitized) {
            return;
        }

        // Publish Released before leaving Record so an ACK poller cannot re-enter the old session.
        let (project_hash, post_iid) = self.begin_pair_reselection();
        let transition = self.pair_binding.replace_name(sanitized);
        let claimed_at = if self
            .pair_binding
            .desired_name()
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .is_empty()
        {
            0.0
        } else {
            epoch_secs_now()
        };
        self.finish_pair_reselection(transition, &project_hash, &post_iid, claimed_at);
    }

    /// Bind one exact PRE selected from the dropdown. The instance latch is the sole selection
    /// authority; the human name is display metadata only.
    pub fn set_pair_candidate(&self, instance_id: &str) -> bool {
        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw = read_shared_id(&self.daw_session_id_cell);
        let Some((selected, name)) =
            select_live_pre_pair_choice_by_instance_for_post_project_in_session(
                &kirin_root,
                instance_id,
                &project_hash,
                &daw,
            )
        else {
            return false;
        };
        let name = sanitize_name(&name);
        let latch = latch_selected_pre(name.clone(), selected);
        if self.pair_binding.matches_exact(&name, &latch) {
            return true;
        }
        let post_instance_id = self
            .identity
            .lock()
            .map(|identity| identity.instance_id.clone())
            .unwrap_or_default();
        if kirin_measure::pair_claim_owned_by_other_post(
            &kirin_root,
            instance_id,
            &project_hash,
            &post_instance_id,
        ) {
            if let Ok(mut notice) = self.keep_action_notice.write() {
                *notice = Some("PRE already in use".to_string());
            }
            return false;
        }
        let (project_hash, post_iid) = self.begin_pair_reselection();
        let transition = self.pair_binding.replace_exact(name, latch);
        self.finish_pair_reselection(transition, &project_hash, &post_iid, epoch_secs_now());
        true
    }

    /// Restore one exact PRE selected in a saved DAW document without scanning the live registry.
    /// The fixed path may not exist yet because hosts restore plugin instances in arbitrary order;
    /// the latch remains Waiting and becomes Paired as soon as that PRE publishes at the same path.
    pub fn restore_pair_candidate(&self, pre_project_hash: &str, instance_id: &str) -> bool {
        let desired_name = self
            .pair_binding
            .desired_name()
            .read()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .clone();
        let daw_session_id = self
            .identity
            .lock()
            .map(|identity| identity.daw_session_uuid.clone())
            .unwrap_or_default();
        let Some(latch) = restored_pair_latch(
            &PlatformPaths::current_kirin_tmp_root(),
            pre_project_hash,
            &daw_session_id,
            &desired_name,
            instance_id,
            current_host_process_id(),
        ) else {
            return false;
        };
        if self.pair_binding.matches_exact(&desired_name, &latch) {
            return true;
        }

        let (project_hash, post_iid) = self.begin_pair_reselection();
        let transition = self.pair_binding.replace_exact(desired_name, latch);
        self.finish_pair_reselection(transition, &project_hash, &post_iid, epoch_secs_now());
        true
    }
}
