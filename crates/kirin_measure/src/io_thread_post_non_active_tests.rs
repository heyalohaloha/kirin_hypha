use super::*;

#[test]
fn inactive_with_pair_keeps_last_active_as_stale() {
    let previous = DeltaResult {
        mode: DeltaMode::Active,
        lufs: Some(1.0),
        tp: Some(2.0),
        crest: Some(3.0),
        last_active: Some(DeltaSnapshot {
            lufs: Some(1.0),
            lufs_s: Some(1.5),
            psr: Some(4.0),
            tp: Some(2.0),
            n_prime_total: Some(5.0),
            crest: Some(3.0),
            sharpness: Some(6.0),
        }),
        ..Default::default()
    };

    let r = resolve_delta_for_non_active_post(SignalState::Inactive, "Drum", &previous);

    assert_eq!(r.mode, DeltaMode::Stale);
    let snap = r.last_active.expect("inactive pair must keep frozen delta");
    assert_eq!(snap.lufs, Some(1.0));
    assert_eq!(snap.lufs_s, Some(1.5));
    assert_eq!(snap.tp, Some(2.0));
    assert_eq!(snap.crest, Some(3.0));
}

#[test]
fn inactive_with_pair_snapshots_previous_core_values_if_needed() {
    let previous = DeltaResult {
        mode: DeltaMode::Active,
        lufs: Some(-0.5),
        tp: Some(0.2),
        crest: Some(1.5),
        last_active: None,
        ..Default::default()
    };

    let r = resolve_delta_for_non_active_post(SignalState::Inactive, "Music", &previous);

    assert_eq!(r.mode, DeltaMode::Stale);
    let snap = r.last_active.expect("core delta should be recoverable");
    assert_eq!(snap.lufs, Some(-0.5));
    assert_eq!(snap.tp, Some(0.2));
    assert_eq!(snap.crest, Some(1.5));
}

#[test]
fn inactive_without_pair_clears_delta() {
    let previous = DeltaResult {
        mode: DeltaMode::Active,
        lufs: Some(1.0),
        last_active: Some(DeltaSnapshot {
            lufs: Some(1.0),
            ..Default::default()
        }),
        ..Default::default()
    };

    let r = resolve_delta_for_non_active_post(SignalState::Inactive, "", &previous);

    assert_eq!(r.mode, DeltaMode::NoPre);
    assert!(r.last_active.is_none());
}

#[test]
fn bypassed_clears_even_when_pair_is_selected() {
    let previous = DeltaResult {
        mode: DeltaMode::Active,
        lufs: Some(1.0),
        last_active: Some(DeltaSnapshot {
            lufs: Some(1.0),
            ..Default::default()
        }),
        ..Default::default()
    };

    let r = resolve_delta_for_non_active_post(SignalState::Bypassed, "Drum", &previous);

    assert_eq!(r.mode, DeltaMode::NoPre);
    assert!(r.last_active.is_none());
}

fn active_delta_fixture() -> DeltaResult {
    DeltaResult {
        mode: DeltaMode::Active,
        lufs: Some(1.0),
        lufs_s: Some(1.5),
        psr: Some(4.0),
        tp: Some(2.0),
        n_prime_total: Some(5.0),
        crest: Some(3.0),
        sharpness: Some(6.0),
        last_active: Some(DeltaSnapshot {
            lufs: Some(1.0),
            lufs_s: Some(1.5),
            psr: Some(4.0),
            tp: Some(2.0),
            n_prime_total: Some(5.0),
            crest: Some(3.0),
            sharpness: Some(6.0),
        }),
    }
}

fn run_non_active_tick(
    state: SignalState,
    pair_pre_name: &str,
) -> (DeltaResult, serde_json::Value) {
    use std::sync::atomic::AtomicU64;

    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let sequence = COUNTER.fetch_add(1, Ordering::Relaxed);
    let root =
        std::env::temp_dir().join(format!("kirin_inv_d8_{}_{}", std::process::id(), sequence));
    let _ = fs::remove_dir_all(&root);
    let project_dir = root.join("project");
    let instance_dir = project_dir.join("post-instance");
    let post_file = instance_dir.join("post.json");
    let post_result = Arc::new(Mutex::new(MeasureResult {
        lufs_m: Some(-11.0),
        true_peak: Some(-0.4),
        ..Default::default()
    }));
    let delta_result = Arc::new(Mutex::new(active_delta_fixture()));
    let signal_state = Arc::new(AtomicU8::new(state as u8));
    let latched = Mutex::new(None);

    run_tick(
        &project_dir,
        &root,
        &mut PostDiscoveryState::new(),
        &instance_dir,
        &post_file,
        "post-instance",
        "owner",
        &post_result,
        &delta_result,
        &signal_state,
        pair_pre_name,
        12.5,
        "project",
        "daw",
        false,
        &latched,
    )
    .expect("INV-D8 non-active tick must publish a minimal snapshot");

    let delta = delta_result.lock().unwrap().clone();
    let json = serde_json::from_slice(&fs::read(&post_file).unwrap()).unwrap();
    let _ = fs::remove_dir_all(&root);
    (delta, json)
}

fn assert_minimal_post_json(json: &serde_json::Value, expected_state: &str, expected_pair: &str) {
    assert_eq!(json["role"], "POST");
    assert_eq!(json["signal_state"], expected_state);
    assert_eq!(json["pair_pre_name"], expected_pair);
    assert_eq!(json["pair_claimed_at"], 12.5);
    for measurement_key in [
        "pre_signal_state",
        "lufs_m",
        "lufs_s",
        "true_peak",
        "crest",
        "psr",
        "n_prime_total",
        "sharpness",
        "psb_summary",
    ] {
        assert!(
            json.get(measurement_key).is_none(),
            "minimal post.json must omit {measurement_key}"
        );
    }
}

#[test]
fn inv_d8_inactive_exact_pair_retains_frozen_delta_and_writes_minimal_json() {
    let (delta, json) = run_non_active_tick(SignalState::Inactive, "Drum");

    assert_eq!(delta.mode, DeltaMode::Stale);
    let frozen = delta
        .last_active
        .expect("Inactive exact pair must retain the last measured delta");
    assert_eq!(frozen.lufs, Some(1.0));
    assert_eq!(frozen.tp, Some(2.0));
    assert_minimal_post_json(&json, "inactive", "Drum");
}

#[test]
fn inv_d8_inactive_without_pair_clears_delta_and_writes_minimal_json() {
    let (delta, json) = run_non_active_tick(SignalState::Inactive, "");

    assert_eq!(delta.mode, DeltaMode::NoPre);
    assert!(delta.last_active.is_none());
    assert_minimal_post_json(&json, "inactive", "");
}

#[test]
fn inv_d8_bypassed_clears_delta_and_writes_minimal_json() {
    let (delta, json) = run_non_active_tick(SignalState::Bypassed, "Drum");

    assert_eq!(delta.mode, DeltaMode::NoPre);
    assert!(delta.last_active.is_none());
    assert_minimal_post_json(&json, "bypassed", "Drum");
}
