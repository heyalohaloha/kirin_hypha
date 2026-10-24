//! Control-thread entry for preparing every eligible Keep pair.
use super::keep_preparation::KeepFailure;
use super::*;

impl KirinHyphaEngine {
    /// Stage an immutable roster, notify its exact POSTs, and enter the local pair. Both writers
    /// of every member must be ready before the generation commits; failures abort the roster.
    pub fn keep_all(&self) -> bool {
        clear_keep_action_notice(&self.keep_action_notice);
        if self.reject_unsupported_record_action(true) {
            return false;
        }
        if self.audition.reject_keep(&self.keep_action_notice) {
            return false;
        }
        let post_iid = {
            let id = match self.identity.lock() {
                Ok(g) => g,
                Err(_) => return false,
            };
            if id.project_hash.is_empty() || id.instance_id.is_empty() {
                return false; // 未 enable_post_writes
            }
            id.instance_id.clone()
        };
        let project_hash = read_shared_id(&self.project_hash_cell);
        let daw = read_shared_id(&self.daw_session_id_cell);
        let host_process_id = current_host_process_id();
        if project_hash.is_empty() {
            return false;
        }
        let p = match StoragePaths::default_platform() {
            Ok(paths) => paths,
            Err(error) => {
                KeepFailure::from_storage(&error).report(
                    &self.keep_action_notice,
                    "All Keep storage",
                    &error,
                );
                return false;
            }
        };
        let kirin_root = PlatformPaths::current_kirin_tmp_root();
        let ready = enumerate_ready_post_pair_candidates_for_operation_group(
            &kirin_root,
            &project_hash,
            &daw,
            host_process_id,
        );
        let members: Vec<_> = ready
            .iter()
            .filter_map(|candidate| {
                Some((
                    CaptureGenerationMember {
                        project_hash: candidate.project_uuid.clone(),
                        post_instance_id: candidate.instance_id.clone(),
                        pre_instance_id: candidate.paired_pre_instance_id.clone()?,
                        record_session_id: String::new(),
                    },
                    candidate.pair_pre_name.clone(),
                ))
            })
            .collect();
        if members.len() > MAX_CAPTURE_GENERATION_MEMBERS {
            if let Ok(mut error) = self.keep_action_notice.write() {
                *error = Some("Maximum 12 pairs reached".to_string());
            }
            return false;
        }
        if members.is_empty() {
            if matches!(self.license.refresh_for_user_action(), License::Os) {
                KeepFailure::NoReadyPairs.publish(&self.keep_action_notice);
            }
            return false;
        }
        let generation = CaptureGeneration::new_for_named_members(
            post_iid.clone(),
            daw.clone(),
            host_process_id,
            members,
        );
        if !generation.is_valid() {
            KeepFailure::SetupChanged.publish(&self.keep_action_notice);
            return false;
        }
        let plugin_data_dir = p.plugin_data_dir();
        let mut transaction =
            match CaptureGenerationTransaction::begin(&plugin_data_dir, &generation) {
                Ok(transaction) => transaction,
                Err(error) => {
                    KeepFailure::from_generation(&error).report(
                        &self.keep_action_notice,
                        "All Keep begin",
                        &error,
                    );
                    return false;
                }
            };
        if let Err(error) = transaction.stage() {
            KeepFailure::from_generation(&error).report(
                &self.keep_action_notice,
                "All Keep stage",
                &error,
            );
            return false;
        }
        let project_hashes = generation
            .members
            .iter()
            .map(|member| member.project_hash.clone())
            .collect::<std::collections::BTreeSet<_>>();
        for ph in project_hashes {
            if let Err(error) = write_broadcast_for_generation(
                &plugin_data_dir,
                &ph,
                &post_iid,
                daw.clone(),
                host_process_id,
                &generation,
            ) {
                KeepFailure::from_broadcast(&error).report(
                    &self.keep_action_notice,
                    "All Keep broadcast",
                    &error,
                );
                return false;
            }
        }
        if !self.keep_with_generation(&generation) {
            return false;
        }
        super::keep_preparation::start_keep_preparation(
            transaction,
            &self.keep_action_notice,
            &self.keep_phase,
            &self.keep_phase_generation_started_at_ms,
            generation.started_at_ms,
            true,
        )
    }
}
