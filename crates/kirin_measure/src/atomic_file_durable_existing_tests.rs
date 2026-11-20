// Cache-algorithm fixtures explicitly model cache-eligible directories.
use super::*;

fn directory_syncs(parent: &Path) -> Vec<(&'static str, std::path::PathBuf)> {
    if cfg!(unix) {
        vec![("directory", parent.to_path_buf())]
    } else {
        Vec::new()
    }
}

#[test]
fn durable_write_and_rename_sync_the_file_then_its_directory() {
    #[cfg(unix)]
    let _policy = super::directory_cache_policy::fixture::Scope::new(true, vec![]);
    let root = tempfile::tempdir().unwrap();
    let tmp = root.path().join("take.json.tmp");
    let target = root.path().join("take.json");
    let syncs = test_hook::syncs_during(false, || {
        write(&tmp, b"complete").unwrap();
        rename(&tmp, &target).unwrap();
    });
    let mut expected = vec![("file", tmp.clone())];
    expected.extend(directory_syncs(root.path()));
    assert_eq!(syncs, expected);
    assert_eq!(fs::read(&target).unwrap(), b"complete");
    assert!(!tmp.exists());
}

#[test]
fn failed_sync_never_stops_the_save() {
    #[cfg(unix)]
    let _policy = super::directory_cache_policy::fixture::Scope::new(true, vec![]);
    let root = tempfile::tempdir().unwrap();
    let left_over = root.path().join("pending.json.tmp");
    fs::write(&left_over, b"earlier run").unwrap();
    let target = root.path().join("pending.json");
    let fresh = root.path().join("fresh.json.tmp");
    let syncs = test_hook::syncs_during(true, || {
        sync_and_rename(&left_over, &target).unwrap();
        write(&fresh, b"fresh").unwrap();
    });
    assert_eq!(syncs.first(), Some(&("file", left_over.clone())));
    assert_eq!(fs::read(&target).unwrap(), b"earlier run");
    assert_eq!(fs::read(&fresh).unwrap(), b"fresh");
}

#[test]
fn an_existing_ancestor_made_by_another_writer_is_synced_once_with_the_new_chain() {
    #[cfg(unix)]
    let _policy = super::directory_cache_policy::fixture::Scope::new(true, vec![]);
    let root = tempfile::tempdir().unwrap();
    let project = root.path().join("project");
    // Another writer made this directory without a sync.
    fs::create_dir_all(&project).unwrap();
    let session = project.join("session").join("pre");
    let syncs = test_hook::syncs_during(false, || create_dir_all(&session).unwrap());
    let parents: Vec<_> = syncs.iter().map(|(_, path)| path.clone()).collect();
    if cfg!(unix) {
        assert!(syncs.iter().all(|(kind, _)| *kind == "directory"));
        let wanted = [root.path(), &project, &project.join("session")].map(Path::to_path_buf);
        assert!(parents.ends_with(&wanted), "{parents:?}");
    } else {
        assert!(syncs.is_empty());
    }
    let again = test_hook::syncs_during(false, || create_dir_all(&session).unwrap());
    assert!(
        again.is_empty(),
        "a chain already made durable needs no sync"
    );
}

#[test]
fn a_failed_directory_sync_still_creates_and_is_retried_later() {
    #[cfg(unix)]
    let _policy = super::directory_cache_policy::fixture::Scope::new(true, vec![]);
    let root = tempfile::tempdir().unwrap();
    let deepest = root.path().join("record_sessions/session");
    let failed = test_hook::syncs_during(true, || create_dir_all(&deepest).unwrap());
    assert!(deepest.is_dir());
    assert!(
        failed.len() <= 1,
        "the first failure ends the walk: {failed:?}"
    );
    let retried = test_hook::syncs_during(false, || create_private_dir_all(&deepest).unwrap());
    if cfg!(unix) {
        let tail = [
            root.path().to_path_buf(),
            root.path().join("record_sessions"),
        ];
        let parents: Vec<_> = retried.iter().map(|(_, path)| path.clone()).collect();
        assert!(parents.ends_with(&tail), "{parents:?}");
    }
}

#[cfg(unix)]
#[test]
fn a_directory_made_again_at_a_synced_path_is_synced_again() {
    #[cfg(unix)]
    let _policy = super::directory_cache_policy::fixture::Scope::new(true, vec![]);
    use std::os::unix::fs::MetadataExt;
    let root = tempfile::tempdir().unwrap();
    let project = root.path().join("project");
    let session = project.join("session");
    create_dir_all(&session).unwrap();
    // Another writer replaced it without a sync. The old one stays, so its inode is not reused.
    fs::rename(&session, root.path().join("old")).unwrap();
    fs::create_dir(&session).unwrap();
    let replaced = test_hook::syncs_during(false, || create_dir_all(&session).unwrap());
    assert_eq!(replaced, directory_syncs(&project));
    assert!(test_hook::syncs_during(false, || create_dir_all(&session).unwrap()).is_empty());
    // This call makes it again on a filesystem that reuses the inode: still synced.
    fs::remove_dir(&session).unwrap();
    let remade = test_hook::syncs_during(false, || {
        create_dir_all_with(&session, |dir| {
            fs::create_dir_all(dir)?;
            let reused = fs::metadata(dir)?;
            let mut known = DURABLE_DIRECTORIES.lock().unwrap();
            known.insert(dir.to_path_buf(), (reused.dev(), reused.ino()));
            Ok(())
        })
        .unwrap()
    });
    assert_eq!(remade, directory_syncs(&project));
}

