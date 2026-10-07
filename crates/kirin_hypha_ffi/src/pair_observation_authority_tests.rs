//! Independent absence / contention / reselection expectations for observation polling.
use super::*;

#[test]
fn no_selection_and_waiting_selection_are_distinct() {
    let binding = PairBinding::new();
    assert_eq!(
        binding.try_observation_snapshot(),
        Some(PairObservationAuthority {
            generation: 1,
            selection_intent: false,
            exact: None,
        })
    );
    binding.replace_name("mix".into());
    assert_eq!(
        binding.try_observation_snapshot(),
        Some(PairObservationAuthority {
            generation: 2,
            selection_intent: true,
            exact: None,
        })
    );
    binding.replace_name(String::new());
    assert_eq!(
        binding.try_observation_snapshot(),
        Some(PairObservationAuthority {
            generation: 3,
            selection_intent: false,
            exact: None,
        })
    );
}

#[test]
fn either_control_or_locator_contention_skips_without_blocking() {
    let binding = PairBinding::new();
    {
        let _control = binding.transition.lock().unwrap();
        assert!(binding.try_observation_snapshot().is_none());
    }
    {
        let _locator = binding.latched_pre.lock().unwrap();
        assert!(binding.try_observation_snapshot().is_none());
    }
    assert!(binding.try_observation_snapshot().is_some());
}

#[test]
fn same_named_different_instances_retire_the_old_authority() {
    let binding = PairBinding::new();
    let pre = |instance: &str| LatchedPre {
        name: "mix".into(),
        instance_id: instance.into(),
        project_dir: "project-hash".into(),
        pre_json: format!("project-hash/{instance}/pre.json").into(),
        daw_session_id: None,
        host_process_id: None,
        readiness: kirin_measure::LatchedPreReadiness::Confirmed,
    };
    binding.replace_exact("mix".into(), pre("pre-a"));
    let first = binding.try_observation_snapshot().unwrap();
    binding.replace_exact("mix".into(), pre("pre-b"));
    let second = binding.try_observation_snapshot().unwrap();
    assert_eq!(first.generation, 2);
    assert_eq!(second.generation, 3);
    assert_eq!(second.exact.unwrap().pre_instance_id, "pre-b");
    binding.replace_exact("mix".into(), pre("pre-a"));
    let returned = binding.try_observation_snapshot().unwrap();
    assert_eq!(returned.generation, 4);
    assert_ne!(returned, first);
}
