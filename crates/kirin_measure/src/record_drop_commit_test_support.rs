use super::*;
use std::collections::BTreeMap;

pub(super) struct FixtureDir(tempfile::TempDir);

impl std::ops::Deref for FixtureDir {
    type Target = Path;
    fn deref(&self) -> &Path {
        self.0.path()
    }
}

pub(super) fn isolated_dir() -> FixtureDir {
    let dir = tempfile::tempdir().unwrap();
    assert!(dir
        .path()
        .canonicalize()
        .unwrap()
        .starts_with(std::env::temp_dir().canonicalize().unwrap()));
    FixtureDir(dir)
}

pub(super) fn metadata_fixture(bounce_id: &str, marker_times: &[i64]) -> ExpectedWavMetadata {
    // Drop follows the published roster, independent of how long its writes took.
    let now = marker_times.iter().max().expect("published marker time") + 1;
    ExpectedWavMetadata {
        expected_duration_samples: 48_000,
        expected_sample_rate: 48_000,
        wav_time_reference_samples: Some(96_000),
        wav_path: format!("/tmp/{bounce_id}.wav"),
        bounce_id: bounce_id.to_string(),
        created_at_ms: now,
        wav_file_size: Some(1_000),
        wav_mtime_ms: now,
        wav_hash: Some(format!("hash-{bounce_id}")),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    }
}

pub(super) fn write_drop_commit(
    base: &Path,
    project_hash: &str,
    session_id: &str,
    metadata: &ExpectedWavMetadata,
) {
    let drop_commit_id = format!("drop-{session_id}");
    let commit = DropRecordCommit {
        schema_version: DROP_COMMIT_SCHEMA.to_string(),
        drop_commit_id: drop_commit_id.clone(),
        project_hash: project_hash.to_string(),
        record_session_id: session_id.to_string(),
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        created_at_ms: metadata.created_at_ms,
        metadata: metadata.clone(),
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_commit_path(base, project_hash, session_id),
        &serde_json::to_vec(&commit).unwrap(),
    )
    .unwrap();
    let transaction = DropRecordTransaction {
        schema_version: DROP_TRANSACTION_SCHEMA.to_string(),
        drop_commit_id,
        project_hash: project_hash.to_string(),
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        created_at_ms: metadata.created_at_ms,
        bounce_id: metadata.bounce_id.clone(),
        wav_hash: metadata.wav_hash.clone().unwrap(),
        record_session_ids: vec![session_id.to_string()],
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_transaction_path(base, project_hash, &transaction.drop_commit_id),
        &serde_json::to_vec(&transaction).unwrap(),
    )
    .unwrap();
    let generation_transaction = DropRecordGenerationTransaction {
        schema_version: DROP_GENERATION_TRANSACTION_SCHEMA.to_string(),
        drop_commit_id: transaction.drop_commit_id.clone(),
        capture_generation_id: transaction.capture_generation_id.clone(),
        generation_started_at_ms: transaction.generation_started_at_ms,
        created_at_ms: transaction.created_at_ms,
        bounce_id: transaction.bounce_id.clone(),
        wav_hash: transaction.wav_hash.clone(),
        projects: vec![DropRecordGenerationProject {
            project_hash: project_hash.to_string(),
            record_session_ids: transaction.record_session_ids.clone(),
        }],
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_generation_transaction_path(base, &generation_transaction.drop_commit_id),
        &serde_json::to_vec(&generation_transaction).unwrap(),
    )
    .unwrap();
}

pub(super) fn generation_fixture(
    member_count: usize,
) -> crate::capture_generation::CaptureGeneration {
    crate::capture_generation::CaptureGeneration::new_for_members(
        "post-0".into(),
        "daw-a".into(),
        std::process::id(),
        (0..member_count)
            .map(|index| crate::capture_generation::CaptureGenerationMember {
                project_hash: "project-a".into(),
                post_instance_id: format!("post-{index}"),
                pre_instance_id: format!("pre-{index}"),
                record_session_id: String::new(),
            })
            .collect(),
    )
}

pub(super) fn multi_project_generation_fixture(
    member_count: usize,
) -> crate::capture_generation::CaptureGeneration {
    crate::capture_generation::CaptureGeneration::new_for_members(
        "post-0".into(),
        "daw-a".into(),
        std::process::id(),
        (0..member_count)
            .map(|index| crate::capture_generation::CaptureGenerationMember {
                project_hash: format!("project-{}", index % 3),
                post_instance_id: format!("post-{index}"),
                pre_instance_id: format!("pre-{index}"),
                record_session_id: String::new(),
            })
            .collect(),
    )
}

pub(super) fn prepare_generation_drop(
    base: &Path,
    generation: &crate::capture_generation::CaptureGeneration,
    included_members: usize,
    bounce_id: &str,
) -> ExpectedWavMetadata {
    prepare_generation_roster(base, generation);
    let metadata = metadata_for_generation(base, generation, bounce_id);
    write_generation_drop_artifacts(base, generation, included_members, &metadata);
    metadata
}

