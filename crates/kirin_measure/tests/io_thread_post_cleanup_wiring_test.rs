//! B-027 段階 3-B α-7 / Group 2 統合点 #4 配線確証 (Gap-6 局所対処)
//!
//! IO Thread POST terminate 終端で record_signal/{POST_iid}.json が
//! 所有中の signal だけが `Released` へ遷移することをソース文字列上で固定する。
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
/// `record_signal::mark_released_if_current` 呼出が存在し、failure は warn ログのみ。
#[test]
fn io_thread_post_terminate_marks_released() {
    let coordinator = read("src/io_thread_post.rs");
    let src = read("src/io_thread_post_shutdown.rs");

    assert!(
        coordinator.contains("shutdown_post_io("),
        "POST coordinator must invoke the bounded shutdown lifecycle"
    );

    assert!(
        src.contains("record_signal::mark_released_if_current("),
        "IO Thread POST terminate must compare-and-release its exact owned signal"
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

/// compare-and-release 呼出は terminate ログ "[IOThread POST] terminated" より
/// 前に位置すること。逆順だと thread 終了直前の明示 Stop 伝播が抜ける。
#[test]
fn io_thread_post_mark_released_precedes_terminated_log() {
    let src = read("src/io_thread_post_shutdown.rs");
    let release_idx = src
        .find("record_signal::mark_released_if_current(")
        .expect("compare-and-release call must exist (統合点 #4)");
    let terminated_idx = src
        .find(r#""[IOThread POST] terminated""#)
        .expect("terminate log must exist");
    assert!(
        release_idx < terminated_idx,
        "compare-and-release must precede `[IOThread POST] terminated` log \
         (release={release_idx}, terminated={terminated_idx})"
    );
}

/// pluginval opens/closes the same restored instance quickly. Teardown cleanup
/// must not delete watch JSON or atomic temp siblings because a replacement IO
/// thread may already be writing the same path.
#[test]
fn io_thread_termination_does_not_delete_live_watch_files() {
    let pre = read("src/io_thread_pre.rs");
    let post = read("src/io_thread_post_shutdown.rs");

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
        pre.contains("Dropping this thread's WatchSnapshotLease"),
        "PRE teardown must release only its runtime-specific watch lease"
    );
    assert!(
        pre.contains("mtime expiry path"),
        "PRE source must preserve the legacy snapshot mtime handoff"
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
        post.contains("Dropping this thread's unique WatchSnapshotLease"),
        "POST teardown must release only its runtime-specific watch lease"
    );
    assert!(
        post.contains("mtime expiry path"),
        "POST source must preserve the legacy snapshot mtime handoff"
    );
}

/// Watch owner leases make old snapshots non-authoritative. Plugin startup must not make every
/// instance recursively clean the shared `/tmp/kirin` or plugin_data history.
#[test]
fn io_thread_startup_never_sweeps_shared_history() {
    let pre = read("src/io_thread_pre.rs");
    let post = read("src/io_thread_post.rs");
    for (role, src) in [("PRE", pre.as_str()), ("POST", post.as_str())] {
        for forbidden in [
            "sweep_stale_pending_at_startup()",
            "sweep_stale_watch_files_at_startup()",
            "sweep_stale_reservations_in(",
            "recover_orphan_tmps(",
            "sweep_stale_active_at_startup(",
        ] {
            assert!(
                !src.contains(forbidden),
                "{role} startup/runtime must not call shared-history cleanup: {forbidden}"
            );
        }
    }
}

/// PRE の PAIR 表示は editor の 10 Hz tick から呼ばれる。ここへ候補列挙を戻すと、
/// GUI を開くだけで `/tmp/kirin` 全体を毎秒何度も走査する退行になる。
#[test]
fn pre_pair_status_reads_one_exact_claim_without_enumeration() {
    let src = read("src/pre_pair_status.rs");
    let end = src
        .find("\n#[cfg(test)]")
        .expect("pair_status_for_pre production block must end before tests");
    let body = &src[..end];

    assert!(
        body.contains("try_observe_pair_claim"),
        "PRE pair status must start from its fixed ownership claim"
    );
    for forbidden in [
        "read_dir",
        "enumerate_live_post_pair_candidates",
        "scan_post_candidates_in",
        "discover_active_post_dirs",
    ] {
        assert!(
            !body.contains(forbidden),
            "PRE pair status must not enumerate runtime/history state: {forbidden}"
        );
    }
}

/// POST の定常 IO loop は確定 claim と exact closed-session paths だけを読む。
/// 全 project の late reconciliation はテスト/旧移行 helper として残っても、この loop からは
/// 到達不能でなければならない。
#[test]
fn post_runtime_never_reconciles_project_history() {
    let src = read("src/io_thread_post.rs");
    assert!(
        src.contains("reconcile_drop_committed_closed_session"),
        "closed Drop recovery must name one exact session"
    );
    assert!(
        !src.contains("reconcile_late_expected_wav_project("),
        "POST runtime must not recursively reconcile a project"
    );
}
