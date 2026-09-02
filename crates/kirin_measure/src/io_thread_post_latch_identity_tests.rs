use super::*;
use std::sync::atomic::AtomicU64;

// ── B-108: pairing latch（compute_latched_display）─────────────────────────

/// kirin_root（`{puid}` の親）を一意な temp に作る。
fn isolated_dir(tag: &str) -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_{tag}_{pid}_{n}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

/// kirin_root に `{puid}/{iid}/pre.json`（signal_state/t 可変）を書き、pre.json パスを返す。
fn write_pre_latch(
    kirin_root: &Path,
    puid: &str,
    iid: &str,
    name: &str,
    signal_state: &str,
    t: &str,
) -> PathBuf {
    let dir = kirin_root.join(puid).join(iid);
    fs::create_dir_all(&dir).unwrap();
    let host_process_id = crate::post_candidates::current_host_process_id();
    let json = format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{iid}","name":"{name}","host_process_id":{host_process_id},"signal_state":"{signal_state}","t":"{t}","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}}"#
    );
    let p = dir.join("pre.json");
    fs::write(&p, json).unwrap();
    p
}

fn attach_pre_owner(pre_json: &Path) -> crate::watch_snapshot_lease::WatchSnapshotLease {
    let instance_dir = pre_json.parent().unwrap();
    let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    lease.bind(instance_dir).unwrap();
    let mut json: serde_json::Value = serde_json::from_slice(&fs::read(pre_json).unwrap()).unwrap();
    json["watch_owner_id"] = serde_json::json!(lease.owner_id());
    fs::write(pre_json, json.to_string()).unwrap();
    lease
}
fn latch_now() -> String {
    chrono::Utc::now().format("%Y-%m-%dT%H:%M:%SZ").to_string()
}
fn latch_old(secs: i64) -> String {
    (chrono::Utc::now() - chrono::Duration::seconds(secs))
        .format("%Y-%m-%dT%H:%M:%SZ")
        .to_string()
}
fn latch_post() -> MeasureResult {
    MeasureResult {
        lufs_m: Some(-10.0),
        true_peak: Some(-1.0),
        crest: Some(12.0),
        ..Default::default()
    }
}

/// T2: ラッチ後に同名 2 台目 PRE が現れてもラッチ先 instance_id 不変（再選定しない）。
#[test]
fn latch_invariant_to_second_same_name() {
    let root = isolated_dir("latch_2nd");
    write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
    let latched = std::sync::Mutex::new(None);
    let _ = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert_eq!(
        latched.lock().unwrap().as_ref().unwrap().instance_id,
        "iid-A"
    );
    // 同名 2 台目出現（曖昧 → 素の Arm 選定なら None）。
    write_pre_latch(&root, "puid-2", "iid-B", "snare", "active", &latch_now());
    let _ = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert_eq!(
        latched.lock().unwrap().as_ref().unwrap().instance_id,
        "iid-A",
        "同名2台目でもラッチ先不変（再選定しない）"
    );
}

/// T3: pair 名変更/クリアで即アンラッチ（Watch 中）。
#[test]
fn latch_name_change_unlatches() {
    let root = isolated_dir("latch_rename");
    write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
    let latched = std::sync::Mutex::new(None);
    let _ = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert!(latched.lock().unwrap().is_some());
    // 名前変更（"kick" 不在）→ アンラッチ + NoPre。
    let (d, _, _) =
        compute_latched_display(&root, "kick", &latch_post(), Some("kick"), false, &latched)
            .unwrap();
    assert!(latched.lock().unwrap().is_none(), "名前変更で即アンラッチ");
    assert_eq!(d.mode, DeltaMode::NoPre);
    // クリア（空）→ アンラッチ + NoPre。
    let _ = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert!(latched.lock().unwrap().is_some());
    let (d2, _, _) =
        compute_latched_display(&root, "", &latch_post(), None, false, &latched).unwrap();
    assert!(latched.lock().unwrap().is_none(), "クリアで即アンラッチ");
    assert_eq!(d2.mode, DeltaMode::NoPre);
}

#[test]
fn unnamed_exact_latch_remains_valid_until_selection_layer_clears_it() {
    let root = isolated_dir("latch_unnamed_exact");
    let pre_json = write_pre_latch(&root, "puid-1", "iid-A", "", "active", &latch_now());
    let latched = std::sync::Mutex::new(Some(LatchedPre {
        name: String::new(),
        instance_id: "iid-A".to_string(),
        project_dir: root.join("puid-1"),
        pre_json,
        daw_session_id: None,
        host_process_id: Some(crate::post_candidates::current_host_process_id()),
        readiness: crate::LatchedPreReadiness::Confirmed,
    }));

    let (delta, _, _) =
        compute_latched_display(&root, "", &latch_post(), None, false, &latched).unwrap();
    assert_eq!(delta.mode, DeltaMode::Active);
    assert_eq!(
        latched
            .lock()
            .unwrap()
            .as_ref()
            .map(|pre| pre.instance_id.as_str()),
        Some("iid-A")
    );
}

