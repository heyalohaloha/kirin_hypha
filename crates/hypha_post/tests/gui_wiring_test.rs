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