pub(super) fn prepare_generation_roster(
    base: &Path,
    generation: &crate::capture_generation::CaptureGeneration,
) {
    crate::atomic_file::write_bytes_atomic(
        &crate::capture_generation::active_generation_path(base),
        &serde_json::to_vec(generation).unwrap(),
    )
    .unwrap();
    crate::capture_generation::archive_generation(base, generation).unwrap();
    for member in &generation.members {
        crate::record_expected::begin_expected_session_for_generation(
            base,
            &member.project_hash,
            &member.record_session_id,
            &member.post_instance_id,
            &generation.capture_generation_id,
            generation.started_at_ms,
        )
        .unwrap();
    }
}

fn marker_time(base: &Path, project: &str, session: &str) -> i64 {
    let marker = read_claim_marker_for_session(base, project, session)
        .unwrap()
        .unwrap();
    marker
        .created_at_ms
        .max(marker.claimed_at_ms)
        .max(marker.closed_at_ms.unwrap_or(0))
}

pub(super) fn metadata_for_session(
    base: &Path,
    project: &str,
    session: &str,
    bounce: &str,
) -> ExpectedWavMetadata {
    metadata_fixture(bounce, &[marker_time(base, project, session)])
}

pub(super) fn metadata_for_generation(
    base: &Path,
    generation: &crate::capture_generation::CaptureGeneration,
    bounce: &str,
) -> ExpectedWavMetadata {
    let times: Vec<_> = generation
        .members
        .iter()
        .map(|member| marker_time(base, &member.project_hash, &member.record_session_id))
        .collect();
    metadata_fixture(bounce, &times)
}

pub(super) fn write_generation_drop_artifacts(
    base: &Path,
    generation: &crate::capture_generation::CaptureGeneration,
    included_members: usize,
    metadata: &ExpectedWavMetadata,
) {
    let drop_commit_id = format!("drop-{}", metadata.bounce_id);
    let mut included = generation
        .members
        .iter()
        .take(included_members)
        .collect::<Vec<_>>();
    included.sort_by(|left, right| left.record_session_id.cmp(&right.record_session_id));
    for member in &included {
        let commit = DropRecordCommit {
            schema_version: DROP_COMMIT_SCHEMA.into(),
            drop_commit_id: drop_commit_id.clone(),
            project_hash: member.project_hash.clone(),
            record_session_id: member.record_session_id.clone(),
            capture_generation_id: generation.capture_generation_id.clone(),
            generation_started_at_ms: generation.started_at_ms,
            created_at_ms: metadata.created_at_ms,
            metadata: metadata.clone(),
        };
        crate::atomic_file::write_bytes_atomic(
            &drop_commit_path(base, &member.project_hash, &member.record_session_id),
            &serde_json::to_vec(&commit).unwrap(),
        )
        .unwrap();
    }
    let mut projects = BTreeMap::<String, Vec<String>>::new();
    for member in included {
        projects
            .entry(member.project_hash.clone())
            .or_default()
            .push(member.record_session_id.clone());
    }
    for session_ids in projects.values_mut() {
        session_ids.sort();
    }
    for (project_hash, session_ids) in &projects {
        let project = DropRecordTransaction {
            schema_version: DROP_TRANSACTION_SCHEMA.into(),
            drop_commit_id: drop_commit_id.clone(),
            project_hash: project_hash.clone(),
            capture_generation_id: generation.capture_generation_id.clone(),
            generation_started_at_ms: generation.started_at_ms,
            created_at_ms: metadata.created_at_ms,
            bounce_id: metadata.bounce_id.clone(),
            wav_hash: metadata.wav_hash.clone().unwrap(),
            record_session_ids: session_ids.clone(),
        };
        crate::atomic_file::write_bytes_atomic(
            &drop_transaction_path(base, project_hash, &drop_commit_id),
            &serde_json::to_vec(&project).unwrap(),
        )
        .unwrap();
    }
    let transaction = DropRecordGenerationTransaction {
        schema_version: DROP_GENERATION_TRANSACTION_SCHEMA.into(),
        drop_commit_id: drop_commit_id.clone(),
        capture_generation_id: generation.capture_generation_id.clone(),
        generation_started_at_ms: generation.started_at_ms,
        created_at_ms: metadata.created_at_ms,
        bounce_id: metadata.bounce_id.clone(),
        wav_hash: metadata.wav_hash.clone().unwrap(),
        projects: projects
            .into_iter()
            .map(
                |(project_hash, record_session_ids)| DropRecordGenerationProject {
                    project_hash,
                    record_session_ids,
                },
            )
            .collect(),
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_generation_transaction_path(base, &drop_commit_id),
        &serde_json::to_vec(&transaction).unwrap(),
    )
    .unwrap();
}

#[test]
fn metadata_follows_the_latest_marker_without_a_wall_clock_allowance() {
    let metadata = metadata_fixture("ordered-drop", &[4_000, 9_000, 6_000]);
    assert_eq!(metadata.created_at_ms, 9_001);
    assert_eq!(metadata.wav_mtime_ms, 9_001);
}
