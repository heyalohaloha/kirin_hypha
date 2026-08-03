use super::*;
use std::fs;
use std::sync::{mpsc, Arc};
use std::thread;

fn isolated_dir(label: &str) -> PathBuf {
    let dir = std::env::temp_dir().join(format!(
        "kirin_pair_observer_{label}_{}_{}",
        std::process::id(),
        Uuid::new_v4()
    ));
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn binding(instance_dir: &Path, pre: &str, claimed_at: f64) -> PairOwnershipBinding {
    PairOwnershipBinding::new(instance_dir.to_path_buf(), pre.to_string(), claimed_at)
}

#[test]
fn observer_requires_the_exact_engine_marker_path() {
    let instance_dir = isolated_dir("exact");
    let lease = PairOwnershipLease::new();
    lease.bind(&instance_dir, Some("pre-a"), 1.0).unwrap();

    assert_eq!(
        lease.observe_binding_if_stable(|| {
            ("exact", Some(binding(&instance_dir, "pre-a", 1.0)))
        }),
        Some(("exact", true))
    );
    assert_eq!(
        lease.observe_binding_if_stable(|| {
            ("other", Some(binding(&instance_dir, "pre-b", 1.0)))
        }),
        Some(("other", false))
    );
    assert_eq!(
        lease.observe_binding_if_stable(|| ("clear", None)),
        Some(("clear", false))
    );
    let _ = fs::remove_dir_all(instance_dir);
}

#[test]
fn observer_is_nonblocking_while_a_pair_commit_is_in_progress() {
    let instance_dir = isolated_dir("contended");
    let lease = Arc::new(PairOwnershipLease::new());
    lease.bind(&instance_dir, Some("pre-a"), 1.0).unwrap();
    let (started_tx, started_rx) = mpsc::channel();
    let (release_tx, release_rx) = mpsc::channel();
    let worker_lease = Arc::clone(&lease);
    let worker_dir = instance_dir.clone();
    let worker = thread::spawn(move || {
        worker_lease
            .commit_binding(Some(&worker_dir), Some("pre-b"), 2.0, || {
                started_tx.send(()).unwrap();
                release_rx.recv().unwrap();
                Some(())
            })
            .unwrap();
    });

    started_rx.recv().unwrap();
    assert_eq!(
        lease.observe_binding_if_stable(|| { ((), Some(binding(&instance_dir, "pre-a", 1.0))) }),
        None
    );
    release_tx.send(()).unwrap();
    worker.join().unwrap();
    let _ = fs::remove_dir_all(instance_dir);
}
