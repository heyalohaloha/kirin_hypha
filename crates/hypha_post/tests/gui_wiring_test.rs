//! T-E / T-F 配線の構造回帰テスト（guardian_77 v3 §9 / §10）。
//!
//! egui 描画ループは cargo test から直接回せないため、editor.rs / lib.rs の
//! ソース文字列上で配線が残っていることを固定する。配線が落ちると
//! 「proposals が生成されても Hypha GUI が沈黙する」サイレント断線になる。

use std::fs;
use std::path::PathBuf;

fn crate_root() -> PathBuf {
    PathBuf::from(env!("CARGO_MANIFEST_DIR"))
}

fn read(rel: &str) -> String {
    let path = crate_root().join(rel);
    fs::read_to_string(&path).unwrap_or_else(|e| panic!("read {rel}: {e}"))
}

#[test]
fn lib_rs_exposes_three_new_arcs_to_editor() {
    let src = read("src/lib.rs");
    for needle in [
        "installation_id: Arc<String>",
        "playback_pos_samples: Arc<AtomicI64>",
        "playback_sample_rate: Arc<AtomicU32>",
    ] {
        assert!(
            src.contains(needle),
            "lib.rs must declare `{needle}` on HyphaPost (T-E/T-F state)"
        );
    }
}

#[test]
fn lib_rs_process_writes_pos_samples_atomic() {
    let src = read("src/lib.rs");
    assert!(
        src.contains("self.playback_pos_samples.store("),
        "process() must push transport().pos_samples() into the shared atomic (T-F)"
    );
    assert!(
        src.contains("transport.pos_samples()"),
        "process() must read pos_samples from the ProcessContext transport"
    );
}

#[test]
fn lib_rs_initialize_caches_sample_rate() {
    let src = read("src/lib.rs");
    // sample rate must land in the atomic via initialize() for the editor
    // to convert pos_samples → seconds (T-F).
    assert!(
        src.contains("self.playback_sample_rate"),
        "initialize() must cache sample_rate into playback_sample_rate"
    );
    assert!(
        src.contains("buffer_config.sample_rate as u32"),
        "initialize() must store buffer_config.sample_rate"
    );
}

#[test]
fn editor_rs_contains_t_e_t_f_render_helpers() {
    let src = read("src/editor.rs");
    for needle in [
        "fn maybe_rescan_proposals",
        "fn current_playback_time",
        "fn draw_proposals_block",
        "fn severity_glyph",
        "scan_latest_v2_preset(",
        "lookup_section_label(",
    ] {
        assert!(
            src.contains(needle),
            "editor.rs must contain T-E/T-F helper `{needle}`"
        );
    }
}

#[test]
fn editor_rs_calls_draw_proposals_block_from_draw_post() {
    let src = read("src/editor.rs");
    let draw_post_start = src
        .find("fn draw_post(")
        .expect("draw_post function must exist");
    // Look for the invocation within a reasonable window of draw_post body.
    let window = &src[draw_post_start..(draw_post_start + 4000).min(src.len())];
    assert!(
        window.contains("draw_proposals_block(ui, state, now)"),
        "draw_post must invoke draw_proposals_block (T-E + T-F)"
    );
}

#[test]
fn editor_rs_uses_r28_silence_when_proposals_and_section_both_absent() {
    // draw_proposals_block must early-return without rendering (R-28) when
    // both `has_proposals` and `section_label` are absent.
    let src = read("src/editor.rs");
    assert!(
        src.contains("if !has_proposals && section_label.is_none()"),
        "draw_proposals_block must gate render with R-28 silence check"
    );
}

#[test]
fn editor_rs_uses_wall_clock_fallback_for_pos() {
    // §10.2 fallback: when transport reports unavailable, fall back to
    // wall-clock delta from Record start.
    let src = read("src/editor.rs");
    assert!(
        src.contains("record_start_wall_time"),
        "editor.rs must carry record_start_wall_time fallback anchor"
    );
    assert!(
        src.contains("|start| now - start"),
        "current_playback_time must compute (now - record_start_wall_time) as fallback"
    );
}

#[test]
fn editor_rs_throttles_proposals_scan_to_500ms() {
    let src = read("src/editor.rs");
    assert!(
        src.contains("PROPOSALS_SCAN_INTERVAL_SECS"),
        "proposals scan throttle constant missing"
    );
    assert!(
        src.contains("0.5"),
        "proposals scan interval must be 0.5 seconds"
    );
}

