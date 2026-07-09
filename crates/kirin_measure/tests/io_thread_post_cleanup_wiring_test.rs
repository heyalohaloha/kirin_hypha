//! B-027 段階 3-B α-7 / Group 2 統合点 #4 配線確証 (Gap-6 局所対処)
//!
//! IO Thread POST terminate 終端で record_signal/{POST_iid}.json が
//! `Released` へ遷移することをソース文字列上で固定する。
//! PRE は missing では止めないため、shutdown/watchdog restart 経路でも
//! 明示 Stop を観測できる必要がある。
//!
//! Runtime テストは spawn_io_thread_post の global state / fs::rename と
//! HOME env override の並列衝突のため難しい。設計判断 #5 / 設計判断 #8 の
//! 維持を保証する最小手段としてソース文字列回帰で固定する。

use std::fs;
use std::path::PathBuf;

fn crate_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

fn read(rel: &str) -> String {
    let path = crate_root().join(rel);
    fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {rel}: {e}"))
}

/// 統合点 #4: IO Thread terminate 終端 (loop 抜けた直後) に
/// `record_signal::mark_released` 呼出が存在し、failure は warn ログのみ。
#[test]
fn io_thread_post_terminate_marks_released() {
    let src = read("src/io_thread_post.rs");

    assert!(
        src.contains("record_signal::mark_released("),
        "IO Thread POST terminate must call record_signal::mark_released"
    );

    assert!(
        src.contains("[POST cleanup #4] mark_released ok"),
        "mark_released 成功 log は info レベル"
    );

    assert!(
        src.contains("[POST cleanup #4] mark_released failed:"),
        "mark_released 失敗 log は warn レベル / panic 禁止 (設計判断 #8)"
    );
}

/// mark_released 呼出は terminate ログ "[IOThread POST] terminated" より
/// 前に位置すること。逆順だと thread 終了直前の明示 Stop 伝播が抜ける。
#[test]
fn io_thread_post_mark_released_precedes_terminated_log() {
    let src = read("src/io_thread_post.rs");
    let release_idx = src
        .find("record_signal::mark_released(")
        .expect("mark_released call must exist (統合点 #4)");
    let terminated_idx = src
        .find(r#""[IOThread POST] terminated""#)
        .expect("terminate log must exist");
    assert!(
        release_idx < terminated_idx,
        "mark_released must precede `[IOThread POST] terminated` log \
         (release={release_idx}, terminated={terminated_idx})"
    );
}

/// pluginval opens/closes the same restored instance quickly. Teardown cleanup
/// must not delete watch JSON or atomic temp siblings because a replacement IO
/// thread may already be writing the same path.
#[test]
fn io_thread_termination_does_not_delete_live_watch_files() {
    let pre = read("src/io_thread_pre.rs");
    let post = read("src/io_thread_post.rs");

    assert!(
        !pre.contains("fs::remove_file(&final_file)"),
        "PRE teardown must not delete pre.json; freshness TTL handles stale files"
    );
    assert!(
        !pre.contains("remove_temp_siblings(&final_file)"),
        "PRE teardown must not delete atomic temp siblings owned by a replacement writer"
    );
    assert!(
        !pre.contains("fs::remove_dir(&final_dir)"),
        "PRE teardown must not remove a directory a replacement writer may be using"
    );
    assert!(
        pre.contains("stale watch files age"),
        "PRE source must document the mtime freshness handoff"
    );

    assert!(
        !post.contains("fs::remove_file(&final_post_file)"),
        "POST teardown must not delete post.json; freshness TTL handles stale files"
    );
    assert!(
        !post.contains("remove_temp_siblings(&final_post_file)"),
        "POST teardown must not delete atomic temp siblings owned by a replacement writer"
    );
    assert!(
        !post.contains("fs::remove_dir(&final_instance_dir)"),
        "POST teardown must not remove a directory a replacement writer may be using"
    );
    assert!(
        post.contains("Freshness is mtime-gated by readers"),
        "POST source must document the mtime freshness handoff"
    );
}
