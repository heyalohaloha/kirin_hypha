//! Immutable, generation-wide Drop acceptance.
//!
//! Kirin OS publishes mutable transport files and one generation manifest. Hypha snapshots the
//! complete producer roster and every exact Drop artifact once, under the generation lifecycle
//! lock. Member callbacks subsequently read only this immutable acceptance, so a retry, restart,
//! or replacement of the transport files cannot split one Keep generation across WAVs.

use super::{
    drop_commit_path, drop_generation_acceptance_path, drop_generation_transaction_path,
    drop_transaction_path, DropRecordCommit, DropRecordGenerationAcceptance,
    DropRecordGenerationTransaction, DropRecordTransaction, DROP_COMMIT_SCHEMA,
    DROP_GENERATION_ACCEPTANCE_SCHEMA, DROP_GENERATION_TRANSACTION_SCHEMA, DROP_TRANSACTION_SCHEMA,
};
use crate::capture_generation::{
    archived_generation_path, CaptureGeneration, CaptureGenerationMember,
    CaptureGenerationMemberIdentity,
};
use crate::record_expected::{
    now_epoch_ms, read_claim_marker_for_session, ExpectedMetadataError, ExpectedWavMetadata,
    EXPECTED_METADATA_FUTURE_SKEW_MS,
};
use sha2::{Digest, Sha256};
use std::collections::{BTreeMap, BTreeSet};
use std::fs;
use std::io;
use std::path::Path;

#[derive(Debug, Clone, PartialEq, Eq, serde::Serialize, serde::Deserialize)]
pub(super) struct AcceptedDropMember {
    pub(super) member: CaptureGenerationMember,
    #[serde(default, skip_serializing_if = "Option::is_none")]
    identity: Option<CaptureGenerationMemberIdentity>,
    pub(super) metadata: ExpectedWavMetadata,
}

pub(super) fn accepted_member_for_session(
    base_dir: &Path,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
    project_hash: &str,
    session_id: &str,
) -> Result<Option<AcceptedDropMember>, ExpectedMetadataError> {
    let acceptance_path =
        drop_generation_acceptance_path(base_dir, capture_generation_id, generation_started_at_ms);
    crate::capture_generation_lifecycle::with_generation_lifecycle_lock(
        base_dir,
        capture_generation_id,
        || {
            let acceptance = match fs::read(&acceptance_path) {
                Ok(bytes) => {
                    let acceptance: DropRecordGenerationAcceptance =
                        serde_json::from_slice(&bytes)?;
                    if !acceptance.is_valid_for(capture_generation_id, generation_started_at_ms) {
                        return Ok(None);
                    }
                    acceptance
                }
                Err(error) if error.kind() == io::ErrorKind::NotFound => {
                    let Some(acceptance) = build_acceptance_from_exact_artifacts(
                        base_dir,
                        capture_generation_id,
                        generation_started_at_ms,
                        project_hash,
                        session_id,
                    )?
                    else {
                        return Ok(None);
                    };
                    crate::atomic_file::write_bytes_immutable(
                        &acceptance_path,
                        &serde_json::to_vec(&acceptance)?,
                    )?;
                    acceptance
                }
                Err(error) => return Err(ExpectedMetadataError::Io(error)),
            };
            Ok(acceptance
                .members
                .iter()
                .find(|accepted| {
                    accepted.member.project_hash == project_hash
                        && accepted.member.record_session_id == session_id
                })
                .cloned())
        },
    )
}

