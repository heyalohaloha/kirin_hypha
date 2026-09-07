use super::*;

#[test]
fn same_name_preserves_exact_binding_and_generation() {
    let binding = PairBinding::new();
    binding.replace_name("mix".to_string());
    binding.set_exact_binding_for_test("pre-old");
    let before = binding.generation();

    let transition = binding.replace_name("mix".to_string());

    assert!(!transition.changed);
    assert_eq!(binding.generation(), before);
    assert_eq!(
        binding.recording_pre.lock().unwrap().as_deref(),
        Some("pre-old")
    );
    assert_eq!(
        binding
            .latched_pre
            .lock()
            .unwrap()
            .as_ref()
            .map(|p| p.instance_id.as_str()),
        Some("pre-old")
    );
}

#[test]
fn renamed_target_detaches_every_old_instance_state_atomically() {
    let binding = PairBinding::new();
    binding.replace_name("mix".to_string());
    binding.set_exact_binding_for_test("pre-old");

    let transition = binding.replace_name("master".to_string());

    assert!(transition.changed);
    assert_eq!(transition.previous_name, "mix");
    assert_eq!(
        transition.previous_pre_instance_id.as_deref(),
        Some("pre-old")
    );
    assert!(binding.recording_pre.lock().unwrap().is_none());
    assert!(binding.latched_pre.lock().unwrap().is_none());
    assert_eq!(binding.desired_name.read().unwrap().as_str(), "master");
}

#[test]
fn clearing_name_is_a_real_unpair() {
    let binding = PairBinding::new();
    binding.replace_name("mix".to_string());
    binding.set_exact_binding_for_test("pre-old");

    let transition = binding.replace_name(String::new());

    assert!(transition.changed);
    assert!(binding.desired_name.read().unwrap().is_empty());
    assert!(binding.recording_pre.lock().unwrap().is_none());
    assert!(binding.latched_pre.lock().unwrap().is_none());
}

#[test]
fn stale_self_check_generation_cannot_release_new_binding() {
    let binding = PairBinding::new();
    binding.replace_name("mix".to_string());
    let stale_generation = binding.generation();
    binding.replace_name("master".to_string());
    binding.replace_name("mix".to_string());
    binding.set_exact_binding_for_test("pre-new");

    assert!(!binding.release_if_current("mix", stale_generation));
    assert_eq!(binding.desired_name.read().unwrap().as_str(), "mix");
    assert_eq!(
        binding.recording_pre.lock().unwrap().as_deref(),
        Some("pre-new")
    );
}

#[test]
fn current_self_check_generation_releases_whole_binding() {
    let binding = PairBinding::new();
    binding.replace_name("mix".to_string());
    binding.set_exact_binding_for_test("pre-current");
    let generation = binding.generation();

    assert!(binding.release_if_current("mix", generation));
    assert!(binding.desired_name.read().unwrap().is_empty());
    assert!(binding.recording_pre.lock().unwrap().is_none());
    assert!(binding.latched_pre.lock().unwrap().is_none());
    assert_eq!(binding.status_snapshot(), (true, None));
    assert_ne!(binding.generation(), generation);
}

#[test]
fn unnamed_exact_pair_has_a_coherent_snapshot_until_explicit_clear() {
    let binding = PairBinding::new();
    let selected = LatchedPre {
        name: String::new(),
        instance_id: "pre-unnamed".to_string(),
        project_dir: "project-a".into(),
        pre_json: "project-a/pre-unnamed/pre.json".into(),
        daw_session_id: None,
        host_process_id: None,
        readiness: kirin_measure::LatchedPreReadiness::Confirmed,
    };

    assert!(binding.replace_exact(String::new(), selected).changed);
    let snapshot = binding.exact_snapshot().expect("exact unnamed pair");
    assert_eq!(snapshot.project_hash, "project-a");
    assert_eq!(snapshot.pre_instance_id, "pre-unnamed");
    assert_eq!(snapshot.generation, binding.generation());
    assert_eq!(
        binding.status_snapshot(),
        (true, Some("pre-unnamed".into()))
    );

    assert!(binding.release_if_current("", snapshot.generation));
    assert_eq!(binding.status_snapshot(), (true, None));
    assert!(binding.name_change_required(""));
    assert!(binding.replace_name(String::new()).changed);
    assert_eq!(binding.status_snapshot(), (false, None));
    assert!(!binding.name_change_required(""));
}

#[test]
fn same_name_can_move_to_an_explicit_second_instance() {
    let binding = PairBinding::new();
    binding.replace_name("mix".to_string());
    binding.set_exact_binding_for_test("pre-old");
    let selected = LatchedPre {
        name: "mix".to_string(),
        instance_id: "pre-new".to_string(),
        project_dir: Default::default(),
        pre_json: Default::default(),
        daw_session_id: None,
        host_process_id: None,
        readiness: kirin_measure::LatchedPreReadiness::Confirmed,
    };

    let transition = binding.replace_exact("mix".to_string(), selected);

    assert!(transition.changed);
    assert_eq!(
        transition.previous_pre_instance_id.as_deref(),
        Some("pre-old")
    );
    assert_eq!(
        binding
            .latched_pre
            .lock()
            .unwrap()
            .as_ref()
            .map(|pre| pre.instance_id.as_str()),
        Some("pre-new")
    );
}

#[test]
fn same_instance_id_in_another_project_is_a_different_exact_locator() {
    let binding = PairBinding::new();
    let old = LatchedPre {
        name: "mix".to_string(),
        instance_id: "pre-shared".to_string(),
        project_dir: "project-a".into(),
        pre_json: "project-a/pre-shared/pre.json".into(),
        daw_session_id: None,
        host_process_id: None,
        readiness: kirin_measure::LatchedPreReadiness::Confirmed,
    };
    assert!(binding.replace_exact("mix".to_string(), old).changed);

    let moved = LatchedPre {
        name: "mix".to_string(),
        instance_id: "pre-shared".to_string(),
        project_dir: "project-b".into(),
        pre_json: "project-b/pre-shared/pre.json".into(),
        daw_session_id: None,
        host_process_id: None,
        readiness: kirin_measure::LatchedPreReadiness::RestoredWaiting,
    };
    assert!(!binding.matches_exact("mix", &moved));
    assert!(binding.replace_exact("mix".to_string(), moved).changed);
    assert_eq!(
        binding
            .latched_pre
            .lock()
            .unwrap()
            .as_ref()
            .map(|pre| pre.project_dir.as_path()),
        Some(std::path::Path::new("project-b"))
    );
}
