use super::*;
use crate::record_expected::{
    begin_expected_session, claim_expected_metadata_for_session, mark_expected_metadata_consumed,
    read_claim_marker_for_session, write_expected_metadata,
};
use std::collections::BTreeMap;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, Barrier};

fn isolated_dir() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let dir = std::env::temp_dir().join(format!(
        "kirin_record_drop_commit_test_{}_{}",
        std::process::id(),
        n
    ));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn metadata_fixture(bounce_id: &str) -> ExpectedWavMetadata {
    // A Drop is produced after Keep has published every member marker. Keep the fixture safely on
    // that side of the ordering boundary even on a coarse/loaded test clock.
    let now = chrono::Utc::now().timestamp_millis() + 1_000;
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

fn write_drop_commit(
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

fn generation_fixture(member_count: usize) -> crate::capture_generation::CaptureGeneration {
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

fn multi_project_generation_fixture(
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

fn prepare_generation_drop(
    base: &Path,
    generation: &crate::capture_generation::CaptureGeneration,
    included_members: usize,
    metadata: &ExpectedWavMetadata,
) {
    prepare_generation_roster(base, generation);
    write_generation_drop_artifacts(base, generation, included_members, metadata);
}

fn prepare_generation_roster(
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

fn write_generation_drop_artifacts(
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

fn assert_partial_generation_drop_is_inert(member_count: usize) {
    let base = isolated_dir();
    let generation = generation_fixture(member_count);
    let metadata = metadata_fixture(&format!("partial-{member_count}"));
    prepare_generation_drop(&base, &generation, member_count - 1, &metadata);
    let first = &generation.members[0];

    assert_eq!(
        inspect_drop_commit_for_open_session(&base, &first.project_hash, &first.record_session_id,)
            .unwrap(),
        None,
        "a partial Drop manifest must not bind any kept pair",
    );
    assert!(
        crate::capture_generation_lifecycle::read_generation_terminal(
            &base,
            &generation.capture_generation_id,
            generation.started_at_ms,
        )
        .unwrap()
        .is_none()
    );
}

#[test]
fn three_pair_drop_rejects_a_two_pair_manifest_atomically() {
    assert_partial_generation_drop_is_inert(3);
}

#[test]
fn twelve_pair_drop_rejects_an_eleven_pair_manifest_atomically() {
    assert_partial_generation_drop_is_inert(12);
}

#[test]
fn complete_three_pair_drop_authorizes_every_exact_member() {
    let base = isolated_dir();
    let generation = generation_fixture(3);
    let metadata = metadata_fixture("complete-three");
    prepare_generation_drop(&base, &generation, 3, &metadata);

    for member in &generation.members {
        assert_eq!(
            inspect_drop_commit_for_open_session(
                &base,
                &member.project_hash,
                &member.record_session_id,
            )
            .unwrap(),
            Some(metadata.clone()),
        );
    }
}

#[test]
fn complete_twelve_pair_drop_snapshots_and_authorizes_every_exact_member() {
    let base = isolated_dir();
    let generation = generation_fixture(12);
    let metadata = metadata_fixture("complete-twelve");
    prepare_generation_drop(&base, &generation, 12, &metadata);

    for member in &generation.members {
        assert_eq!(
            inspect_drop_commit_for_open_session(
                &base,
                &member.project_hash,
                &member.record_session_id,
            )
            .unwrap(),
            Some(metadata.clone()),
        );
    }
    let acceptance: DropRecordGenerationAcceptance = serde_json::from_slice(
        &fs::read(drop_generation_acceptance_path(
            &base,
            &generation.capture_generation_id,
            generation.started_at_ms,
        ))
        .unwrap(),
    )
    .unwrap();
    assert_eq!(acceptance.generation, generation);
    assert_eq!(acceptance.members.len(), 12);
    assert!(acceptance
        .members
        .iter()
        .all(|member| member.metadata == metadata));
    assert_eq!(acceptance.exact_artifacts_sha256.len(), 64);
}

#[test]
fn complete_multi_project_drop_requires_and_snapshots_every_project_transaction() {
    let base = isolated_dir();
    let generation = multi_project_generation_fixture(12);
    let metadata = metadata_fixture("complete-multi-project");
    prepare_generation_drop(&base, &generation, 12, &metadata);

    for member in &generation.members {
        assert_eq!(
            inspect_drop_commit_for_open_session(
                &base,
                &member.project_hash,
                &member.record_session_id,
            )
            .unwrap(),
            Some(metadata.clone()),
        );
    }
}

#[test]
fn older_closed_generation_remains_drop_authorized_after_next_keep_and_restart() {
    let base = isolated_dir();
    let first = generation_fixture(3);
    let first_metadata = metadata_fixture("first-after-second-keep");
    prepare_generation_roster(&base, &first);
    for member in &first.members {
        crate::record_expected::mark_expected_metadata_consumed(
            &base,
            &member.project_hash,
            None,
            &member.record_session_id,
        )
        .unwrap();
    }

    let second = generation_fixture(3);
    crate::atomic_file::write_bytes_atomic(
        &crate::capture_generation::active_generation_path(&base),
        &serde_json::to_vec(&second).unwrap(),
    )
    .unwrap();
    crate::capture_generation::archive_generation(&base, &second).unwrap();
    write_generation_drop_artifacts(&base, &first, 3, &first_metadata);

    // Resolve solely from durable paths, as a restarted consumer would. The next active Keep is
    // only a discovery pointer and cannot hide or retarget the older immutable generation.
    for member in &first.members {
        let inspected = inspect_drop_commit_for_closed_pair_session(
            &base,
            &member.project_hash,
            &member.record_session_id,
        )
        .unwrap()
        .expect("closed generation remains Drop-authorized");
        assert_eq!(inspected, first_metadata);
        assert_eq!(
            bind_drop_commit_for_closed_pair_session(
                &base,
                &member.project_hash,
                &member.record_session_id,
                &inspected,
            )
            .unwrap(),
            Some(first_metadata.clone()),
        );
    }
}

#[test]
fn first_generation_wav_acceptance_is_absorbing_for_every_member() {
    let base = isolated_dir();
    let generation = generation_fixture(3);
    let first_metadata = metadata_fixture("accepted-wav-a");
    prepare_generation_drop(&base, &generation, 3, &first_metadata);
    let first_member = &generation.members[0];
    assert_eq!(
        inspect_drop_commit_for_open_session(
            &base,
            &first_member.project_hash,
            &first_member.record_session_id,
        )
        .unwrap(),
        Some(first_metadata.clone()),
    );
    let acceptance_path = drop_generation_acceptance_path(
        &base,
        &generation.capture_generation_id,
        generation.started_at_ms,
    );
    let accepted_bytes = fs::read(&acceptance_path).unwrap();

    let replacement = metadata_fixture("replacement-wav-b");
    prepare_generation_drop(&base, &generation, 3, &replacement);
    for member in &generation.members[1..] {
        assert_eq!(
            inspect_drop_commit_for_open_session(
                &base,
                &member.project_hash,
                &member.record_session_id,
            )
            .unwrap(),
            Some(first_metadata.clone()),
            "accepted members must resolve from the immutable snapshot, not replaced artifacts",
        );
    }
    let replacement_commit_id = format!("drop-{}", replacement.bounce_id);
    fs::remove_file(drop_generation_transaction_path(
        &base,
        &replacement_commit_id,
    ))
    .unwrap();
    fs::remove_file(drop_transaction_path(
        &base,
        "project-a",
        &replacement_commit_id,
    ))
    .unwrap();
    for member in &generation.members {
        fs::remove_file(drop_commit_path(
            &base,
            &member.project_hash,
            &member.record_session_id,
        ))
        .unwrap();
        assert_eq!(
            inspect_drop_commit_for_open_session(
                &base,
                &member.project_hash,
                &member.record_session_id,
            )
            .unwrap(),
            Some(first_metadata.clone()),
            "an accepted retry must not depend on mutable transport artifacts still existing",
        );
    }
    assert_eq!(fs::read(acceptance_path).unwrap(), accepted_bytes);
}

#[test]
fn inconsistent_artifact_replacement_before_acceptance_rejects_the_whole_generation() {
    let base = isolated_dir();
    let generation = generation_fixture(3);
    let metadata = metadata_fixture("coherent-before-replacement");
    prepare_generation_drop(&base, &generation, 3, &metadata);
    let replaced = &generation.members[2];
    let path = drop_commit_path(&base, &replaced.project_hash, &replaced.record_session_id);
    let mut commit: DropRecordCommit = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    commit.metadata = metadata_fixture("incoherent-member");
    commit.created_at_ms = commit.metadata.created_at_ms;
    crate::atomic_file::write_bytes_atomic(&path, &serde_json::to_vec(&commit).unwrap()).unwrap();

    for member in &generation.members {
        assert_eq!(
            inspect_drop_commit_for_open_session(
                &base,
                &member.project_hash,
                &member.record_session_id,
            )
            .unwrap(),
            None,
        );
    }
    assert!(!drop_generation_acceptance_path(
        &base,
        &generation.capture_generation_id,
        generation.started_at_ms,
    )
    .exists());
}

#[test]
fn concurrent_member_binds_publish_one_acceptance_and_complete_the_full_roster() {
    let base = isolated_dir();
    let generation = generation_fixture(12);
    let metadata = metadata_fixture("concurrent-twelve");
    prepare_generation_drop(&base, &generation, 12, &metadata);
    let barrier = Arc::new(Barrier::new(generation.members.len()));
    let mut handles = Vec::new();
    for member in generation.members.clone() {
        let base = base.clone();
        let barrier = Arc::clone(&barrier);
        let metadata = metadata.clone();
        handles.push(std::thread::spawn(move || {
            barrier.wait();
            assert_eq!(
                bind_drop_commit_for_open_session(
                    &base,
                    &member.project_hash,
                    &member.record_session_id,
                )
                .unwrap(),
                Some(metadata),
            );
        }));
    }
    for handle in handles {
        handle.join().unwrap();
    }
    let acceptance_path = drop_generation_acceptance_path(
        &base,
        &generation.capture_generation_id,
        generation.started_at_ms,
    );
    let first_bytes = fs::read(&acceptance_path).unwrap();
    for member in &generation.members {
        let marker =
            read_claim_marker_for_session(&base, &member.project_hash, &member.record_session_id)
                .unwrap()
                .unwrap();
        assert_eq!(marker.metadata, Some(metadata.clone()));
        assert_eq!(
            inspect_drop_commit_for_open_session(
                &base,
                &member.project_hash,
                &member.record_session_id,
            )
            .unwrap(),
            Some(metadata.clone()),
        );
    }
    assert_eq!(fs::read(acceptance_path).unwrap(), first_bytes);
}

#[test]
fn drop_commit_binds_only_the_exact_open_session() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    begin_expected_session(&base, "project-a", "session-b").unwrap();
    let metadata = metadata_fixture("drop-exact-session");
    write_drop_commit(&base, "project-a", "session-a", &metadata);

    assert_eq!(
        bind_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        Some(metadata.clone())
    );
    assert_eq!(
        bind_drop_commit_for_open_session(&base, "project-a", "session-b").unwrap(),
        None
    );
    assert_eq!(
        read_claim_marker_for_session(&base, "project-a", "session-a")
            .unwrap()
            .unwrap()
            .metadata,
        Some(metadata)
    );
}

#[test]
fn validated_generation_drop_publishes_durable_terminal_state() {
    let base = isolated_dir();
    let generation = crate::capture_generation::CaptureGeneration::new_single(
        "project-a".into(),
        "post-a".into(),
        "pre-a".into(),
        "daw-a".into(),
        std::process::id(),
    );
    let session_id = generation.members[0].record_session_id.clone();
    crate::atomic_file::write_bytes_atomic(
        &crate::capture_generation::active_generation_path(&base),
        &serde_json::to_vec(&generation).unwrap(),
    )
    .unwrap();
    crate::capture_generation::archive_generation(&base, &generation).unwrap();
    crate::record_expected::begin_expected_session_for_generation(
        &base,
        "project-a",
        &session_id,
        "post-a",
        &generation.capture_generation_id,
        generation.started_at_ms,
    )
    .unwrap();
    let metadata = metadata_fixture("drop-generation-terminal");
    let drop_commit_id = "drop-generation-terminal".to_string();
    let commit = DropRecordCommit {
        schema_version: DROP_COMMIT_SCHEMA.into(),
        drop_commit_id: drop_commit_id.clone(),
        project_hash: "project-a".into(),
        record_session_id: session_id.clone(),
        capture_generation_id: generation.capture_generation_id.clone(),
        generation_started_at_ms: generation.started_at_ms,
        created_at_ms: metadata.created_at_ms,
        metadata: metadata.clone(),
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_commit_path(&base, "project-a", &session_id),
        &serde_json::to_vec(&commit).unwrap(),
    )
    .unwrap();
    let transaction = DropRecordTransaction {
        schema_version: DROP_TRANSACTION_SCHEMA.into(),
        drop_commit_id: drop_commit_id.clone(),
        project_hash: "project-a".into(),
        capture_generation_id: generation.capture_generation_id.clone(),
        generation_started_at_ms: generation.started_at_ms,
        created_at_ms: metadata.created_at_ms,
        bounce_id: metadata.bounce_id.clone(),
        wav_hash: metadata.wav_hash.clone().unwrap(),
        record_session_ids: vec![session_id.clone()],
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_transaction_path(&base, "project-a", &drop_commit_id),
        &serde_json::to_vec(&transaction).unwrap(),
    )
    .unwrap();
    let generation_transaction = DropRecordGenerationTransaction {
        schema_version: DROP_GENERATION_TRANSACTION_SCHEMA.into(),
        drop_commit_id: drop_commit_id.clone(),
        capture_generation_id: generation.capture_generation_id.clone(),
        generation_started_at_ms: generation.started_at_ms,
        created_at_ms: metadata.created_at_ms,
        bounce_id: metadata.bounce_id.clone(),
        wav_hash: metadata.wav_hash.clone().unwrap(),
        projects: vec![DropRecordGenerationProject {
            project_hash: "project-a".into(),
            record_session_ids: vec![session_id.clone()],
        }],
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_generation_transaction_path(&base, &drop_commit_id),
        &serde_json::to_vec(&generation_transaction).unwrap(),
    )
    .unwrap();

    assert_eq!(
        bind_drop_commit_for_open_session(&base, "project-a", &session_id).unwrap(),
        Some(metadata)
    );
    let terminal = crate::capture_generation_lifecycle::read_generation_terminal(
        &base,
        &generation.capture_generation_id,
        generation.started_at_ms,
    )
    .unwrap()
    .expect("validated Drop is a durable generation terminal");
    assert_eq!(
        terminal.terminal_reason,
        crate::capture_generation_lifecycle::GenerationTerminalReason::DropCommitted
    );
}

#[test]
fn drop_commit_cannot_reopen_or_retarget_a_closed_session() {
    let base = isolated_dir();
    let first = metadata_fixture("drop-closed-first");
    write_expected_metadata(&base, "project-a", &first).unwrap();
    claim_expected_metadata_for_session(&base, "project-a", "session-a").unwrap();
    mark_expected_metadata_consumed(&base, "project-a", Some(&first.bounce_id), "session-a")
        .unwrap();
    let later = metadata_fixture("drop-closed-later");
    write_drop_commit(&base, "project-a", "session-a", &later);

    assert_eq!(
        bind_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
    assert_eq!(
        read_claim_marker_for_session(&base, "project-a", "session-a")
            .unwrap()
            .unwrap()
            .metadata,
        Some(first)
    );
}

#[test]
fn exact_drop_can_complete_a_metadata_free_closed_pair_lifecycle() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    mark_expected_metadata_consumed(&base, "project-a", None, "session-a").unwrap();
    let closed_at_ms = read_claim_marker_for_session(&base, "project-a", "session-a")
        .unwrap()
        .unwrap()
        .closed_at_ms
        .expect("closed lifecycle");
    let metadata = metadata_fixture("drop-after-stop");
    write_drop_commit(&base, "project-a", "session-a", &metadata);

    assert_eq!(
        inspect_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
    let inspected = inspect_drop_commit_for_closed_pair_session(&base, "project-a", "session-a")
        .unwrap()
        .expect("exact closed-pair Drop");
    assert_eq!(inspected, metadata);
    assert_eq!(
        bind_drop_commit_for_closed_pair_session(&base, "project-a", "session-a", &inspected,)
            .unwrap(),
        Some(metadata.clone())
    );
    let marker = read_claim_marker_for_session(&base, "project-a", "session-a")
        .unwrap()
        .unwrap();
    assert_eq!(marker.metadata, Some(metadata));
    assert_eq!(marker.closed_at_ms, Some(closed_at_ms));
}

#[test]
fn closed_pair_bind_rejects_metadata_replaced_after_inspection() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    let first = metadata_fixture("drop-inspected-first");
    write_drop_commit(&base, "project-a", "session-a", &first);
    let inspected = inspect_drop_commit_for_closed_pair_session(&base, "project-a", "session-a")
        .unwrap()
        .unwrap();

    let later = metadata_fixture("drop-replaced-later");
    write_drop_commit(&base, "project-a", "session-a", &later);

    assert_eq!(
        bind_drop_commit_for_closed_pair_session(&base, "project-a", "session-a", &inspected,)
            .unwrap(),
        None
    );
    assert_eq!(
        read_claim_marker_for_session(&base, "project-a", "session-a")
            .unwrap()
            .unwrap()
            .metadata,
        None
    );
}

#[test]
fn drop_commit_is_inert_until_transaction_manifest_is_ready() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    let metadata = metadata_fixture("drop-not-ready");
    let commit = DropRecordCommit {
        schema_version: DROP_COMMIT_SCHEMA.to_string(),
        drop_commit_id: "drop-not-ready".to_string(),
        project_hash: "project-a".to_string(),
        record_session_id: "session-a".to_string(),
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        created_at_ms: metadata.created_at_ms,
        metadata,
    };
    crate::atomic_file::write_bytes_atomic(
        &drop_commit_path(&base, "project-a", "session-a"),
        &serde_json::to_vec(&commit).unwrap(),
    )
    .unwrap();

    assert_eq!(
        inspect_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
}

#[test]
fn project_transaction_is_inert_until_generation_manifest_is_ready() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    let metadata = metadata_fixture("drop-generation-not-ready");
    write_drop_commit(&base, "project-a", "session-a", &metadata);
    fs::remove_file(drop_generation_transaction_path(&base, "drop-session-a")).unwrap();

    assert_eq!(
        inspect_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
}

#[test]
fn transaction_cannot_authorize_a_session_not_in_its_batch() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    let metadata = metadata_fixture("drop-wrong-batch");
    write_drop_commit(&base, "project-a", "session-a", &metadata);
    let path = drop_transaction_path(&base, "project-a", "drop-session-a");
    let mut transaction: DropRecordTransaction =
        serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    transaction.record_session_ids = vec!["session-other".to_string()];
    crate::atomic_file::write_bytes_atomic(&path, &serde_json::to_vec(&transaction).unwrap())
        .unwrap();

    assert_eq!(
        inspect_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
}

#[test]
fn generation_transaction_cannot_omit_the_project_batch() {
    let base = isolated_dir();
    begin_expected_session(&base, "project-a", "session-a").unwrap();
    let metadata = metadata_fixture("drop-generation-wrong-batch");
    write_drop_commit(&base, "project-a", "session-a", &metadata);
    let path = drop_generation_transaction_path(&base, "drop-session-a");
    let mut transaction: DropRecordGenerationTransaction =
        serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    transaction.projects = vec![DropRecordGenerationProject {
        project_hash: "project-other".to_string(),
        record_session_ids: vec!["session-other".to_string()],
    }];
    crate::atomic_file::write_bytes_atomic(&path, &serde_json::to_vec(&transaction).unwrap())
        .unwrap();

    assert_eq!(
        inspect_drop_commit_for_open_session(&base, "project-a", "session-a").unwrap(),
        None
    );
}
