use super::*;

#[test]
fn diagnostics_separate_missing_unready_and_wrong_generation_writers() {
    let temp = tempfile::tempdir().unwrap();
    let generation = CaptureGeneration::new_single(
        "project".into(),
        "post".into(),
        "pre".into(),
        "daw".into(),
        std::process::id(),
    );
    let report = ready::ProducerWaitReport {
        total: 2,
        ..Default::default()
    };
    let snapshot = || producer_readiness_diagnostic(temp.path(), &generation, &report);
    let missing = snapshot();
    assert_eq!(missing["members"][0]["pre_writer"]["claim_present"], false);
    assert_eq!(missing["members"][0]["post_writer"]["claim_present"], false);
    let reason = failure_reason(&missing);
    assert!(reason.contains("PRE writer missing") && reason.contains("POST writer missing"));

    let member = &generation.members[0];
    let claim = |role, instance: &str, id: &str, started| {
        crate::record_writer_claim::claim_writer_for_generation(
            temp.path(),
            "project",
            &member.record_session_id,
            role,
            instance,
            id,
            started,
        )
        .unwrap()
    };
    let mut pre = claim(
        Role::Pre,
        "pre",
        &generation.capture_generation_id,
        generation.started_at_ms,
    );
    let unready = snapshot();
    let pre_state = &unready["members"][0]["pre_writer"];
    assert_eq!(pre_state["claim_present"], true);
    assert_eq!(pre_state["generation_matches"], true);
    assert_eq!(pre_state["artifact_ready"], false);
    pre.mark_ready().unwrap();
    let wrong_id = uuid::Uuid::new_v4().to_string();
    let mut post = claim(Role::Post, "post", &wrong_id, generation.started_at_ms + 1);
    post.mark_ready().unwrap();
    let mismatched = snapshot();
    assert_eq!(
        mismatched["members"][0]["pre_writer"]["artifact_ready"],
        true
    );
    assert_eq!(mismatched["members"][0]["pre_writer"]["closed"], false);
    assert_eq!(
        mismatched["members"][0]["post_writer"]["artifact_ready"],
        true
    );
    assert_eq!(
        mismatched["members"][0]["post_writer"]["generation_matches"],
        false
    );
    assert_eq!(
        mismatched["members"][0]["post_writer"]["started_at_matches"],
        false
    );
    assert!(failure_reason(&mismatched).contains("POST generation mismatch"));
    pre.mark_closed().unwrap();
    assert_eq!(snapshot()["members"][0]["pre_writer"]["closed"], true);
    assert!(failure_reason(&snapshot()).contains("PRE writer closed"));
}
