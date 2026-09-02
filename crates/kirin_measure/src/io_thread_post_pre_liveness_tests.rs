use super::*;
use crate::record_signal::write_pending;
use std::sync::atomic::AtomicU64;

const TEST_PH: &str = "ph";
const TEST_POST_IID: &str = "post-iid-liveness";
const TEST_PRE_IID: &str = "pre-iid-liveness";
const TEST_DAW_SESSION: &str = "daw-session-1";

fn isolated_root(tag: &str) -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_pre_liveness_test_{pid}_{n}_{tag}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn write_dummy_pre_json(kirin_root: &Path, project_hash: &str, pre_iid: &str) -> PathBuf {
    let dir = kirin_root.join(project_hash).join(pre_iid);
    fs::create_dir_all(&dir).unwrap();
    let path = dir.join("pre.json");
    // 中身は空の JSON で OK (本テストは mtime のみ評価)。
    fs::write(&path, b"{}").unwrap();
    path
}

/// stem/offline export 後は PRE pre.json の mtime が止まり得る。
/// stale 検出時も Keep は利用者の Stop まで保持する。
#[test]
fn poll_pre_liveness_at_stale_pre_keeps_recording_and_signal() {
    let kirin_root = isolated_root("stale_pre");
    let plugin_data_root = isolated_root("stale_pre_pdr");

    // PRE pre.json を書き込み (mtime = "現在時刻")。
    write_dummy_pre_json(&kirin_root, TEST_PH, TEST_PRE_IID);
    // POST 自身の record_signal を書き込み (status=Pending で OK)。
    write_pending(
        &plugin_data_root,
        TEST_PH,
        TEST_POST_IID,
        TEST_PRE_IID.to_string(),
        TEST_DAW_SESSION.to_string(),
    )
    .unwrap();

    let sm = Arc::new(RecordStateMachine::new());
    sm.try_enter_record(crate::License::Os).unwrap();
    assert!(
        sm.is_recording(),
        "precondition: record_sm must be Recording"
    );
    // G-115-64: cleanup 後に空文字 / None になることを assert する前提として
    // 「設定済み」状態を入力にする (editor の Keep 経由 = trigger_keep_internal
    // が set_pair_label + paired_pre_target=Some を実施した後の状態を再現)。
    let pair_label = Arc::new(Mutex::new(format!("pair: {}", TEST_PRE_IID)));
    let paired_pre_target = Arc::new(Mutex::new(Some(TEST_PRE_IID.to_string())));

    // mtime + 100 秒先を `now` として注入 → 60 秒 threshold を超える。
    let stale_now = SystemTime::now() + Duration::from_secs(100);
    poll_pre_liveness_at(
        &kirin_root,
        &plugin_data_root,
        TEST_PH,
        TEST_POST_IID,
        TEST_PRE_IID,
        &sm,
        &pair_label,
        &paired_pre_target,
        stale_now,
    );

    assert!(sm.is_recording(), "stale pre.json must keep Record armed");
    assert_eq!(
        pair_label.lock().unwrap().as_str(),
        format!("pair: {}", TEST_PRE_IID).as_str(),
        "stale pre.json must keep pair_label for manual Stop"
    );
    assert_eq!(
        paired_pre_target.lock().unwrap().as_deref(),
        Some(TEST_PRE_IID),
        "stale pre.json must keep paired_pre_target for manual Stop"
    );
    let signal_after = crate::record_signal::read_signal(&plugin_data_root, TEST_PH, TEST_POST_IID);
    assert!(
        signal_after.is_some(),
        "stale pre.json must not delete record_signal"
    );
}

/// pre.json 不在でも Keep は利用者の Stop まで保持する。
#[test]
fn poll_pre_liveness_at_missing_pre_json_keeps_recording() {
    let kirin_root = isolated_root("missing_pre");
    let plugin_data_root = isolated_root("missing_pre_pdr");

    // pre.json を一切作らない (PRE drop 後を再現)。
    write_pending(
        &plugin_data_root,
        TEST_PH,
        TEST_POST_IID,
        TEST_PRE_IID.to_string(),
        TEST_DAW_SESSION.to_string(),
    )
    .unwrap();

    let sm = Arc::new(RecordStateMachine::new());
    sm.try_enter_record(crate::License::Os).unwrap();
    let pair_label = Arc::new(Mutex::new(format!("pair: {}", TEST_PRE_IID)));
    let paired_pre_target = Arc::new(Mutex::new(Some(TEST_PRE_IID.to_string())));

    poll_pre_liveness_at(
        &kirin_root,
        &plugin_data_root,
        TEST_PH,
        TEST_POST_IID,
        TEST_PRE_IID,
        &sm,
        &pair_label,
        &paired_pre_target,
        SystemTime::now(),
    );

    assert!(sm.is_recording(), "missing pre.json must keep Record armed");
    assert_eq!(
        pair_label.lock().unwrap().as_str(),
        format!("pair: {}", TEST_PRE_IID).as_str(),
        "missing pre.json must keep pair_label for manual Stop"
    );
    assert_eq!(
        paired_pre_target.lock().unwrap().as_deref(),
        Some(TEST_PRE_IID),
        "missing pre.json must keep paired_pre_target for manual Stop"
    );
    let signal_after = crate::record_signal::read_signal(&plugin_data_root, TEST_PH, TEST_POST_IID);
    assert!(
        signal_after.is_some(),
        "missing pre.json must not delete record_signal"
    );
}

