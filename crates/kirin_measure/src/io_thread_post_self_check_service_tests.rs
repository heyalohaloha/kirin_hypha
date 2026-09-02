use super::*;
use std::sync::atomic::{AtomicUsize, Ordering as AtomicOrdering};

fn foreign_claim(root: &Path) -> crate::PairOwnershipLease {
    let owner = crate::PairOwnershipLease::new();
    let instance_dir = root.join("project").join("post-1");
    fs::create_dir_all(&instance_dir).unwrap();
    assert_eq!(
        owner
            .commit_claimed_binding_if(
                root,
                Some(&instance_dir),
                Some("pre-1"),
                "project",
                "post-1",
                1.0,
                || true,
                || Some(()),
            )
            .unwrap(),
        Some(())
    );
    owner
}

fn active_delta() -> DeltaResult {
    DeltaResult {
        lufs: Some(1.0),
        mode: DeltaMode::Active,
        last_active: Some(DeltaSnapshot {
            lufs: Some(1.0),
            ..Default::default()
        }),
        ..Default::default()
    }
}

#[test]
fn foreign_claim_releases_only_after_three_exact_confirmations() {
    let temp = tempfile::tempdir().unwrap();
    let _foreign = foreign_claim(temp.path());
    let record_sm = RecordStateMachine::new();
    let is_playing = AtomicBool::new(false);
    let signal_state = AtomicU8::new(SignalState::Inactive as u8);
    let releases = Arc::new(AtomicUsize::new(0));
    let release_count = Arc::clone(&releases);
    let release: ReleasePairBindingIfCurrentFn = Arc::new(move |name, generation| {
        assert_eq!(name, "PRE-A");
        assert_eq!(generation, 7);
        release_count.fetch_add(1, AtomicOrdering::Relaxed);
        true
    });
    let claimed_at = RwLock::new(1.0);
    let notice = RwLock::new(None);
    let delta = Mutex::new(active_delta());
    let start = Instant::now();
    let mut state = PairSelfCheckState::new(start);

    for second in 0..2 {
        state.service(
            start + Duration::from_secs(second),
            temp.path(),
            &record_sm,
            &is_playing,
            &signal_state,
            Some("pre-1"),
            "PRE-A",
            7,
            "project",
            "post-1",
            "current-engine-owner",
            1.0,
            &release,
            &claimed_at,
            &notice,
            &delta,
        );
        assert_eq!(releases.load(AtomicOrdering::Relaxed), 0);
    }
    state.service(
        start + Duration::from_secs(2),
        temp.path(),
        &record_sm,
        &is_playing,
        &signal_state,
        Some("pre-1"),
        "PRE-A",
        7,
        "project",
        "post-1",
        "current-engine-owner",
        1.0,
        &release,
        &claimed_at,
        &notice,
        &delta,
    );

    assert_eq!(releases.load(AtomicOrdering::Relaxed), 1);
    assert_eq!(*claimed_at.read().unwrap(), 0.0);
    assert_eq!(
        notice.read().unwrap().as_deref(),
        Some("PRE already in use")
    );
    let delta = delta.lock().unwrap();
    assert_eq!(delta.mode, DeltaMode::NoPre);
    assert!(delta.last_active.is_none());
    assert!(delta.lufs.is_none());
    assert!(delta.lufs_s.is_none());
    assert!(delta.psr.is_none());
    assert!(delta.tp.is_none());
    assert!(delta.n_prime_total.is_none());
    assert!(delta.crest.is_none());
    assert!(delta.sharpness.is_none());
}

#[test]
fn playback_resets_partial_conflict_confirmations() {
    let temp = tempfile::tempdir().unwrap();
    let _foreign = foreign_claim(temp.path());
    let record_sm = RecordStateMachine::new();
    let is_playing = AtomicBool::new(false);
    let signal_state = AtomicU8::new(SignalState::Inactive as u8);
    let releases = Arc::new(AtomicUsize::new(0));
    let release_count = Arc::clone(&releases);
    let release: ReleasePairBindingIfCurrentFn = Arc::new(move |_, _| {
        release_count.fetch_add(1, AtomicOrdering::Relaxed);
        true
    });
    let claimed_at = RwLock::new(1.0);
    let notice = RwLock::new(None);
    let delta = Mutex::new(active_delta());
    let start = Instant::now();
    let mut state = PairSelfCheckState::new(start);
    let service = |state: &mut PairSelfCheckState, second| {
        state.service(
            start + Duration::from_secs(second),
            temp.path(),
            &record_sm,
            &is_playing,
            &signal_state,
            Some("pre-1"),
            "PRE-A",
            7,
            "project",
            "post-1",
            "current-engine-owner",
            1.0,
            &release,
            &claimed_at,
            &notice,
            &delta,
        );
    };

    service(&mut state, 0);
    service(&mut state, 1);
    is_playing.store(true, Ordering::Relaxed);
    service(&mut state, 2);
    is_playing.store(false, Ordering::Relaxed);
    service(&mut state, 3);
    service(&mut state, 4);
    assert_eq!(releases.load(AtomicOrdering::Relaxed), 0);
    service(&mut state, 5);
    assert_eq!(releases.load(AtomicOrdering::Relaxed), 1);
}
