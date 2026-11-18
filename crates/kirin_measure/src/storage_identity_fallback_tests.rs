use super::{load_installation_id_from_paths, StoragePaths};
use std::fs;

fn fixture() -> (tempfile::TempDir, StoragePaths) {
    let root = tempfile::tempdir().unwrap();
    let paths =
        StoragePaths::with_roots(root.path().join("primary"), root.path().join("secondary"));
    fs::create_dir_all(paths.primary_path().parent().unwrap()).unwrap();
    fs::create_dir_all(paths.secondary_path().parent().unwrap()).unwrap();
    (root, paths)
}

#[test]
fn primary_id_wins_over_conflicting_backup_without_mutation() {
    let (_root, paths) = fixture();
    let primary = br#"{"installation_id":"primary-id","license":"sense"}"#;
    let backup = br#"{"installation_id":"backup-id","license":"os"}"#;
    fs::write(paths.primary_path(), primary).unwrap();
    fs::write(paths.secondary_path(), backup).unwrap();
    assert_eq!(
        load_installation_id_from_paths(&paths).as_deref(),
        Some("primary-id")
    );
    assert_eq!(fs::read(paths.primary_path()).unwrap(), primary);
    assert_eq!(fs::read(paths.secondary_path()).unwrap(), backup);
}

#[test]
fn missing_primary_uses_backup_without_recreating_primary() {
    let (_root, paths) = fixture();
    let backup = br#"{"installation_id":"backup-id"}"#;
    fs::write(paths.secondary_path(), backup).unwrap();
    assert_eq!(
        load_installation_id_from_paths(&paths).as_deref(),
        Some("backup-id")
    );
    assert!(!paths.primary_path().exists());
    assert_eq!(fs::read(paths.secondary_path()).unwrap(), backup);
}

#[test]
fn invalid_primary_variants_use_existing_backup_without_repair() {
    let (_root, paths) = fixture();
    fs::write(
        paths.secondary_path(),
        br#"{"installation_id":"backup-id"}"#,
    )
    .unwrap();
    for invalid in [
        "{",
        "{}",
        r#"{"installation_id":""}"#,
        r#"{"installation_id":42}"#,
    ] {
        fs::write(paths.primary_path(), invalid).unwrap();
        assert_eq!(
            load_installation_id_from_paths(&paths).as_deref(),
            Some("backup-id")
        );
        assert_eq!(fs::read_to_string(paths.primary_path()).unwrap(), invalid);
    }
}

#[test]
fn unreadable_primary_directory_falls_back_without_replacing_it() {
    let (_root, paths) = fixture();
    fs::create_dir(paths.primary_path()).unwrap();
    fs::write(
        paths.secondary_path(),
        br#"{"installation_id":"backup-id"}"#,
    )
    .unwrap();
    assert_eq!(
        load_installation_id_from_paths(&paths).as_deref(),
        Some("backup-id")
    );
    assert!(paths.primary_path().is_dir());
}

#[test]
fn no_valid_identity_does_not_create_or_invent_an_id() {
    let (_root, paths) = fixture();
    assert_eq!(load_installation_id_from_paths(&paths), None);
    assert!(!paths.primary_path().exists() && !paths.secondary_path().exists());
    for invalid in [
        "{",
        "{}",
        r#"{"installation_id":""}"#,
        r#"{"installation_id":false}"#,
    ] {
        fs::write(paths.secondary_path(), invalid).unwrap();
        assert_eq!(load_installation_id_from_paths(&paths), None);
        assert_eq!(fs::read_to_string(paths.secondary_path()).unwrap(), invalid);
    }
}
