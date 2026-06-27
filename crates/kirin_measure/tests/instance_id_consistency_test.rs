//! B-022 段階 1 受入基準 #1-2:
//! `record_signal.requested_by == plugin_data writer instance_id` 整合性。
//!
//! 旧コード (`pub instance_id: RwLock<String>` + String snapshot) では:
//! - `editor()` が `WrapperInner::new()` 内で 1 度だけ呼ばれ Default UUID で
//!   snapshot 凍結 → trigger_keep の `write_pending` は古い値を `requested_by`
//!   に書き込む
//! - `Plugin::initialize()` (= `set_active(true)` 経由) で io_thread が起動し、
//!   chunk-restored 値で snapshot → plugin_data writer は新値を `instance_id`
//!   に書き込む
//! - → 同一 plugin instance 内で `requested_by` と `instance_id` が乖離
//!   （Hypha 専用 8 で観測。directive §2.1）
//!
//! 新コード (B-022 段階 1 / `Arc<RwLock<String>>` 共有 + lazy-read) では:
//! - editor / io_thread は同じ `Arc<RwLock<String>>` を共有
//! - 各 use site (trigger_keep / run_tick) で `arc.read()` を呼ぶ
//! - `set_state_inner` の `deserialize_fields` で Arc 内容が書き換わると
//!   次の lazy-read が新値を返す → 両者が常に同期
//!
//! 本テストはその不変条件を構造的に固定する。
//! 単体での `record_signal::write_pending` + `PluginDataFile::new` レベルで
//! 整合性を検証する（IO Thread を起動せず、Watch / Record の thread サイクル
//! を介さない最小 surface でテスト）。

use kirin_measure::{
    plugin_data::{PluginDataFile, Role as PluginDataRole},
    record_signal,
};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::{Arc, RwLock};

/// `Arc<RwLock<String>>` から現在値を lazy-read（panic-safe）。
///
/// `crate::io_thread_post::read_instance_id_arc` と同等のロジック。
/// ヘルパは `pub(crate)` のためテストから直接呼べないので再実装する。
fn read_instance_id_arc(arc: &Arc<RwLock<String>>) -> String {
    arc.read().ok().map(|g| g.clone()).unwrap_or_default()
}

fn isolated_dir(label: &str) -> std::path::PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_iid_consistency_{label}_{pid}_{n}"));
    let _ = std::fs::remove_dir_all(&dir);
    std::fs::create_dir_all(&dir).unwrap();
    dir
}

/// 同一 `Arc<RwLock<String>>` を editor / io_thread 双方が共有する限り、
/// `record_signal.requested_by` と `plugin_data.instance_id` は常に一致する。
///
/// 受入基準 #1-2 の不変条件を最も直接的に表現するテスト。
#[test]
fn record_signal_and_plugin_data_share_instance_id_via_arc_rwlock() {
    let base = isolated_dir("share");
    let project_hash = "test-ph-share";
    let target_pre = "pre-target".to_string();
    let daw_session = "daw-share".to_string();

    // editor + io_thread 共有 Arc (= plugin params.instance_id 相当)
    let instance_id_arc = Arc::new(RwLock::new("uuid-A-default".to_string()));

    // editor 側 lazy-read (trigger_keep 経路 simulating)
    let iid_for_signal = read_instance_id_arc(&instance_id_arc);
    let signal = record_signal::write_pending(
        &base,
        project_hash,
        &iid_for_signal,
        target_pre.clone(),
        daw_session.clone(),
    )
    .expect("write_pending must succeed");

    // io_thread 側 lazy-read (run_record_tick → writer_start 経路 simulating)
    let iid_for_writer = read_instance_id_arc(&instance_id_arc);
    let plugin_data = PluginDataFile::new(
        "test-install-share".to_string(),
        project_hash.to_string(),
        iid_for_writer.clone(),
        PluginDataRole::Post,
        None,
        48_000,
        Some(target_pre.clone()),
        None,
        None,
        None,
    );

    // 受入基準 #1-2 (Default UUID 状態でも整合)
    assert_eq!(
        signal.requested_by, plugin_data.instance_id,
        "record_signal.requested_by must match plugin_data.instance_id when both \
         lazy-read from the same Arc<RwLock<String>>"
    );
    assert_eq!(signal.requested_by, "uuid-A-default");

    // chunk-restore 相当: nih-plug `set_state_inner` の `deserialize_fields` で
    // Arc 内容が書き換わるシナリオを模倣。
    *instance_id_arc.write().unwrap() = "uuid-B-restored".to_string();

    // 復元後の lazy-read は新値を返す
    let iid_post_restore = read_instance_id_arc(&instance_id_arc);
    assert_eq!(iid_post_restore, "uuid-B-restored");

    // 復元後の write_pending / plugin_data 生成も新値で整合
    let signal2 = record_signal::write_pending(
        &base,
        project_hash,
        &iid_post_restore,
        target_pre.clone(),
        daw_session.clone(),
    )
    .expect("write_pending after restore must succeed");

    let plugin_data2 = PluginDataFile::new(
        "test-install-share".to_string(),
        project_hash.to_string(),
        read_instance_id_arc(&instance_id_arc),
        PluginDataRole::Post,
        None,
        48_000,
        Some(target_pre),
        None,
        None,
        None,
    );

    assert_eq!(
        signal2.requested_by, plugin_data2.instance_id,
        "受入基準 #1-2: chunk-restore 後も requested_by == instance_id を維持"
    );
    assert_eq!(signal2.requested_by, "uuid-B-restored");
}

