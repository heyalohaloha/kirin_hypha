use super::*;
use crate::watch_snapshot_lease::WatchSnapshotLease;
use std::fs::{File, OpenOptions};
use std::io::Write;
use tempfile::TempDir;

fn scope(root: &Path) -> Scope {
    Scope {
        root: root.to_owned(),
        host: crate::current_host_process_id(),
        lifetime: 1,
        project: "project".into(),
        session: "session".into(),
        post: "post".into(),
    }
}
fn pre(root: &Path, id: &str, name: &str) -> WatchSnapshotLease {
    let path = root.join("project").join(id);
    let mut lease = WatchSnapshotLease::new();
    lease.bind(&path).unwrap();
    fs::write(
        path.join("pre.json"),
        serde_json::to_vec(&serde_json::json!({
            "instance_id":id,"name":name,"host_process_id":crate::current_host_process_id(),
            "watch_owner_id":lease.owner_id(),"signal_state":"bypassed"
        }))
        .unwrap(),
    )
    .unwrap();
    lease
}
fn limits(snapshot: &Snapshot) {
    let s = snapshot.stats;
    assert!(s.entries <= 512 && s.json_files <= 64 && s.bytes <= 1048576);
    assert!(s.max_batch_entries <= 32 && s.max_batch_operations <= 8 && s.max_batch_bytes <= 65536);
    println!(
        "preview entries={} json={} bytes={} ops={} elapsed_us={} stop={:?}",
        s.entries, s.json_files, s.bytes, s.operations, s.elapsed_us, snapshot.stopped
    );
}
#[test]
fn complete_single_and_many_never_guess_from_truncation() {
    for count in [1, 32, 33] {
        let temp = TempDir::new().unwrap();
        let leases: Vec<_> = (0..count)
            .map(|i| pre(temp.path(), &format!("pre-{i}"), "Same name"))
            .collect();
        let result = scan(scope(temp.path()), &|| false);
        limits(&result);
        if count == 1 {
            assert_eq!(result.single_available().unwrap().id, "pre-0");
        } else {
            assert!(result.single_available().is_none());
        }
        if count <= 32 {
            assert_eq!(result.stopped, None);
            assert_eq!(result.candidates.len(), count);
        } else {
            assert!(result.stopped.is_some());
        }
        drop(leases);
    }
}
#[test]
fn dead_owner_malformed_file_and_cancel_do_not_create_a_single_candidate() {
    let temp = TempDir::new().unwrap();
    let first = pre(temp.path(), "pre-a", "");
    let second = pre(temp.path(), "pre-b", "");
    drop(second);
    assert_eq!(
        scan(scope(temp.path()), &|| false)
            .single_available()
            .unwrap()
            .id,
        "pre-a"
    );
    fs::write(temp.path().join("project/pre-b/pre.json"), b"broken").unwrap();
    assert!(scan(scope(temp.path()), &|| false)
        .single_available()
        .is_none());
    let cancelled = scan(scope(temp.path()), &|| true);
    assert_eq!(cancelled.stopped, Some(Stop::Cancelled));
    assert_eq!(cancelled.stats.operations, 0);
    drop(first);
}
#[test]
fn canonical_claim_and_contention_are_observed_in_one_scan() {
    let temp = TempDir::new().unwrap();
    let _pre = pre(temp.path(), "pre", "PRE");
    let host = crate::current_host_process_id();
    let owner = uuid::Uuid::new_v4().to_string();
    let claim = crate::pair_claim_index::PairClaim {
        schema: crate::pair_claim_index::PAIR_CLAIM_SCHEMA.into(),
        pre_instance_id: "pre".into(),
        project_hash: "project".into(),
        post_instance_id: "other-post".into(),
        post_watch_owner_id: String::new(),
        pair_owner_id: owner.clone(),
        host_process_id: host,
        pair_claimed_at_bits: 1.0f64.to_bits(),
    };
    let proof = crate::pair_ownership_marker::engine_binding_proof_path(
        temp.path(),
        &temp.path().join("project/other-post"),
        &owner,
        host,
        "pre",
        claim.pair_claimed_at_bits,
    )
    .unwrap();
    fs::create_dir_all(proof.parent().unwrap()).unwrap();
    let proof_file = OpenOptions::new()
        .read(true)
        .write(true)
        .create_new(true)
        .open(proof)
        .unwrap();
    proof_file.lock().unwrap();
    let target = temp.path().join("pair_target").join(host.to_string());
    fs::create_dir_all(&target).unwrap();
    let lock = OpenOptions::new()
        .read(true)
        .write(true)
        .create_new(true)
        .open(target.join(".pre.lock"))
        .unwrap();
    fs::write(target.join("pre.json"), serde_json::to_vec(&claim).unwrap()).unwrap();
    let claimed = scan(scope(temp.path()), &|| false);
    limits(&claimed);
    assert_eq!(claimed.stopped, None);
    assert!(claimed.single_available().is_none());
    assert_eq!(claimed.candidates[0].owner.as_deref(), Some("other-post"));
    lock.lock().unwrap();
    let contended = scan(scope(temp.path()), &|| false);
    assert_eq!(contended.stopped, Some(Stop::Uncertain));
    lock.unlock().unwrap();
    drop(proof_file);
    assert_eq!(
        scan(scope(temp.path()), &|| false)
            .single_available()
            .unwrap()
            .id,
        "pre"
    );
}
#[test]
fn every_visited_entry_and_json_byte_is_bounded() {
    for count in [512, 513] {
        let temp = TempDir::new().unwrap();
        for i in 0..count {
            File::create(temp.path().join(format!("unused-{i}"))).unwrap();
        }
        let result = scan(scope(temp.path()), &|| false);
        limits(&result);
        assert!(result.stopped.is_some());
        assert!(result.single_available().is_none());
    }
    let temp = TempDir::new().unwrap();
    let file = temp.path().join("value.json");
    for size in [65536, 65537] {
        File::create(&file)
            .unwrap()
            .write_all(&vec![b' '; size])
            .unwrap();
        let mut budget = Budget::new(&|| false);
        let result = budget.json(&file);
        assert_eq!(result.is_ok(), size == 65536);
        assert!(budget.stats.bytes <= 65536);
    }
    fs::write(&file, vec![b' '; 16384]).unwrap();
    let mut budget = Budget::new(&|| false);
    for _ in 0..64 {
        assert!(budget.json(&file).is_ok());
    }
    assert_eq!(budget.stats.bytes, 1048576);
    assert_eq!(budget.json(&file), Err(Stop::Limit));
    assert_eq!(budget.stats.json_files, 64);
}
#[test]
fn delayed_call_is_not_a_hard_deadline_but_prevents_further_io() {
    let mut budget = Budget::new(&|| false);
    assert_eq!(
        budget.operation(|| {
            std::thread::sleep(std::time::Duration::from_millis(35));
            Ok(())
        }),
        Err(Stop::Limit)
    );
    let before = budget.stats.operations;
    assert_eq!(
        budget.operation(|| panic!("must not begin another filesystem operation")),
        Err::<(), _>(Stop::Limit)
    );
    assert_eq!(budget.stats.operations, before);
}
