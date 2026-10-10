//! Unit-test admission files stay private even when a test binary is run outside Cargo/CTest.
//! The root is thread-local so parallel tests never mutate process-wide storage environment.
use super::StoragePaths;

thread_local! {
    static SANDBOX: tempfile::TempDir = tempfile::tempdir().expect("admission test sandbox");
}

pub(super) fn paths() -> StoragePaths {
    SANDBOX.with(|sandbox| StoragePaths::with_root(sandbox.path().join("Kirin OS")))
}

#[test]
fn default_admission_paths_are_private_stable_and_separate_between_threads() {
    let first = paths();
    assert_eq!(first, paths());
    assert_ne!(first, StoragePaths::default_platform().unwrap());
    assert!(first.plugin_data_dir().starts_with(std::env::temp_dir()));
    let other = std::thread::spawn(|| {
        let storage = paths();
        std::fs::create_dir_all(storage.plugin_data_dir()).unwrap();
        assert!(storage.plugin_data_dir().exists());
        storage
    })
    .join()
    .unwrap();
    assert_ne!(first, other);
    assert!(!other.plugin_data_dir().exists()); // The owning test thread removes its sandbox.
}

#[test]
fn admission_and_blind_barrier_write_only_into_the_test_sandbox() {
    use super::{KirinHyphaEngine, PluginDataRole, ADMISSION_TEST};
    let _serial = ADMISSION_TEST.lock().unwrap();
    let storage = paths();
    for prefix in [
        "ffi-reference",
        "ffi-owner",
        "ffi-audition",
        "ffi-blind-exclusion",
    ] {
        let engine = KirinHyphaEngine::new(
            48_000,
            kirin_measure::channel_layout::ChannelLayout::stereo(),
        );
        *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        let project = format!("{prefix}-{}", uuid::Uuid::new_v4());
        {
            let mut identity = engine.identity.lock().unwrap();
            identity.project_hash = project.clone();
            identity.instance_id = uuid::Uuid::new_v4().to_string();
        }
        assert!(engine.set_reference_audition_active(true));
        assert!(engine.set_version_blind_capture_exclusion(true));
        assert!(storage.plugin_data_dir().join(&project).is_dir());
        assert!(engine.set_version_blind_capture_exclusion(false));
        assert!(engine.set_reference_audition_active(false));
        let epoch = engine.begin_local_blind().unwrap();
        assert!(engine.end_local_blind(epoch));
    }
}