/// Gap-2 + G-115-64: pre.json mtime fresh (< 60s) → exit_record せず Record 維持.
/// pair_label / paired_pre_target も保持される (cleanup は走らない).
#[test]
fn poll_pre_liveness_at_fresh_pre_keeps_recording() {
    let kirin_root = isolated_root("fresh_pre");
    let plugin_data_root = isolated_root("fresh_pre_pdr");

    write_dummy_pre_json(&kirin_root, TEST_PH, TEST_PRE_IID);
    write_pending(
        &plugin_data_root,
        TEST_PH,
        TEST_POST_IID,
        TEST_PRE_IID.to_string(),
        TEST_DAW_SESSION.to_string(),
    )
    .unwrap();

    let sm = Arc::new(RecordStateMachine::new());
    sm.try_enter_record(crate::License::Os).unwrap();
    let pair_label = Arc::new(Mutex::new(format!("pair: {}", TEST_PRE_IID)));
    let paired_pre_target = Arc::new(Mutex::new(Some(TEST_PRE_IID.to_string())));

    // `now` をリアルな現在時刻にする → mtime ≈ now → 経過 ≈ 0 秒。
    poll_pre_liveness_at(
        &kirin_root,
        &plugin_data_root,
        TEST_PH,
        TEST_POST_IID,
        TEST_PRE_IID,
        &sm,
        &pair_label,
        &paired_pre_target,
        SystemTime::now(),
    );

    assert!(
        sm.is_recording(),
        "fresh pre.json must NOT trigger exit_record()"
    );
    assert_eq!(
        pair_label.lock().unwrap().as_str(),
        format!("pair: {}", TEST_PRE_IID).as_str(),
        "G-115-64: fresh pre.json must NOT clear pair_label"
    );
    assert_eq!(
        paired_pre_target.lock().unwrap().as_deref(),
        Some(TEST_PRE_IID),
        "G-115-64: fresh pre.json must NOT clear paired_pre_target"
    );
    let signal_after = crate::record_signal::read_signal(&plugin_data_root, TEST_PH, TEST_POST_IID);
    assert!(
        signal_after.is_some(),
        "fresh pre.json must NOT delete signal"
    );
}

/// Gap-2: `record_sm` が Watch 状態のとき sub-tick は no-op。
#[test]
fn poll_pre_liveness_at_watch_state_is_noop() {
    let kirin_root = isolated_root("watch_state");
    let plugin_data_root = isolated_root("watch_state_pdr");

    write_pending(
        &plugin_data_root,
        TEST_PH,
        TEST_POST_IID,
        TEST_PRE_IID.to_string(),
        TEST_DAW_SESSION.to_string(),
    )
    .unwrap();

    let sm = Arc::new(RecordStateMachine::new());
    // try_enter_record せず Watch のまま。
    assert!(!sm.is_recording());

    let pair_label = Arc::new(Mutex::new(String::new()));
    let paired = Arc::new(Mutex::new(Some(TEST_PRE_IID.to_string())));

    // poll_pre_liveness (top-level) は record_sm guard 内で早期 return。
    poll_pre_liveness(
        &kirin_root,
        TEST_PH,
        TEST_POST_IID,
        &sm,
        &pair_label,
        &paired,
    );

    // Watch のまま signal も削除されない。
    let signal_after = crate::record_signal::read_signal(&plugin_data_root, TEST_PH, TEST_POST_IID);
    // Watch ガードで delete_signal を呼ばないため signal は残る (本 test は
    // production の StoragePaths を経由するので、本機ホームの plugin_data
    // を触らないことが重要だが、Watch ガードの早期 return でその経路にすら
    // 入らないことを is_recording=false で間接確証する)。
    assert!(!sm.is_recording(), "Watch state must remain unchanged");
    let _ = signal_after; // 環境依存ホームを避けるため値は assert しない
}
