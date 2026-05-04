//! B-027 段階 3-B α-7 / Group 2 統合点 #4 配線確証 (Gap-6 局所対処)
//!
//! IO Thread POST terminate 終端で record_signal/{POST_iid}.json の
//! `delete_signal` が呼ばれていることをソース文字列上で固定する。
//! Watchdog restart 経路や shutdown=true で loop 抜けた直後に delete が
//! 落ちると orphan が残り、PRE 側で偽の Pending を観測する事故が再発する。
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
/// `record_signal::delete_signal` 呼出が存在し、failure は warn ログのみ。
#[test]
fn io_thread_post_terminate_calls_delete_signal() {
    let src = read("src/io_thread_post.rs");

    assert!(
        src.contains("record_signal::delete_signal("),
        "IO Thread POST terminate must call record_signal::delete_signal \
         (Group 2 統合点 #4 / Gap-6 局所対処)"
    );

    assert!(
        src.contains("[POST cleanup #4] record_signal deleted:"),
        "delete_signal 成功 log は info レベル ([POST cleanup #4] record_signal deleted)"
    );

    assert!(
        src.contains("[POST cleanup #4] delete_signal failed:"),
        "delete_signal 失敗 log は warn レベル / panic 禁止 (設計判断 #8)"
    );
}

/// delete_signal 呼出は terminate ログ "[IOThread POST] terminated" より
/// 前に位置すること。逆順だと thread 終了直前の cleanup が抜け、watchdog
/// restart シナリオで新 thread spawn 時に古い signal が残る (Gap-6 残留)。
#[test]
fn io_thread_post_delete_signal_precedes_terminated_log() {
    let src = read("src/io_thread_post.rs");
    let delete_idx = src
        .find("record_signal::delete_signal(")
        .expect("delete_signal call must exist (統合点 #4)");
    let terminated_idx = src
        .find(r#""[IOThread POST] terminated""#)
        .expect("terminate log must exist");
    assert!(
        delete_idx < terminated_idx,
        "delete_signal must precede `[IOThread POST] terminated` log \
         (delete={delete_idx}, terminated={terminated_idx}); reversal breaks \
         watchdog restart cleanup ordering"
    );
}
