//! A-3 修正: HyphaPre 側の instance_id 永続化と project_hash / daw_session_id
//! 配線が src/lib.rs 上に残っていることを構造的に固定する。配線が落ちると
//! DAW 再起動後に PRE/POST のペアリングが切れ、record_signal の
//! `target_pre_instance_id` 一致判定が崩れる（致命級）。

use std::fs;
use std::path::PathBuf;

fn read_lib_rs() -> String {
    let path = PathBuf::from(env!("CARGO_MANIFEST_DIR")).join("src/lib.rs");
    fs::read_to_string(&path).unwrap_or_else(|e| panic!("read src/lib.rs: {e}"))
}

/// B-022 段階 1: 型を `RwLock<String>` から `Arc<RwLock<String>>` に変更。
/// chunk-restore 後の最新値を io_thread の毎 tick lazy-read に届けるため、
/// `Arc` を共有する形に格上げ。`#[persist]` 互換は nih-plug
/// `params/persist.rs` の `impl_persistent_arc!` で担保 / chunk JSON は不変。
#[test]
fn lib_rs_persists_instance_id_via_rwlock_string() {
    let src = read_lib_rs();
    assert!(
        src.contains(r#"#[persist = "instance_id"]"#),
        "HyphaPreParams must annotate instance_id with `#[persist = \"instance_id\"]`"
    );
    assert!(
        src.contains("instance_id: Arc<RwLock<String>>"),
        "instance_id must be `Arc<RwLock<String>>` (B-022 段階 1: lazy-read 共有用)"
    );
}

#[test]
fn lib_rs_carries_project_hash_and_daw_session_id() {
    let src = read_lib_rs();
    assert!(
        src.contains("project_hash"),
        "HyphaPre must carry process-shared project_hash"
    );
    assert!(
        src.contains("daw_session_id"),
        "HyphaPre must carry process-shared daw_session_id (chunk-persistent session id)"
    );
}

/// IO Thread spawn 呼び出しが新シグネチャ（11 引数）に追従していること。
#[test]
fn lib_rs_spawn_io_thread_pre_passes_project_hash_and_daw_session_id() {
    let src = read_lib_rs();
    let call_idx = src
        .find("spawn_io_thread_pre(")
        .expect("spawn_io_thread_pre must be invoked from initialize()");
    // 呼び出し直後の数百文字を見て、project_hash / daw_session_id が引数として
    // 渡されていることを確認する（クローン経由で渡る）。
    let window_end = (call_idx + 1500).min(src.len());
    let window = &src[call_idx..window_end];
    assert!(
        window.contains("project_hash"),
        "spawn_io_thread_pre must receive project_hash"
    );
    assert!(
        window.contains("daw_session_id"),
        "spawn_io_thread_pre must receive daw_session_id"
    );
}

#[test]
fn lib_rs_process_stores_transport_playing_for_watch_max() {
    let src = read_lib_rs();
    assert!(
        src.contains("is_playing: Arc<AtomicBool>"),
        "HyphaPre must carry GUI-only is_playing for Watch MAX reset"
    );
    assert!(
        src.contains("self.is_playing.store(playing"),
        "process() must publish transport.playing to the PRE editor"
    );
    assert!(
        src.contains("Arc::clone(&self.is_playing)"),
        "editor() must pass is_playing into create_pre_editor"
    );
}
