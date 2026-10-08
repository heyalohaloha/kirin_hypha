use super::{
    active_analysis_targets, attack_pair_origin, bound_analysis_targets,
    confirmed_analysis_targets, PostAnalysisBinding,
};
use crate::pairing_scope::{LatchedPre, LatchedPreReadiness};
use std::path::PathBuf;
use std::sync::{Arc, Mutex};

fn latch(readiness: LatchedPreReadiness, pre_json: PathBuf) -> Arc<Mutex<Option<LatchedPre>>> {
    Arc::new(Mutex::new(Some(LatchedPre {
        name: "Drum".to_string(),
        instance_id: "pre-exact".to_string(),
        project_dir: PathBuf::from("/tmp/kirin/project"),
        pre_json,
        daw_session_id: Some("daw-exact".to_string()),
        host_process_id: Some(42),
        readiness,
    })))
}

#[test]
fn confirmed_latch_builds_spectrum_and_history_targets_from_one_exact_pre() {
    let pre_json = PathBuf::from("/tmp/kirin/project/pre-exact/pre.json");
    let (spectrum, history) =
        confirmed_analysis_targets(&latch(LatchedPreReadiness::Confirmed, pre_json.clone()));

    let spectrum = spectrum.expect("confirmed PRE must feed Spectrum");
    let history = history.expect("confirmed PRE must feed meter history");
    assert_eq!(spectrum.pre_instance_id, "pre-exact");
    assert_eq!(history.pre_instance_id, "pre-exact");
    assert_eq!(spectrum.instance_dir, history.instance_dir);
    assert_eq!(history.pre_json, pre_json);
}

#[test]
fn reference_b_preserves_original_time_target_and_suppresses_spectrum_only() {
    let pre_json = PathBuf::from("/tmp/kirin/project/pre-exact/pre.json");
    let exact = latch(LatchedPreReadiness::Confirmed, pre_json);
    assert!(active_analysis_targets(&exact, false).0.is_some());
    let targets = active_analysis_targets(&exact, true);
    assert!(targets.0.is_none());
    assert_eq!(targets.1, active_analysis_targets(&exact, false).1);
}

#[test]
fn restored_waiting_latch_feeds_neither_optional_analysis_endpoint() {
    let targets = confirmed_analysis_targets(&latch(
        LatchedPreReadiness::RestoredWaiting,
        PathBuf::from("/tmp/kirin/project/pre-exact/pre.json"),
    ));
    assert_eq!(targets, (None, None));
}

#[test]
fn non_pre_json_locator_feeds_neither_optional_analysis_endpoint() {
    let targets = confirmed_analysis_targets(&latch(
        LatchedPreReadiness::Confirmed,
        PathBuf::from("/tmp/kirin/project/pre-exact/not-pre.json"),
    ));
    assert_eq!(targets, (None, None));
}

#[test]
fn absent_or_poisoned_latch_feeds_neither_optional_analysis_endpoint() {
    let absent = Arc::new(Mutex::new(None));
    assert_eq!(confirmed_analysis_targets(&absent), (None, None));

    let poisoned = latch(
        LatchedPreReadiness::Confirmed,
        PathBuf::from("/tmp/kirin/project/pre-exact/pre.json"),
    );
    let poison_target = Arc::clone(&poisoned);
    let _ = std::thread::spawn(move || {
        let _guard = poison_target.lock().unwrap();
        panic!("poison exact latch");
    })
    .join();
    assert_eq!(confirmed_analysis_targets(&poisoned), (None, None));
}

#[test]
fn analysis_requires_the_same_exact_pre_and_post_binding() {
    let exact = latch(
        LatchedPreReadiness::Confirmed,
        PathBuf::from("/tmp/kirin/project/pre-exact/pre.json"),
    );
    let bound = |owner, generation, claimed_at| {
        bound_analysis_targets(
            &exact,
            PostAnalysisBinding {
                project_hash: "fixture-project",
                post_instance_id: "post-exact",
                pair_pre_name: "PRE-A",
                paired_pre_instance_id: Some("pre-exact"),
                pair_owner_id: owner,
                generation,
                claimed_at,
            },
            false,
        )
    };
    let first = bound("pair-owner-a", 7, 1.0);
    let second = bound("pair-owner-a", 8, 1.0);
    assert!(first.0.is_some());
    assert!(first.1.is_some());
    assert_ne!(first.1, second.1);
    let missing_owner = bound("", 7, 1.0);
    assert!(missing_owner.0.is_some());
    assert!(missing_owner.1.is_none());
    let missing_generation = bound("pair-owner-a", 0, 1.0);
    assert!(missing_generation.0.is_some());
    assert!(missing_generation.1.is_none());
    assert_eq!(
        bound_analysis_targets(
            &exact,
            PostAnalysisBinding {
                project_hash: "fixture-project",
                post_instance_id: "post-exact",
                pair_pre_name: "PRE-A",
                paired_pre_instance_id: Some("other-pre"),
                pair_owner_id: "pair-owner-a",
                generation: 7,
                claimed_at: 1.0,
            },
            false,
        ),
        (None, None)
    );
    let audition_targets = bound_analysis_targets(
        &exact,
        PostAnalysisBinding {
            project_hash: "fixture-project",
            post_instance_id: "post-exact",
            pair_pre_name: "PRE-A",
            paired_pre_instance_id: Some("pre-exact"),
            pair_owner_id: "pair-owner-a",
            generation: 7,
            claimed_at: 1.0,
        },
        true,
    );
    assert!(audition_targets.0.is_none());
    assert_eq!(audition_targets.1, first.1);
}

#[test]
fn attack_origin_uses_confirmed_pre_shelf_and_rejects_invalid_binding() {
    let exact = latch(
        LatchedPreReadiness::Confirmed,
        PathBuf::from("/tmp/kirin/pre-role-project/pre-exact/pre.json"),
    );
    let binding = PostAnalysisBinding {
        project_hash: "post-role-project",
        post_instance_id: "post-exact",
        pair_pre_name: "PRE-A",
        paired_pre_instance_id: Some("pre-exact"),
        pair_owner_id: "pair-owner-a",
        generation: 7,
        claimed_at: 1.0,
    };
    let (spectrum, history) = bound_analysis_targets(&exact, binding, false);
    let target = spectrum.unwrap();
    assert!(history.is_some());
    let origin = attack_pair_origin(&target, binding).unwrap();
    assert_eq!(origin.project_hash, "pre-role-project");
    assert_eq!(origin.pre_instance_id, "pre-exact");
    assert_eq!(origin.post_instance_id, "post-exact");
    for changed in [
        PostAnalysisBinding {
            generation: 0,
            ..binding
        },
        PostAnalysisBinding {
            claimed_at: 0.0,
            ..binding
        },
        PostAnalysisBinding {
            claimed_at: f64::NAN,
            ..binding
        },
        PostAnalysisBinding {
            paired_pre_instance_id: Some("foreign-pre"),
            ..binding
        },
        PostAnalysisBinding {
            pair_owner_id: "",
            ..binding
        },
    ] {
        assert!(attack_pair_origin(&target, changed).is_none());
    }
    let mut wrong_directory = target;
    wrong_directory.instance_dir = "/tmp/kirin/pre-role-project/foreign-pre".into();
    assert!(attack_pair_origin(&wrong_directory, binding).is_none());
}
