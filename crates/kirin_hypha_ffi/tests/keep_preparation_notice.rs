//! Public Keep controls report missing pairing without waiting for any producer or timer.
//! This binary has one test, so its disposable storage environment is process-isolated.
use kirin_hypha_ffi::{channel_abi::ChannelLayout, KirinHyphaEngine, KIRIN_KEEP_PHASE_IDLE};
use kirin_measure::{PlatformPaths, TEST_STORAGE_ROOT_ENV};
use std::ffi::OsString;

struct StorageRoot(Option<OsString>);

impl Drop for StorageRoot {
    fn drop(&mut self) {
        match &self.0 {
            Some(value) => std::env::set_var(TEST_STORAGE_ROOT_ENV, value),
            None => std::env::remove_var(TEST_STORAGE_ROOT_ENV),
        }
    }
}

#[test]
fn missing_pairing_has_actionable_one_shot_notices_in_keep_and_all_keep() {
    let temporary = tempfile::tempdir().unwrap();
    let _storage = StorageRoot(std::env::var_os(TEST_STORAGE_ROOT_ENV));
    std::env::set_var(TEST_STORAGE_ROOT_ENV, temporary.path());
    let paths = PlatformPaths::default_current().unwrap();
    std::fs::create_dir_all(&paths.storage.kirin_os_root).unwrap();
    std::fs::write(paths.storage.primary_path(), br#"{"schema_version":"1.0","installation_id":"keep-notice-fixture","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#).unwrap();
    let post = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    post.set_identity(
        "notice-post".into(),
        "notice-project".into(),
        "".into(),
        "".into(),
    );
    post.enable_post_writes();
    assert!(!post.keep());
    assert_eq!(
        post.drain_keep_action_notice().as_deref(),
        Some("Choose a PRE before Keep.")
    );
    assert_eq!(post.drain_keep_action_notice(), None);

    post.set_pair_target("missing".into());
    assert!(!post.keep());
    assert_eq!(
        post.drain_keep_action_notice().as_deref(),
        Some("PRE unavailable. Choose PRE again.")
    );
    assert_eq!(post.drain_keep_action_notice(), None);

    assert!(!post.keep_all());
    assert_eq!(
        post.drain_keep_action_notice().as_deref(),
        Some("No pairs ready. Select PRE, then retry.")
    );
    assert_eq!(post.drain_keep_action_notice(), None);
    assert_eq!(post.keep_phase(), KIRIN_KEEP_PHASE_IDLE);
    assert!(!post.is_recording());
    assert_eq!(post.record_error_message(), None);
    // Stop clears the action edge and leaves no preparation authority to revive on retry.
    assert!(!post.keep());
    post.stop();
    assert_eq!(post.drain_keep_action_notice(), None);
    assert_eq!(post.keep_phase(), KIRIN_KEEP_PHASE_IDLE);

    // Let the shell keep its existing license-required explanation for either operation.
    let mut identity: serde_json::Value =
        serde_json::from_slice(&std::fs::read(paths.storage.primary_path()).unwrap()).unwrap();
    identity["license"] = serde_json::json!("sense");
    std::fs::write(
        paths.storage.primary_path(),
        serde_json::to_vec(&identity).unwrap(),
    )
    .unwrap();
    post.set_license(1);
    assert!(!post.keep());
    assert_eq!(post.drain_keep_action_notice(), None);
    assert!(!post.keep_all());
    assert_eq!(post.drain_keep_action_notice(), None);
}