fn build_acceptance_from_exact_artifacts(
    base_dir: &Path,
    capture_generation_id: &str,
    generation_started_at_ms: i64,
    requested_project_hash: &str,
    requested_session_id: &str,
) -> Result<Option<DropRecordGenerationAcceptance>, ExpectedMetadataError> {
    // The exact requested member is the address used only to discover this Drop's transaction id.
    // It is read again below as part of the complete, canonical artifact snapshot.
    let Some(seed_commit_bytes) = read_optional(&drop_commit_path(
        base_dir,
        requested_project_hash,
        requested_session_id,
    ))?
    else {
        return Ok(None);
    };
    let seed_commit: DropRecordCommit = serde_json::from_slice(&seed_commit_bytes)?;
    if seed_commit.schema_version != DROP_COMMIT_SCHEMA
        || seed_commit.project_hash != requested_project_hash
        || seed_commit.record_session_id != requested_session_id
        || seed_commit.capture_generation_id != capture_generation_id
        || seed_commit.generation_started_at_ms != generation_started_at_ms
        || seed_commit.drop_commit_id.trim().is_empty()
    {
        return Ok(None);
    }

    let archive_path =
        archived_generation_path(base_dir, capture_generation_id, generation_started_at_ms);
    let Some(archive_bytes) = read_optional(&archive_path)? else {
        return Ok(None);
    };
    let generation: CaptureGeneration = serde_json::from_slice(&archive_bytes)?;
    if !generation.is_valid()
        || generation.capture_generation_id != capture_generation_id
        || generation.started_at_ms != generation_started_at_ms
    {
        return Ok(None);
    }

    let generation_transaction_path =
        drop_generation_transaction_path(base_dir, &seed_commit.drop_commit_id);
    let Some(generation_transaction_bytes) = read_optional(&generation_transaction_path)? else {
        return Ok(None);
    };
    let generation_transaction: DropRecordGenerationTransaction =
        serde_json::from_slice(&generation_transaction_bytes)?;
    if generation_transaction.schema_version != DROP_GENERATION_TRANSACTION_SCHEMA
        || generation_transaction.drop_commit_id != seed_commit.drop_commit_id
        || generation_transaction.capture_generation_id != capture_generation_id
        || generation_transaction.generation_started_at_ms != generation_started_at_ms
        || generation_transaction.created_at_ms <= 0
        || generation_transaction.bounce_id.trim().is_empty()
        || generation_transaction.wav_hash.trim().is_empty()
    {
        return Ok(None);
    }

    let expected_projects = exact_project_roster(&generation);
    let declared_projects = generation_transaction
        .projects
        .iter()
        .map(|project| {
            (
                project.project_hash.clone(),
                project.record_session_ids.clone(),
            )
        })
        .collect::<BTreeMap<_, _>>();
    if declared_projects.len() != generation_transaction.projects.len()
        || declared_projects != expected_projects
        || !generation_transaction
            .projects
            .windows(2)
            .all(|pair| pair[0].project_hash < pair[1].project_hash)
    {
        return Ok(None);
    }

    let now_ms = now_epoch_ms();
    let mut artifacts = Vec::<(Vec<String>, Vec<u8>)>::new();
    artifacts.push((
        vec![
            "capture_generation_archive".into(),
            capture_generation_id.into(),
            generation_started_at_ms.to_string(),
        ],
        archive_bytes,
    ));
    artifacts.push((
        vec![
            "drop_generation_transaction".into(),
            seed_commit.drop_commit_id.clone(),
        ],
        generation_transaction_bytes,
    ));
    let mut accepted_by_session = BTreeMap::new();
    let mut common_metadata = None::<ExpectedWavMetadata>;

    for (project_hash, session_ids) in &expected_projects {
        let project_path =
            drop_transaction_path(base_dir, project_hash, &seed_commit.drop_commit_id);
        let Some(project_bytes) = read_optional(&project_path)? else {
            return Ok(None);
        };
        let project: DropRecordTransaction = serde_json::from_slice(&project_bytes)?;
        if project.schema_version != DROP_TRANSACTION_SCHEMA
            || project.drop_commit_id != generation_transaction.drop_commit_id
            || project.project_hash != *project_hash
            || project.capture_generation_id != capture_generation_id
            || project.generation_started_at_ms != generation_started_at_ms
            || project.created_at_ms != generation_transaction.created_at_ms
            || project.bounce_id != generation_transaction.bounce_id
            || project.wav_hash != generation_transaction.wav_hash
            || project.record_session_ids != *session_ids
        {
            return Ok(None);
        }
        artifacts.push((
            vec!["drop_project_transaction".into(), project_hash.clone()],
            project_bytes,
        ));

        for session_id in session_ids {
            let commit_path = drop_commit_path(base_dir, project_hash, session_id);
            let Some(commit_bytes) = read_optional(&commit_path)? else {
                return Ok(None);
            };
            let commit: DropRecordCommit = serde_json::from_slice(&commit_bytes)?;
            let Some(member) = generation.members.iter().find(|member| {
                member.project_hash == *project_hash && member.record_session_id == *session_id
            }) else {
                return Ok(None);
            };
            let Some(marker) = read_claim_marker_for_session(base_dir, project_hash, session_id)?
            else {
                return Ok(None);
            };
            let expected_hash = commit.metadata.wav_hash.as_deref().unwrap_or_default();
            if commit.schema_version != DROP_COMMIT_SCHEMA
                || commit.drop_commit_id != generation_transaction.drop_commit_id
                || commit.project_hash != *project_hash
                || commit.record_session_id != *session_id
                || commit.capture_generation_id != capture_generation_id
                || commit.generation_started_at_ms != generation_started_at_ms
                || commit.created_at_ms != generation_transaction.created_at_ms
                || commit.metadata.created_at_ms != commit.created_at_ms
                || commit.metadata.bounce_id != generation_transaction.bounce_id
                || expected_hash != generation_transaction.wav_hash
                || !generation_metadata_is_attestable(&commit.metadata, now_ms)
                || commit.created_at_ms < marker.claimed_at_ms
                || marker.session_id != *session_id
                || marker.capture_generation_id != capture_generation_id
                || marker.generation_started_at_ms != generation_started_at_ms
                || marker.requested_by_post_instance_id != member.post_instance_id
                || marker
                    .metadata
                    .as_ref()
                    .is_some_and(|metadata| metadata != &commit.metadata)
                || common_metadata
                    .as_ref()
                    .is_some_and(|metadata| metadata != &commit.metadata)
            {
                return Ok(None);
            }
            common_metadata.get_or_insert_with(|| commit.metadata.clone());
            artifacts.push((
                vec![
                    "drop_session_commit".into(),
                    project_hash.clone(),
                    session_id.clone(),
                ],
                commit_bytes,
            ));
            accepted_by_session.insert(
                (project_hash.clone(), session_id.clone()),
                AcceptedDropMember {
                    member: member.clone(),
                    identity: generation.member_identity(session_id).cloned(),
                    metadata: commit.metadata.without_consumed_marker(),
                },
            );
        }
    }

    let members = generation
        .members
        .iter()
        .map(|member| {
            accepted_by_session.remove(&(
                member.project_hash.clone(),
                member.record_session_id.clone(),
            ))
        })
        .collect::<Option<Vec<_>>>();
    let Some(members) = members else {
        return Ok(None);
    };
    if !accepted_by_session.is_empty() || members.len() != generation.members.len() {
        return Ok(None);
    }

    let acceptance = DropRecordGenerationAcceptance {
        schema_version: DROP_GENERATION_ACCEPTANCE_SCHEMA.to_string(),
        capture_generation_id: capture_generation_id.to_string(),
        generation_started_at_ms,
        drop_commit_id: generation_transaction.drop_commit_id,
        created_at_ms: generation_transaction.created_at_ms,
        accepted_at_ms: now_ms,
        bounce_id: generation_transaction.bounce_id,
        wav_hash: generation_transaction.wav_hash,
        exact_artifacts_sha256: artifact_sha256(&artifacts),
        generation,
        members,
    };
    Ok(acceptance
        .is_valid_for(capture_generation_id, generation_started_at_ms)
        .then_some(acceptance))
}

