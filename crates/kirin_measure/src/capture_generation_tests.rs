use super::*;

fn generation_with_member_count(member_count: usize) -> CaptureGeneration {
    CaptureGeneration::new_for_members(
        "post-0".into(),
        "daw-a".into(),
        42,
        (0..member_count)
            .map(|index| CaptureGenerationMember {
                project_hash: "project-a".into(),
                post_instance_id: format!("post-{index}"),
                pre_instance_id: format!("pre-{index}"),
                record_session_id: String::new(),
            })
            .collect(),
    )
}

#[test]
fn generation_accepts_twelve_exact_pairs_and_rejects_thirteen() {
    assert_eq!(MAX_CAPTURE_GENERATION_MEMBERS, 12);
    assert!(generation_with_member_count(12).is_valid());
    assert!(
        !generation_with_member_count(13).is_valid(),
        "producer must reject a roster the atomic consumer cannot display"
    );
}

#[test]
fn one_generation_is_published_identically_to_every_project() {
    let temp = tempfile::tempdir().unwrap();
    let generation = CaptureGeneration::new_for_members(
        "post-a".into(),
        "daw-a".into(),
        std::process::id(),
        vec![
            CaptureGenerationMember {
                project_hash: "project-a".into(),
                post_instance_id: "post-a".into(),
                pre_instance_id: "pre-a".into(),
                record_session_id: String::new(),
            },
            CaptureGenerationMember {
                project_hash: "project-b".into(),
                post_instance_id: "post-b".into(),
                pre_instance_id: "pre-b".into(),
                record_session_id: String::new(),
            },
        ],
    );

    let mut transaction =
        crate::capture_generation_tx::CaptureGenerationTransaction::begin(temp.path(), &generation)
            .unwrap();
    transaction.stage().unwrap();
    let mut guards = Vec::new();
    for member in &generation.members {
        for (role, instance_id) in [
            (
                crate::plugin_data::Role::Pre,
                member.pre_instance_id.as_str(),
            ),
            (
                crate::plugin_data::Role::Post,
                member.post_instance_id.as_str(),
            ),
        ] {
            let mut guard = crate::record_writer_claim::claim_writer_for_generation(
                temp.path(),
                &member.project_hash,
                &member.record_session_id,
                role,
                instance_id,
                &generation.capture_generation_id,
                generation.started_at_ms,
            )
            .unwrap();
            guard.mark_ready().unwrap();
            guards.push(guard);
        }
    }
    transaction
        .commit_when_ready(std::time::Duration::ZERO)
        .unwrap();

    assert_eq!(
        read_current_generation(temp.path(), "project-a")
            .unwrap()
            .unwrap(),
        generation
    );
    assert_eq!(
        read_current_generation(temp.path(), "project-b")
            .unwrap()
            .unwrap(),
        generation
    );
    assert_eq!(
        read_active_generation(temp.path()).unwrap().unwrap(),
        generation
    );
}

#[test]
fn generation_roster_is_sorted_and_member_addressable() {
    let generation = CaptureGeneration::new_for_members(
        "post-b".into(),
        "daw-a".into(),
        42,
        vec![
            CaptureGenerationMember {
                project_hash: "project-b".into(),
                post_instance_id: "post-b".into(),
                pre_instance_id: "pre-b".into(),
                record_session_id: String::new(),
            },
            CaptureGenerationMember {
                project_hash: "project-a".into(),
                post_instance_id: "post-a".into(),
                pre_instance_id: "pre-a".into(),
                record_session_id: String::new(),
            },
        ],
    );
    assert!(generation.is_valid());
    assert!(generation
        .members
        .iter()
        .all(|member| Uuid::parse_str(&member.record_session_id).is_ok()));
    assert_eq!(generation.members[0].post_instance_id, "post-a");
    assert_eq!(
        generation
            .member("project-b", "post-b")
            .map(|member| member.pre_instance_id.as_str()),
        Some("pre-b")
    );
    assert_eq!(
        generation.member_identities[0].channel_key,
        generation.members[0].pre_instance_id
    );
    assert_eq!(
        generation.member_identities[1].channel_key,
        generation.members[1].pre_instance_id
    );
}

