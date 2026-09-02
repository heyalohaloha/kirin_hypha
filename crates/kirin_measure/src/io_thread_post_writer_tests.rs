use super::paired_pre_target_snapshot;
use std::sync::{Arc, Mutex};

#[test]
fn writer_pair_target_snapshot_keeps_the_exact_instance_identity() {
    let target = Arc::new(Mutex::new(Some("pre-instance-exact".to_string())));
    assert_eq!(
        paired_pre_target_snapshot(&target).as_deref(),
        Some("pre-instance-exact")
    );
}

#[test]
fn absent_writer_pair_target_stays_absent() {
    let target = Arc::new(Mutex::new(None));
    assert_eq!(paired_pre_target_snapshot(&target), None);
}

#[test]
fn poisoned_writer_pair_target_fails_closed_without_inventing_an_identity() {
    let target = Arc::new(Mutex::new(Some("pre-before-poison".to_string())));
    let poison_target = Arc::clone(&target);
    let _ = std::thread::spawn(move || {
        let _guard = poison_target.lock().unwrap();
        panic!("poison paired PRE target");
    })
    .join();
    assert_eq!(paired_pre_target_snapshot(&target), None);
}