#[test]
fn restored_exact_latch_waits_for_pre_loaded_later_without_name_rescan() {
    let root = isolated_dir("restored_exact_wait");
    let pre_json = write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
    let old_owner = attach_pre_owner(&pre_json);
    drop(old_owner); // normal previous-DAW residue: JSON remains, kernel lease is released

    let competing = write_pre_latch(&root, "puid-2", "iid-B", "snare", "active", &latch_now());
    let _competing_owner = attach_pre_owner(&competing);
    let latched = std::sync::Mutex::new(Some(LatchedPre {
        name: "snare".to_string(),
        instance_id: "iid-A".to_string(),
        project_dir: root.join("puid-1"),
        pre_json: pre_json.clone(),
        daw_session_id: Some("daw-1".to_string()),
        host_process_id: Some(crate::post_candidates::current_host_process_id()),
        readiness: crate::LatchedPreReadiness::RestoredWaiting,
    }));

    let (waiting, _, _) = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert_eq!(waiting.mode, DeltaMode::Stale);
    assert_eq!(
        latched
            .lock()
            .unwrap()
            .as_ref()
            .map(|pre| pre.instance_id.as_str()),
        Some("iid-A"),
        "released previous-process residue must not release the saved exact binding or select iid-B by name"
    );

    write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
    let _new_owner = attach_pre_owner(&pre_json);
    let (active, _, _) = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert_eq!(active.mode, DeltaMode::Active);
    assert_eq!(
        latched
            .lock()
            .unwrap()
            .as_ref()
            .map(|pre| pre.instance_id.as_str()),
        Some("iid-A"),
        "late PRE publication must resume the saved exact pair, not select by name"
    );
    assert_eq!(
        latched.lock().unwrap().as_ref().map(|pre| pre.readiness),
        Some(crate::LatchedPreReadiness::Confirmed),
        "the fixed restore latch becomes a normal live latch only after current owner proof"
    );
}

/// PRE Watch lease終了は計測をstaleにするだけで、exact latchを別instanceへ移さない。
#[test]
fn released_watch_owner_keeps_exact_pair_and_rejects_name_retargeting() {
    let root = isolated_dir("latch_recreate");
    let old = write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
    let lease = attach_pre_owner(&old);
    let latched = std::sync::Mutex::new(None);
    let _ = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert!(latched.lock().unwrap().is_some());

    drop(lease);
    // Watch owner release and metric freshness are separate signals. The just-written snapshot
    // remains usable until its timestamp TTL expires; age that exact snapshot to exercise the
    // unavailable-measurement path without changing the pair identity.
    write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_old(20));
    let (d, _, _) = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert!(
        latched.lock().unwrap().is_some(),
        "released Watch owner must not detach the exact pair"
    );
    assert_eq!(d.mode, DeltaMode::Stale);

    write_pre_latch(&root, "puid-2", "iid-B", "snare", "active", &latch_now());
    let (d2, _, _) = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert_eq!(
        d2.mode,
        DeltaMode::Stale,
        "same-name PRE must not steal the pair"
    );
    assert_eq!(
        latched
            .lock()
            .unwrap()
            .as_ref()
            .map(|pre| pre.instance_id.as_str()),
        Some("iid-A")
    );
}

/// T5: ラッチ先 pre.json が stale > NO_PRE_SECS(10s) でもアンラッチしない。
#[test]
fn latch_stale_beyond_ttl_keeps_pair_latched() {
    let root = isolated_dir("latch_stale");
    write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
    let latched = std::sync::Mutex::new(None);
    let _ = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert!(latched.lock().unwrap().is_some());
    // t を 20s 古く（> NO_PRE_SECS=10）→ muted Δ/---。ラッチは外さない。
    write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_old(20));
    let (d, _, _) = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert!(
        latched.lock().unwrap().is_some(),
        "stale>TTL でも明示 pair は維持"
    );
    assert_eq!(d.mode, DeltaMode::Stale);
}

/// T5b: ラッチ先 PRE の name field が一時的に違っても、instance ラッチを優先する。
#[test]
fn latch_pre_name_mismatch_keeps_instance_authority() {
    let root = isolated_dir("latch_name_mismatch");
    write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
    let latched = std::sync::Mutex::new(None);
    let _ = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();

    write_pre_latch(
        &root,
        "puid-1",
        "iid-A",
        "snare_tmp",
        "active",
        &latch_now(),
    );
    let (d, sd, _) = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert_eq!(d.mode, DeltaMode::Active);
    assert!(!sd);
    assert_eq!(
        latched.lock().unwrap().as_ref().unwrap().instance_id,
        "iid-A"
    );
}

/// T7: Record 中（recording=true）はラッチ凍結 — 名前変更でもアンラッチしない（W-284 同型）。
#[test]
fn latch_frozen_during_record() {
    let root = isolated_dir("latch_record");
    write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
    let latched = std::sync::Mutex::new(None);
    // Watch で初回ラッチ。
    let _ = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert!(latched.lock().unwrap().is_some());
    // Record 中に名前変更（別名）→ アンラッチしない（凍結）。
    let _ = compute_latched_display(&root, "kick", &latch_post(), Some("kick"), true, &latched)
        .unwrap();
    assert_eq!(
        latched.lock().unwrap().as_ref().unwrap().instance_id,
        "iid-A",
        "Record 中は名前変更でもラッチ凍結"
    );
}