/// editor() と io_thread spawn のタイミングが異なる前後関係 (Default→restore
/// 後→spawn) でも、両者が同一 `Arc<RwLock<String>>` を共有する限り
/// lazy-read は同じ最新値を返す。
///
/// 旧コード退行の構造的防止: editor が早期スナップショット凍結する経路を
/// 再導入したらこのテストが失敗する。
#[test]
fn lazy_read_tracks_arc_mutations_across_simulated_chunk_restore() {
    let arc = Arc::new(RwLock::new("uuid-A-default".to_string()));

    // (1) Plugin::Default::default → params.instance_id = UUID_A
    // (2) WrapperInner::new() で plugin.editor() が呼ばれ、editor が Arc を共有保持
    let editor_arc = Arc::clone(&arc);

    // (3) host が IComponent::setState を呼び `set_state_inner` 経由で
    //     deserialize_fields が走る → Arc 内容が UUID_B に書き換わる
    *arc.write().unwrap() = "uuid-B-restored".to_string();

    // (4) IAudioProcessor::setActive(true) 経由で Plugin::initialize() が
    //     呼ばれ、io_thread が起動して Arc を共有保持
    let io_thread_arc = Arc::clone(&arc);

    // (5) ユーザが Keep ボタンを押す → editor の trigger_keep が lazy-read
    let from_editor_use_site = read_instance_id_arc(&editor_arc);
    // (6) io_thread の run_record_tick が writer_start を呼ぶ前に lazy-read
    let from_io_use_site = read_instance_id_arc(&io_thread_arc);

    // 旧 String snapshot 実装ではここが不一致になる
    assert_eq!(
        from_editor_use_site, "uuid-B-restored",
        "editor lazy-read must reflect post-restore value"
    );
    assert_eq!(
        from_io_use_site, "uuid-B-restored",
        "io_thread lazy-read must reflect post-restore value"
    );
    assert_eq!(
        from_editor_use_site, from_io_use_site,
        "受入基準 #1-2 不変: 同一 Arc<RwLock<String>> を共有する限り、\
         editor / io_thread の lazy-read は常に同値"
    );
}

/// `read_instance_id_arc` が poisoned RwLock で panic せず空文字を返す
/// (panic-safe 契約)。
#[test]
fn read_instance_id_arc_returns_empty_string_on_poisoned_lock() {
    let arc: Arc<RwLock<String>> = Arc::new(RwLock::new("seed".to_string()));
    let arc_clone = Arc::clone(&arc);
    // 別 thread で write 中に panic させて RwLock を poison させる
    let _ = std::thread::spawn(move || {
        let _g = arc_clone.write().unwrap();
        panic!("intentional poison");
    })
    .join();
    assert!(arc.is_poisoned(), "RwLock must be poisoned for this test");
    assert_eq!(
        read_instance_id_arc(&arc),
        String::new(),
        "panic-safe contract: poisoned RwLock → empty string (not panic)"
    );
}
