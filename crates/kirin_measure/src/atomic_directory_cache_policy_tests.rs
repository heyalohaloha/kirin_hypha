//! Included as a child of atomic_file::durable. Uses only disposable directories.
use super::directory_cache_policy::fixture::Scope;
use super::*;

#[cfg(unix)]
#[test]
fn unknown_filesystem_does_not_trust_equal_cached_identity() {
    use std::os::unix::fs::MetadataExt;
    let root = tempfile::tempdir().unwrap();
    let path = root.path().join("recreated");
    fs::create_dir(&path).unwrap();
    let metadata = fs::metadata(&path).unwrap();
    // Models an external recreation completed before create_dir_all (created=0).
    // This injects identical metadata, not evidence of real inode reuse.
    DURABLE_DIRECTORIES
        .lock()
        .unwrap()
        .insert(path.clone(), (metadata.dev(), metadata.ino()));
    let _policy = Scope::new(true, vec![(path.clone(), false)]);
    let first = test_hook::syncs_during(false, || create_dir_all(&path).unwrap());
    assert!(first.contains(&("directory", root.path().to_path_buf())));
    assert!(!DURABLE_DIRECTORIES.lock().unwrap().contains_key(&path));
    let again = test_hook::syncs_during(false, || create_dir_all(&path).unwrap());
    assert_eq!(again, vec![("directory", root.path().to_path_buf())]);
}

#[cfg(unix)]
#[test]
fn each_ancestor_policy_and_sync_failure_are_observed() {
    let root = tempfile::tempdir().unwrap();
    let parent = root.path().join("mount");
    let child = parent.join("unchanged-child");
    let _cache = Scope::new(true, vec![]);
    create_dir_all(&child).unwrap();
    assert!(test_hook::syncs_during(false, || create_dir_all(&child).unwrap()).is_empty());
    let _mixed = Scope::new(true, vec![(parent.clone(), false)]);
    let failed = test_hook::syncs_during(true, || create_dir_all(&child).unwrap());
    assert_eq!(failed, vec![("directory", root.path().to_path_buf())]);
    assert!(!DURABLE_DIRECTORIES.lock().unwrap().contains_key(&parent));
    let retried = test_hook::syncs_during(false, || create_dir_all(&child).unwrap());
    assert_eq!(
        retried,
        vec![
            ("directory", root.path().to_path_buf()),
            ("directory", parent)
        ]
    );
}

#[test]
fn policy_query_failure_is_not_cache_authority() {
    let root = tempfile::tempdir().unwrap();
    let missing = root.path().join("does-not-exist");
    assert!(!super::directory_cache_policy::permits_cache(&missing));
}

#[test]
fn actual_platform_policy_keeps_normal_disposable_save_working() {
    let root = tempfile::tempdir().unwrap();
    let directory = root.path().join("actual-policy");
    create_dir_all(&directory).unwrap();
    let cacheable = super::directory_cache_policy::permits_cache(&directory);
    let events = test_hook::syncs_during(false, || create_dir_all(&directory).unwrap());
    if !cacheable {
        assert!(events.contains(&("directory", root.path().to_path_buf())));
        assert!(!DURABLE_DIRECTORIES.lock().unwrap().contains_key(&directory));
    }
    let file = directory.join("complete.json");
    super::super::write_bytes_atomic_durable(&file, b"{}").unwrap();
    assert_eq!(fs::read(file).unwrap(), b"{}");
    println!("DIRECTORY_POLICY actual_cacheable={cacheable} inode_reuse_not_verified=true");
}