#[test]
fn generation_separates_stable_channel_identity_from_optional_role() {
    let generation = CaptureGeneration::new_for_named_members(
        "post-mix".into(),
        "daw-a".into(),
        42,
        vec![
            (
                CaptureGenerationMember {
                    project_hash: "project-a".into(),
                    post_instance_id: "post-mix".into(),
                    pre_instance_id: "persisted-pre-uuid".into(),
                    record_session_id: String::new(),
                },
                Some("2Mix".into()),
            ),
            (
                CaptureGenerationMember {
                    project_hash: "project-a".into(),
                    post_instance_id: "post-user".into(),
                    pre_instance_id: "persisted-user-pre-uuid".into(),
                    record_session_id: String::new(),
                },
                Some("Blue Mycelium 07".into()),
            ),
        ],
    );

    assert!(generation.is_valid());
    let mix = generation
        .member_identities
        .iter()
        .find(|identity| identity.display_name.as_deref() == Some("2Mix"))
        .unwrap();
    assert_eq!(mix.channel_key, "persisted-pre-uuid");
    assert_eq!(mix.channel_role.as_deref(), Some("bus_mix"));
    let arbitrary = generation
        .member_identities
        .iter()
        .find(|identity| identity.display_name.as_deref() == Some("Blue Mycelium 07"))
        .unwrap();
    assert_eq!(arbitrary.channel_key, "persisted-user-pre-uuid");
    assert_eq!(arbitrary.channel_role, None);
}

#[test]
fn serialized_generation_carries_the_complete_consumer_identity_contract() {
    let generation = CaptureGeneration::new_single_named(
        "project-a".into(),
        "post-a".into(),
        "persisted-pre-a".into(),
        "daw-a".into(),
        42,
        Some("  Drum  ".into()),
    );

    let json = serde_json::to_value(&generation).unwrap();
    let member = &json["members"][0];
    let identity = &json["member_identities"][0];
    assert_eq!(identity["pair_key"], member["record_session_id"]);
    assert_eq!(identity["channel_key"], member["pre_instance_id"]);
    assert_eq!(identity["display_name"], "Drum");
    assert_eq!(identity["channel_role"], "bus_drum");
}

#[test]
fn generation_rejects_two_posts_targeting_the_same_pre() {
    let generation = CaptureGeneration::new_for_members(
        "post-a".into(),
        "daw-a".into(),
        42,
        vec![
            CaptureGenerationMember {
                project_hash: "project-a".into(),
                post_instance_id: "post-a".into(),
                pre_instance_id: "pre-shared".into(),
                record_session_id: String::new(),
            },
            CaptureGenerationMember {
                project_hash: "project-a".into(),
                post_instance_id: "post-b".into(),
                pre_instance_id: "pre-shared".into(),
                record_session_id: String::new(),
            },
        ],
    );

    assert!(
        !generation.is_valid(),
        "one PRE inbox cannot belong to two POST members in one generation"
    );
}

#[test]
fn generation_rejects_a_member_without_an_exact_pre() {
    let generation = CaptureGeneration::new_for_members(
        "post-a".into(),
        "daw-a".into(),
        42,
        vec![CaptureGenerationMember {
            project_hash: "project-a".into(),
            post_instance_id: "post-a".into(),
            pre_instance_id: String::new(),
            record_session_id: String::new(),
        }],
    );

    assert!(
        !generation.is_valid(),
        "a consumer-visible generation must never contain an unresolved PRE"
    );
}

