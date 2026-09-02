use super::confirmed_analysis_targets;
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