fn exact_project_roster(generation: &CaptureGeneration) -> BTreeMap<String, Vec<String>> {
    let mut projects = BTreeMap::<String, Vec<String>>::new();
    for member in &generation.members {
        projects
            .entry(member.project_hash.clone())
            .or_default()
            .push(member.record_session_id.clone());
    }
    for sessions in projects.values_mut() {
        sessions.sort();
    }
    projects
}

fn artifact_sha256(artifacts: &[(Vec<String>, Vec<u8>)]) -> String {
    let mut hasher = Sha256::new();
    hasher.update((artifacts.len() as u64).to_be_bytes());
    for (fields, bytes) in artifacts {
        hasher.update((fields.len() as u64).to_be_bytes());
        for field in fields {
            let field = field.as_bytes();
            hasher.update((field.len() as u64).to_be_bytes());
            hasher.update(field);
        }
        hasher.update((bytes.len() as u64).to_be_bytes());
        hasher.update(bytes);
    }
    hex::encode(hasher.finalize())
}

fn read_optional(path: &Path) -> Result<Option<Vec<u8>>, ExpectedMetadataError> {
    match fs::read(path) {
        Ok(bytes) => Ok(Some(bytes)),
        Err(error) if error.kind() == io::ErrorKind::NotFound => Ok(None),
        Err(error) => Err(ExpectedMetadataError::Io(error)),
    }
}

/// Current generation Drop artifacts are immutable and bound to an archived exact roster. Their
/// wall-clock age is not an identity check: a DAW or Kirin OS restart may delay attestation well
/// beyond ten minutes. Freshness remains enforced only by the legacy mutable `current.json` path.
fn generation_metadata_is_attestable(metadata: &ExpectedWavMetadata, now_ms: i64) -> bool {
    metadata.is_complete()
        && metadata.created_at_ms <= now_ms.saturating_add(EXPECTED_METADATA_FUTURE_SKEW_MS)
}