#[cfg(unix)]
#[test]
fn a_symlink_pointed_at_another_directory_is_synced_again() {
    #[cfg(unix)]
    let _policy = super::directory_cache_policy::fixture::Scope::new(true, vec![]);
    let root = tempfile::tempdir().unwrap();
    let (first, second) = (root.path().join("first"), root.path().join("second"));
    fs::create_dir_all(first.join("project")).unwrap();
    // Another writer made the new target's project without a sync.
    fs::create_dir_all(second.join("project")).unwrap();
    let link = root.path().join("records");
    std::os::unix::fs::symlink(&first, &link).unwrap();
    let project = link.join("project");
    create_dir_all(&project).unwrap();
    assert!(test_hook::syncs_during(false, || create_dir_all(&project).unwrap()).is_empty());
    fs::remove_file(&link).unwrap();
    std::os::unix::fs::symlink(&second, &link).unwrap();
    let syncs = test_hook::syncs_during(false, || create_dir_all(&project).unwrap());
    let parents = [root.path().to_path_buf(), link];
    assert_eq!(syncs, parents.map(|parent| ("directory", parent)));
}

#[cfg(unix)]
#[test]
fn a_failed_sync_of_a_directory_made_again_is_retried() {
    #[cfg(unix)]
    let _policy = super::directory_cache_policy::fixture::Scope::new(true, vec![]);
    use std::os::unix::fs::MetadataExt;
    let root = tempfile::tempdir().unwrap();
    let project = root.path().join("project");
    let session = project.join("session");
    create_dir_all(&session).unwrap();
    fs::remove_dir(&session).unwrap();
    // Made again with a reused inode, and that sync fails: it is not remembered as synced.
    test_hook::syncs_during(true, || {
        create_dir_all_with(&session, |dir| {
            fs::create_dir_all(dir)?;
            let reused = fs::metadata(dir)?;
            let mut known = DURABLE_DIRECTORIES.lock().unwrap();
            known.insert(dir.to_path_buf(), (reused.dev(), reused.ino()));
            Ok(())
        })
        .unwrap()
    });
    let retried = test_hook::syncs_during(false, || create_dir_all(&session).unwrap());
    assert_eq!(retried, directory_syncs(&project));
    assert!(test_hook::syncs_during(false, || create_dir_all(&session).unwrap()).is_empty());
}

#[cfg(unix)]
#[test]
fn a_replaced_ancestor_under_an_unchanged_directory_is_synced() {
    #[cfg(unix)]
    let _policy = super::directory_cache_policy::fixture::Scope::new(true, vec![]);
    let root = tempfile::tempdir().unwrap();
    let generation = root.path().join("capture_generation");
    let archive = generation.join("archive");
    let take = archive.join("take");
    create_dir_all(&take).unwrap();
    // Another writer moves the archive aside, makes its parent again and moves it back.
    let aside = root.path().join("aside");
    fs::rename(&archive, &aside).unwrap();
    fs::rename(&generation, root.path().join("old_generation")).unwrap();
    fs::create_dir(&generation).unwrap();
    fs::rename(&aside, &archive).unwrap();
    let syncs = test_hook::syncs_during(false, || create_dir_all(&take).unwrap());
    let parents = [root.path().to_path_buf(), generation];
    assert_eq!(syncs, parents.map(|parent| ("directory", parent)));
    assert!(test_hook::syncs_during(false, || create_dir_all(&take).unwrap()).is_empty());
}

#[test]
fn the_frequent_watch_state_write_stays_unsynced() {
    #[cfg(unix)]
    let _policy = super::directory_cache_policy::fixture::Scope::new(true, vec![]);
    let root = tempfile::tempdir().unwrap();
    let syncs = test_hook::syncs_during(false, || {
        super::super::write_bytes_atomic(&root.path().join("pre.json"), b"{}").unwrap();
    });
    assert!(syncs.is_empty());
    let take = root.path().join("take.json");
    let syncs = test_hook::syncs_during(false, || {
        super::super::write_bytes_atomic_durable(&take, b"{}").unwrap();
    });
    assert_eq!(syncs[0].0, "file");
    assert_eq!(syncs[1..], directory_syncs(root.path())[..]);
}

#[test]
fn a_failed_rename_is_still_reported() {
    #[cfg(unix)]
    let _policy = super::directory_cache_policy::fixture::Scope::new(true, vec![]);
    let root = tempfile::tempdir().unwrap();
    let missing = root.path().join("missing.tmp");
    assert!(rename(&missing, &root.path().join("x.json")).is_err());
    assert!(sync_and_rename(&missing, &root.path().join("y.json")).is_err());
}

#[test]
fn real_syncs_succeed_on_the_local_filesystem() {
    #[cfg(unix)]
    let _policy = super::directory_cache_policy::fixture::Scope::new(true, vec![]);
    let root = tempfile::tempdir().unwrap();
    let file = File::create(root.path().join("a")).unwrap();
    sync_bytes(&file).unwrap();
    #[cfg(unix)]
    sync_directory(&File::open(root.path()).unwrap()).unwrap();
}
