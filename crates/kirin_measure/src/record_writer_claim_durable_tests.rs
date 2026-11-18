use super::*;
use crate::atomic_file::durable::test_hook::syncs_during;
#[test]
fn owner_release_syncs_both_entries_wrong_owner_changes_nothing() {
    let root = tempfile::tempdir().unwrap();
    let mut claim = claim_writer(root.path(), "project", "session", Role::Post, "post").unwrap();
    claim.mark_ready().unwrap();
    let wrong = syncs_during(false, || {
        assert!(release_if_current_owner(&claim.path, "not-owner").is_err());
    });
    assert!(wrong.is_empty());
    let events = syncs_during(false, || {
        release_if_current_owner(&claim.path, &claim.claim.owner_id).unwrap()
    });
    #[cfg(unix)]
    assert_eq!(
        events
            .iter()
            .filter(|(kind, _)| *kind == "directory")
            .count(),
        2
    );
    #[cfg(not(unix))]
    assert!(events.is_empty());
    assert!(!claim.path.exists());
    assert!(!claim_state_path(&claim.path).exists());
}