impl DropRecordGenerationAcceptance {
    fn is_valid_for(&self, capture_generation_id: &str, generation_started_at_ms: i64) -> bool {
        let unique_session_ids = self
            .generation
            .members
            .iter()
            .map(|member| member.record_session_id.as_str())
            .collect::<BTreeSet<_>>();
        let common_metadata = self.members.first().map(|member| &member.metadata);
        self.schema_version == DROP_GENERATION_ACCEPTANCE_SCHEMA
            && self.capture_generation_id == capture_generation_id
            && self.generation_started_at_ms == generation_started_at_ms
            && !self.drop_commit_id.trim().is_empty()
            && self.created_at_ms > 0
            && self.accepted_at_ms > 0
            && !self.bounce_id.trim().is_empty()
            && !self.wav_hash.trim().is_empty()
            && self.exact_artifacts_sha256.len() == 64
            && self
                .exact_artifacts_sha256
                .bytes()
                .all(|byte| byte.is_ascii_hexdigit())
            && self.generation.is_valid()
            && self.generation.capture_generation_id == self.capture_generation_id
            && self.generation.started_at_ms == self.generation_started_at_ms
            && unique_session_ids.len() == self.generation.members.len()
            && self.members.len() == self.generation.members.len()
            && common_metadata.is_some()
            && self
                .members
                .iter()
                .zip(&self.generation.members)
                .all(|(accepted, member)| {
                    accepted.member == *member
                        && accepted.identity
                            == self
                                .generation
                                .member_identity(&member.record_session_id)
                                .cloned()
                        && accepted.metadata.is_complete()
                        && Some(&accepted.metadata) == common_metadata
                        && accepted.metadata.created_at_ms == self.created_at_ms
                        && accepted.metadata.bounce_id == self.bounce_id
                        && accepted.metadata.wav_hash.as_deref() == Some(self.wav_hash.as_str())
                        && accepted.metadata.consumed_at_ms.is_none()
                        && accepted.metadata.consumed_by_session_id.is_none()
                })
    }
}

#[cfg(test)]
mod tests {
    use super::{artifact_sha256, generation_metadata_is_attestable};
    use crate::record_expected::ExpectedWavMetadata;

    #[test]
    fn artifact_fingerprint_commits_exact_bytes_and_unambiguous_identities() {
        let baseline = artifact_sha256(&[(
            vec!["member".into(), "a".into()],
            br#"{"ok":true}"#.to_vec(),
        )]);
        let whitespace = artifact_sha256(&[(
            vec!["member".into(), "a".into()],
            br#"{ "ok": true }"#.to_vec(),
        )]);
        let other_identity = artifact_sha256(&[(
            vec!["member".into(), "b".into()],
            br#"{"ok":true}"#.to_vec(),
        )]);
        let split = artifact_sha256(&[
            (vec!["member".into(), "a".into()], b"ab".to_vec()),
            (vec!["member".into(), "b".into()], b"c".to_vec()),
        ]);
        let joined = artifact_sha256(&[(vec!["member".into(), "a".into()], b"abc".to_vec())]);

        assert_ne!(baseline, whitespace);
        assert_ne!(baseline, other_identity);
        assert_ne!(split, joined);
    }

    #[test]
    fn immutable_generation_attestation_survives_a_delayed_restart() {
        let now_ms = 1_800_000_000_000;
        let metadata = ExpectedWavMetadata {
            expected_duration_samples: 48_000,
            expected_sample_rate: 48_000,
            wav_time_reference_samples: Some(0),
            wav_path: "/tmp/drop.wav".to_string(),
            bounce_id: "bounce-delayed-restart".to_string(),
            created_at_ms: now_ms - 11 * 60 * 1_000,
            wav_file_size: Some(192_044),
            wav_mtime_ms: now_ms - 11 * 60 * 1_000,
            wav_hash: Some("a".repeat(64)),
            consumed_at_ms: None,
            consumed_by_session_id: None,
        };

        assert!(generation_metadata_is_attestable(&metadata, now_ms));
        let mut future = metadata;
        future.created_at_ms = now_ms + 60 * 1_000 + 1;
        assert!(!generation_metadata_is_attestable(&future, now_ms));
    }
}
