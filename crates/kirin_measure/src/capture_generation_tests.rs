use super::*;

#[test]
fn one_generation_is_published_identically_to_every_project() {
    let temp = tempfile::tempdir().unwrap();
    let generation = CaptureGeneration::new_for_members(
        "post-a".into(),
        "daw-a".into(),
        42,
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
            let mut guard = crate::record_writer_claim::claim_writer(
                temp.path(),
                &member.project_hash,
                &member.record_session_id,
                role,
                instance_id,
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
