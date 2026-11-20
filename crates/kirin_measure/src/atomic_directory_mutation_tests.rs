use super::*;
use crate::atomic_file::durable::test_hook::syncs_during;
fn directory_events(events: &[(&str, std::path::PathBuf)], expected: &[&Path]) {
    #[cfg(unix)]
    assert_eq!(
        events,
        expected
            .iter()
            .map(|p| ("directory", p.to_path_buf()))
            .collect::<Vec<_>>()
    );
    #[cfg(not(unix))]
    {
        let _ = expected;
        assert!(events.is_empty());
    }
}
#[test]
fn claim_success_and_collision_sync_only_changed_entries() {
    for fail in [false, true] {
        let root = tempfile::tempdir().unwrap();
        let destination = root.path().join("owner");
        let temp = crate::atomic_claim::build_claim_temp(root.path(), "one", b"first").unwrap();
        let events = syncs_during(fail, || {
            assert_eq!(
                crate::atomic_claim::link_claim(&temp, &destination).unwrap(),
                crate::atomic_claim::ClaimOutcome::Created
            );
        });
        directory_events(&events, &[root.path(), root.path()]);
        assert_eq!(fs::read(&destination).unwrap(), b"first");
        assert!(!temp.exists());
        let other = crate::atomic_claim::build_claim_temp(root.path(), "two", b"second").unwrap();
        let events = syncs_during(fail, || {
            assert_eq!(
                crate::atomic_claim::link_claim(&other, &destination).unwrap(),
                crate::atomic_claim::ClaimOutcome::AlreadyClaimed
            );
        });
        directory_events(&events, &[root.path()]);
        assert_eq!(fs::read(destination).unwrap(), b"first");
        assert!(!other.exists());
    }
}
#[test]
fn failed_claim_cleans_temp_without_creating_destination() {
    let root = tempfile::tempdir().unwrap();
    let temp = crate::atomic_claim::build_claim_temp(root.path(), "one", b"complete").unwrap();
    let destination = root.path().join("missing").join("owner");
    let events = syncs_during(false, || {
        assert!(crate::atomic_claim::link_claim(&temp, &destination).is_err())
    });
    directory_events(&events, &[root.path()]);
    assert!(!temp.exists() && !destination.exists());
}
#[test]
fn delete_and_cross_directory_move_keep_syscall_semantics() {
    for fail in [false, true] {
        let root = tempfile::tempdir().unwrap();
        let left = root.path().join("left");
        let right = root.path().join("right");
        fs::create_dir(&left).unwrap();
        fs::create_dir(&right).unwrap();
        let from = left.join("bad.json");
        let to = right.join("bad.json");
        fs::write(&from, b"preserved evidence").unwrap();
        let events = syncs_during(fail, || rename(&from, &to).unwrap());
        directory_events(&events, &[&right, &left]);
        assert_eq!(fs::read(&to).unwrap(), b"preserved evidence");
        let events = syncs_during(fail, || remove_file(&to).unwrap());
        directory_events(&events, &[&right]);
        let events = syncs_during(fail, || {
            assert_eq!(
                remove_file(&to).unwrap_err().kind(),
                io::ErrorKind::NotFound
            )
        });
        directory_events(&events, &[]);
        let events = syncs_during(fail, || assert!(remove_file(&left).is_err()));
        directory_events(&events, &[]);
        assert!(left.is_dir());
    }
}
#[test]
fn actual_quarantine_syncs_source_and_destination_parents() {
    use crate::capture_generation_lifecycle::*;
    let root = tempfile::tempdir().unwrap();
    let id = uuid::Uuid::new_v4().to_string();
    let source = generation_terminal_path(root.path(), &id);
    fs::create_dir_all(source.parent().unwrap()).unwrap();
    fs::write(&source, b"{malformed").unwrap();
    let events = syncs_during(false, || {
        assert!(read_generation_terminal_recovering(root.path(), &id, 1)
            .unwrap()
            .is_none());
    });
    let parent = source.parent().unwrap();
    let quarantine = parent.join("quarantine");
    #[cfg(unix)]
    assert!(events.ends_with(&[
        ("directory", quarantine.clone()),
        ("directory", parent.to_path_buf())
    ]));
    #[cfg(not(unix))]
    assert!(events.is_empty());
    assert!(!source.exists());
    let files = fs::read_dir(quarantine)
        .unwrap()
        .collect::<Result<Vec<_>, _>>()
        .unwrap();
    assert_eq!(files.len(), 1);
    assert_eq!(fs::read(files[0].path()).unwrap(), b"{malformed");
}