#[test]
fn invalid_pointer_is_not_promoted_to_a_generation() {
    let temp = tempfile::tempdir().unwrap();
    let path = current_generation_path(temp.path(), "project-a");
    std::fs::create_dir_all(path.parent().unwrap()).unwrap();
    std::fs::write(path, br#"{"schema_version":"capture_generation.v1"}"#).unwrap();

    assert!(matches!(
        read_current_generation(temp.path(), "project-a"),
        Err(CaptureGenerationError::Serde(_))
    ));
}

#[test]
fn stop_targets_preparing_member_before_retained_active_member() {
    let temp = tempfile::tempdir().unwrap();
    let active = CaptureGeneration::new_single(
        "project-a".into(),
        "post-a".into(),
        "pre-old".into(),
        "daw-a".into(),
        std::process::id(),
    );
    crate::atomic_file::write_bytes_atomic(
        &active_generation_path(temp.path()),
        &serde_json::to_vec(&active).unwrap(),
    )
    .unwrap();

    let preparing = CaptureGeneration::new_single(
        "project-a".into(),
        "post-a".into(),
        "pre-new".into(),
        "daw-a".into(),
        std::process::id(),
    );
    let mut transaction =
        crate::capture_generation_tx::CaptureGenerationTransaction::begin(temp.path(), &preparing)
            .unwrap();
    transaction.stage().unwrap();

    assert_eq!(
        read_stop_target_generation(temp.path(), "project-a", "post-a")
            .unwrap()
            .unwrap(),
        preparing,
        "Stop must address the producer-owned generation during prepare→commit"
    );
    drop(transaction);
    assert_eq!(
        read_stop_target_generation(temp.path(), "project-a", "post-a")
            .unwrap()
            .unwrap(),
        active,
        "without a matching preparation, retained active is the exact fallback"
    );
}

#[test]
fn stop_target_requires_the_exact_project_and_post_member() {
    let temp = tempfile::tempdir().unwrap();
    let generation = CaptureGeneration::new_single(
        "project-a".into(),
        "post-shared".into(),
        "pre-a".into(),
        "daw-a".into(),
        std::process::id(),
    );
    crate::atomic_file::write_bytes_atomic(
        &active_generation_path(temp.path()),
        &serde_json::to_vec(&generation).unwrap(),
    )
    .unwrap();

    assert!(
        read_stop_target_generation(temp.path(), "project-b", "post-shared")
            .unwrap()
            .is_none()
    );
    assert_eq!(
        read_stop_target_generation(temp.path(), "project-a", "post-shared")
            .unwrap()
            .unwrap(),
        generation
    );
}

#[test]
fn terminal_generation_never_rearms_after_cache_loss_or_io_restart() {
    let temp = tempfile::tempdir().unwrap();
    let generation = CaptureGeneration::new_single(
        "project-a".into(),
        "post-a".into(),
        "pre-a".into(),
        "daw-a".into(),
        std::process::id(),
    );
    publish_current_generation(temp.path(), "project-a", &generation).unwrap();
    crate::atomic_file::write_bytes_atomic(
        &preparing_generation_path(temp.path()),
        &serde_json::to_vec(&generation).unwrap(),
    )
    .unwrap();
    crate::atomic_file::write_bytes_atomic(
        &active_generation_path(temp.path()),
        &serde_json::to_vec(&generation).unwrap(),
    )
    .unwrap();

    assert!(read_producer_authorized_generation(
        temp.path(),
        "project-a",
        &generation.capture_generation_id,
        generation.started_at_ms,
    )
    .unwrap()
    .is_some());

    crate::capture_generation_lifecycle::mark_generation_terminal(
        temp.path(),
        &generation,
        crate::capture_generation_lifecycle::GenerationTerminalReason::DropCommitted,
    )
    .unwrap();

    // active/current are consumer evidence for Kirin OS and intentionally survive. A newly
    // started IO worker has no in-memory processed-broadcast cache, yet the durable terminal fact
    // remains the sole start-authority check and makes both reads non-authoritative.
    assert_eq!(
        read_active_generation(temp.path()).unwrap(),
        Some(generation.clone())
    );
    assert_eq!(
        read_current_generation(temp.path(), "project-a").unwrap(),
        Some(generation.clone())
    );
    for _fresh_io_worker in 0..2 {
        assert!(read_producer_authorized_generation(
            temp.path(),
            "project-a",
            &generation.capture_generation_id,
            generation.started_at_ms,
        )
        .unwrap()
        .is_none());
    }
}

#[test]
fn next_keep_requires_and_accepts_a_new_generation_and_session() {
    let temp = tempfile::tempdir().unwrap();
    let first = CaptureGeneration::new_single(
        "project-a".into(),
        "post-a".into(),
        "pre-a".into(),
        "daw-a".into(),
        std::process::id(),
    );
    crate::capture_generation_lifecycle::mark_generation_terminal(
        temp.path(),
        &first,
        crate::capture_generation_lifecycle::GenerationTerminalReason::AllStop,
    )
    .unwrap();

    let second = CaptureGeneration::new_single(
        "project-a".into(),
        "post-a".into(),
        "pre-a".into(),
        "daw-a".into(),
        std::process::id(),
    );
    assert_ne!(second.capture_generation_id, first.capture_generation_id);
    assert_ne!(
        second.members[0].record_session_id,
        first.members[0].record_session_id
    );
    publish_current_generation(temp.path(), "project-a", &second).unwrap();
    crate::atomic_file::write_bytes_atomic(
        &preparing_generation_path(temp.path()),
        &serde_json::to_vec(&second).unwrap(),
    )
    .unwrap();

    assert_eq!(
        read_producer_authorized_generation(
            temp.path(),
            "project-a",
            &second.capture_generation_id,
            second.started_at_ms,
        )
        .unwrap(),
        Some(second)
    );
}