#[test]
fn editor_rs_caps_rendered_cards() {
    let src = read("src/editor.rs");
    assert!(
        src.contains("MAX_CARDS_RENDERED"),
        "editor.rs must bound the rendered card list (300x200px budget)"
    );
}

// ── A-3 修正: instance_id 永続化 + pair_label 配線 ───────────────────────

/// HyphaPostParams に instance_id が `#[persist = "instance_id"]` で
/// `Arc<RwLock<String>>` 型で永続化されていること。これが無いと DAW 再保存→
/// 再起動で PRE/POST のペアリングが切れ、record_signal の target_pre_instance_id
/// 一致判定が崩れる（A-3 致命級）。
///
/// B-022 段階 1: 型を `RwLock<String>` から `Arc<RwLock<String>>` に変更
/// （chunk-restore 後の最新値を editor / io_thread に lazy-read 経由で伝播
/// するため。nih-plug `params/persist.rs` の `impl_persistent_arc!` で
/// `Arc<RwLock<T>>` も `RwLock<T>` と同等に扱われる / chunk JSON は不変）。
#[test]
fn lib_rs_persists_instance_id_via_rwlock_string() {
    let src = read("src/lib.rs");
    assert!(
        src.contains(r#"#[persist = "instance_id"]"#),
        "HyphaPostParams must annotate instance_id with `#[persist = \"instance_id\"]`"
    );
    assert!(
        src.contains("instance_id: Arc<RwLock<String>>"),
        "instance_id must be `Arc<RwLock<String>>` (B-022 段階 1: lazy-read 共有用)"
    );
}

/// HyphaPost には project_hash と daw_session_id が新しく載っていること。
#[test]
fn lib_rs_carries_project_hash_and_daw_session_id() {
    let src = read("src/lib.rs");
    assert!(
        src.contains("project_hash"),
        "HyphaPost must carry process-shared project_hash"
    );
    assert!(
        src.contains("daw_session_id"),
        "HyphaPost must carry process-shared daw_session_id (cross-process barrier)"
    );
}

/// pair_label の Arc<Mutex<String>> が POST 側で生成され editor へ渡されること。
#[test]
fn lib_rs_wires_pair_label_to_editor() {
    let src = read("src/lib.rs");
    assert!(
        src.contains("pair_label: Arc<Mutex<String>>"),
        "HyphaPost must hold pair_label as Arc<Mutex<String>> for sub-3 visibility"
    );
}

/// editor.rs に set_pair_label / clear_pair_label が定義されており、
/// format_pair_label は kirin_measure から import されていること（B-023 段階 4）。
/// `format_pair_label` の単一定義点は kirin_measure::io_thread_post に移動した
/// ため、editor.rs はそれを `use kirin_measure::format_pair_label;` で取り込む。
#[test]
fn editor_rs_has_pair_label_helpers() {
    let src = read("src/editor.rs");
    for needle in ["fn set_pair_label", "fn clear_pair_label"] {
        assert!(
            src.contains(needle),
            "editor.rs must define helper `{needle}` (B-023 段階 4 pair_label pair)"
        );
    }
    assert!(
        src.contains("format_pair_label"),
        "editor.rs must reference format_pair_label (imported from kirin_measure)"
    );
    // B-023 段階 4 (判断 2): PRE_ プレフィックスは完全廃止
    assert!(
        !src.contains(r#""pair: PRE_""#) && !src.contains("pair: PRE_{}"),
        "PRE_ prefix must be removed in B-023 段階 4 (decision 2)"
    );
}

/// pairing_label の描画が `if recording { ... }` で gate されていること。
/// Watch 中はラベル自体が非表示なので、空文字でも空行が出ない。
#[test]
fn editor_rs_gates_pairing_label_with_recording_flag() {
    let src = read("src/editor.rs");
    let pairing_call_idx = src
        .find("pairing_label(ui, pair)")
        .expect("pairing_label call must exist in editor.rs");
    // 直前 200 文字以内に `if recording` があれば gate されているとみなす。
    let window_start = pairing_call_idx.saturating_sub(200);
    let window = &src[window_start..pairing_call_idx];
    assert!(
        window.contains("if recording"),
        "pairing_label call must be wrapped in `if recording {{ ... }}` (Watch 中は描画省略)"
    );
}
