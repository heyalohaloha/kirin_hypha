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
fn lib_rs_offline_end_auto_stop_is_wired_from_initialize() {
    let src = read("src/lib.rs");
    assert!(
        src.contains(
            "fn offline_render_ended(prev: Option<ProcessMode>, current: ProcessMode) -> bool"
        ),
        "lib.rs must keep offline-end detection isolated for testing"
    );
    assert!(
        src.contains("prev == Some(ProcessMode::Offline) && current == ProcessMode::Realtime"),
        "VST3 offline-end detection must be exactly Offline -> Realtime"
    );

    let initialize_start = src
        .find("fn initialize(")
        .expect("initialize function must exist");
    let mut safe_end = (initialize_start + 4500).min(src.len());
    while safe_end > initialize_start && !src.is_char_boundary(safe_end) {
        safe_end -= 1;
    }
    let window = &src[initialize_start..safe_end];

    assert!(
        window.contains("offline_render_auto_stop_due("),
        "initialize() must use the guarded offline auto-stop predicate"
    );
    assert!(
        window.contains("offline_autostop_enabled()"),
        "offline-end auto-stop must require explicit opt-in"
    );
    assert!(
        window.contains("self.offline_render_samples.load(Ordering::Relaxed)"),
        "opt-in offline-end auto-stop must require current-record offline process samples"
    );
    assert!(
        window.contains(".offline_render_sample_generation"),
        "offline-end auto-stop must be tied to the current Record generation"
    );
    assert!(
        window.contains("&& recording_now"),
        "opt-in offline-end auto-stop must only run while Keep/Record is active"
    );
    assert!(
        window.contains("crate::editor::trigger_stop_internal("),
        "offline-end auto-stop must reuse manual Stop cleanup"
    );
    assert!(
        window.contains("self.prev_process_mode = Some(buffer_config.process_mode);"),
        "initialize() must update prev_process_mode after edge handling"
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
    // 5000 byte 上限は B-027 段階 2 で Name 入力欄追加に伴う body 増を吸収
    // (元 4000 では draw_proposals_block 呼出 byte index がぎりぎり外に出る)。
    // window 5000 → 6000: B-048 / G-115-245 NoPre 分岐に Last Known Good if-let
    // 追加 (draw_post body 5587 byte 計測 / 6000 確保)。
    // window 6000 → 7000: B-049 / G-115-247 Inactive 分岐に if-let 追加で
    // draw_proposals_block 呼出が rel 5967 byte → 文字列終端 6003 byte で
    // 6000 window から 3 byte はみ出す (contains 不成立)。1000 byte 余裕を確保。
    // window 7000 → 9000: W-283 / G-115-251 で draw_post 直下 pair_pre_name
    // snapshot + Active arm 内 pair_empty wrap + Keep gate (UTF-8 注釈付) を追加
    // して body が ~845 byte 増 (Python rb 計測 rel 7845 byte / 余裕含み 9000)。
    // UTF-8 char boundary を跨がないよう boundary まで walk-back する
    // (Japanese comments が raw byte index 上で multi-byte char の途中に当たる)。
    let mut safe_end = (draw_post_start + 9000).min(src.len());
    while safe_end > draw_post_start && !src.is_char_boundary(safe_end) {
        safe_end -= 1;
    }
    let window = &src[draw_post_start..safe_end];
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

/// editor.rs に set_pair_label が定義されており、`clear_pair_label` /
/// `exit_record_full` は kirin_measure から import されていること
///
/// G-115-64 構造的修正: clear_pair_label の定義は `kirin_measure::cleanup` に
/// 集約された (editor 側 / IO Thread 側で同一関数を共有 / 構造的契約一点生成).
/// editor.rs では production 経路で `exit_record_full` のみ呼び、
/// `clear_pair_label` は test mod から `use kirin_measure::clear_pair_label;` で
/// 取り込む (本ファイルでも assert).
#[test]
fn editor_rs_has_pair_label_helpers() {
    let src = read("src/editor.rs");
    // set_pair_label は editor 固有 (Record 開始時の format_pair_label 経由).
    assert!(
        src.contains("fn set_pair_label"),
        "editor.rs must define helper `fn set_pair_label` (B-023 段階 4 pair_label pair)"
    );
    // G-115-64: clear_pair_label の **ローカル定義は禁止** (kirin_measure に集約).
    assert!(
        !src.contains("fn clear_pair_label"),
        "G-115-64: editor.rs must NOT define `fn clear_pair_label` locally — it is centralized in kirin_measure::cleanup"
    );
    // G-115-64: exit_record_full を import + Record→Watch 遷移で使用していること.
    assert!(
        src.contains("exit_record_full"),
        "G-115-64: editor.rs must use kirin_measure::exit_record_full for Record→Watch cleanup symmetry"
    );
    // G-115-64: test mod から clear_pair_label を kirin_measure 経由で取り込むこと.
    assert!(
        src.contains("use kirin_measure::clear_pair_label"),
        "G-115-64: editor.rs test mod must import clear_pair_label from kirin_measure (single source)"
    );
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

/// B-027 段階 3 (a) 仮説 2 (G-115-53): ComboBox 候補ループが
/// `ui.push_id(cand.instance_id.as_str(), |ui| { ... })` でラップされていること。
/// instance_id (UUID v4) を ID 種に使うことで、sort 順入替で label_text が
/// 同 index 位置で変動しても widget identity が固定される (#5-A-3 異常 2 防止)。
/// auto-ID 依存だと press/release フレーム間で ID 不一致 → clicked() 喪失。
#[test]
fn editor_rs_wraps_combo_entries_with_push_id() {
    let src = read("src/editor.rs");
    let combo_fn_idx = src
        .find("fn draw_pair_pre_combo(")
        .expect("draw_pair_pre_combo must exist (B-027 段階 3-A)");
    let combo_end = src[combo_fn_idx..]
        .find("fn all_keep_dropdown_label(")
        .map(|idx| combo_fn_idx + idx)
        .expect("draw_pair_pre_combo should be followed by all_keep_dropdown_label");
    let body = &src[combo_fn_idx..combo_end];
    assert!(
        body.contains("ui.push_id(cand.instance_id.as_str()"),
        "draw_pair_pre_combo must wrap each entry with `ui.push_id(cand.instance_id.as_str(), ...)` (B-027 段階 3 (a) 仮説 2)"
    );
    // selectable_label 呼出は push_id クロージャ内に居ること (順序確認)。
    let push_idx = body
        .find("ui.push_id(cand.instance_id.as_str()")
        .expect("push_id wrap must precede selectable_label");
    let select_idx = body[push_idx..]
        .find("ui.selectable_label(")
        .map(|idx| push_idx + idx)
        .expect("selectable_label call must remain in draw_pair_pre_combo");
    assert!(
        push_idx < select_idx,
        "push_id wrap must precede selectable_label call (push_id={push_idx}, select={select_idx})"
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

#[test]
fn editor_rs_keep_uses_authoritative_reservation_cap() {
    let src = read("src/editor.rs");
    let start = src
        .find("pub(crate) fn trigger_keep_internal(")
        .expect("trigger_keep_internal must exist");
    let end = src[start..]
        .find("fn trigger_all_keep_broadcast(")
        .map(|idx| start + idx)
        .expect("trigger_all_keep_broadcast must follow trigger_keep_internal");
    let body = &src[start..end];

    assert!(
        body.contains("reservation::sweep_stale_reservations"),
        "Keep must sweep stale reservation frames before counting the cap"
    );
    assert!(
        !body.contains("check_record_exclusion"),
        "Keep must not pre-check >=12 before reserving; reserve->count>MAX is authoritative"
    );
    assert!(
        body.contains("reservation::reserve_pairing"),
        "Keep must reserve the concrete PRE/POST pairing before cap enforcement"
    );
    assert!(
        body.contains(
            "count_distinct_pairings(&plugin_data_dir, project_hash) > MAX_ACTIVE_PER_PROJECT"
        ),
        "Keep must reject only after the concrete reservation makes the count exceed MAX"
    );
}

// ── B-027 段階 3-B α-7 / Group 2 (Gap-6 局所対処) 配線確証 ────────────────
//
// POST 側 record_signal/{POST_iid}.json の cleanup 責任を構造的に保証する。
// 統合点 #2 (trigger_stop) と #3 (HyphaPost::drop) で `delete_signal` 呼出が
// 落ちると orphan signal が残留し、PRE 側で偽の Pending を観測する事故が
// 静音再発する (Gap-6)。runtime テストは cdylib + global state の壁で困難な
// ため、ソース文字列上で固定する (gui_wiring_test と同じ R-22 配線回帰枠)。

/// 統合点 #2: trigger_stop 内で mark_released の後に delete_signal が呼ばれる。
/// 順序が逆 (delete → mark_released) だと PRE 側 1 秒 polling との race で
/// mark_released 結果が常に観測不能になり、設計判断 #9 (i) の「PRE 側 file
/// removed 経路」と二重防御が崩れる。
#[test]
fn editor_rs_trigger_stop_calls_delete_signal_after_mark_released() {
    let src = read("src/editor.rs");
    let mark_idx = src
        .find("mark_released(&plugin_data_dir, project_hash, instance_id)")
        .expect("trigger_stop must call mark_released (B-027 段階 3-B α-7)");
    let delete_idx = src
        .find("delete_signal(&plugin_data_dir, project_hash, instance_id)")
        .expect("trigger_stop must call delete_signal (Group 2 統合点 #2)");
    assert!(
        mark_idx < delete_idx,
        "delete_signal must be called AFTER mark_released in trigger_stop \
         (mark={mark_idx}, delete={delete_idx}); reversal breaks PRE-side \
         polling observation (設計判断 #9 (i))"
    );
    // 失敗時は warn のみ (panic 禁止 / 設計判断 #8)
    assert!(
        src.contains("[POST cleanup #2] delete_signal failed"),
        "trigger_stop delete_signal failure must log warn only (no panic)"
    );
}

/// 統合点 #2 broadcast (B-027 段階 3-B α-7-4-D / Step 12-A): trigger_stop 内で
/// 既存 delete_signal の **後** に delete_broadcast が呼ばれる。順序が逆だと
/// originator 自身が broadcast 削除 → 受信側 cache に未登録の orphan が残留する
/// (NotFound→Ok) のため統合点 #3 (Drop) / #4 (IO Thread terminate) との重複呼出
/// は安全。失敗時 warn のみ (設計判断 #8 / 既存 delete_signal と同規範)。
#[test]
fn editor_rs_trigger_stop_calls_delete_broadcast_after_delete_signal() {
    let src = read("src/editor.rs");
    let delete_signal_idx = src
        .find("delete_signal(&plugin_data_dir, project_hash, instance_id)")
        .expect("trigger_stop must call delete_signal (Group 2 統合点 #2)");
    let delete_broadcast_idx = src
        .find("delete_broadcast(&plugin_data_dir, project_hash, instance_id)")
        .expect("trigger_stop must call delete_broadcast (Step 12-A 統合点 #2 broadcast)");
    assert!(
        delete_signal_idx < delete_broadcast_idx,
        "delete_broadcast must be called AFTER delete_signal in trigger_stop \
         (delete_signal={delete_signal_idx}, delete_broadcast={delete_broadcast_idx}); \
         reversal breaks orphan broadcast cleanup (DEV INBOX §9-3)"
    );
    // 失敗時は warn のみ (panic 禁止 / 設計判断 #8)
    assert!(
        src.contains("[POST cleanup #2 broadcast] delete_broadcast failed"),
        "trigger_stop delete_broadcast failure must log warn only (no panic)"
    );
}

/// 統合点 #3: HyphaPost::drop 内 watchdog join 後に delete_signal が呼ばれる。
/// 順序: record_sm.exit_record() → shutdown flags → watchdog join →
///       delete_signal。
/// watchdog join を待たずに delete を先行すると、IO Thread terminate (#4) の
/// 完了前に file が消え、log 上で「先に消えた」のような race 痕跡が出るが
/// 冪等のため動作正常。順序固定は設計意図 (lib.rs:246-251) を保つため。
#[test]
fn lib_rs_drop_calls_delete_signal_after_watchdog_join() {
    let src = read("src/lib.rs");
    let drop_start = src
        .find("impl Drop for HyphaPost")
        .expect("HyphaPost Drop impl must exist");
    // Drop 本体は最大 4000 byte 以内に収まる想定 (B-027 段階 3-B 時点)。
    // UTF-8 char boundary を walk-back する (gui_wiring_test と同一規約)。
    let mut safe_end = (drop_start + 4000).min(src.len());
    while safe_end > drop_start && !src.is_char_boundary(safe_end) {
        safe_end -= 1;
    }
    let body = &src[drop_start..safe_end];

    let join_idx = body
        .find("watchdog_handle.take()")
        .expect("Drop must join watchdog (existing behavior)");
    let delete_idx = body
        .find("delete_signal(")
        .expect("Drop must call delete_signal (Group 2 統合点 #3)");
    assert!(
        join_idx < delete_idx,
        "delete_signal must be called AFTER watchdog join in HyphaPost::drop \
         (join={join_idx}, delete={delete_idx}); design 判断 lib.rs:246-251"
    );
    assert!(
        body.contains("[POST cleanup #3] delete_signal failed"),
        "Drop delete_signal failure must log warn only (Drop 内 panic = abort)"
    );
}

/// B-231: Watch 表示は pair 選択中なら Stale/NoPre で muted Δ を維持する。
/// PRE 明示 Bypassed だけは pair を維持したまま POST 絶対値へ戻す。
#[test]
fn editor_rs_watch_keeps_delta_grid_except_explicit_pre_bypass() {
    let src = read("src/editor.rs");
    let draw_post_start = src
        .find("fn draw_post(")
        .expect("draw_post function must exist");
    // window 7000 は editor_rs_calls_draw_proposals_block_from_draw_post と同位相
    // (B-049 Inactive 分岐 if-let 追加で draw_post body 増 / B-048 6000 → B-049 7000)。
    let mut safe_end = (draw_post_start + 7000).min(src.len());
    while safe_end > draw_post_start && !src.is_char_boundary(safe_end) {
        safe_end -= 1;
    }
    let window = &src[draw_post_start..safe_end];

    assert!(
        window.contains("pair_empty || d.mode == DeltaMode::Bypassed"),
        "draw_post Watch branch must reserve POST absolute fallback for pair-empty or explicit PRE bypass"
    );
    assert!(
        window.contains("draw_delta_grid(ui, d, COL_MUTED, false);"),
        "draw_post Watch branch must keep muted delta grid for paired transient Stale/NoPre"
    );
    assert!(
        src.contains("raw_d.mode == DeltaMode::Bypassed"),
        "draw_post display selection must preserve explicit PRE bypass as a distinct mode"
    );
    assert!(
        !window.contains("DeltaMode::Stale | DeltaMode::NoPre =>"),
        "Stale/NoPre must not be grouped into POST-absolute fallback"
    );
}

/// B-049 / G-115-247: SignalState::Inactive 分岐に Last Known Good を追加。
/// editor.rs の SignalState::Inactive 分岐内で以下 3 パターンが共存することを保証:
/// - `if let Some(snap) = &d.last_active` (凍結値経路)
/// - `draw_delta_grid_frozen(ui, snap, false)` (B-048 関数再利用)
/// - else 分岐に `draw_inactive_grid(ui)` (既存 fallback / 初回起動)
#[test]
fn editor_rs_inactive_branch_has_last_known_good_and_fallback() {
    let src = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("src")
            .join("editor.rs"),
    )
    .expect("read editor.rs");

    // Inactive arm の開始位置から、次の arm (SignalState::Active) までを抽出。
    let inactive_idx = src
        .find("SignalState::Inactive =>")
        .expect("SignalState::Inactive arm not found");
    let after = &src[inactive_idx..];
    let end_marker = after
        .find("SignalState::Active =>")
        .expect("SignalState::Active arm must follow Inactive arm");
    let block = &after[..end_marker];

    assert!(
        block.contains("if let Some(snap) = &d.last_active"),
        "B-049: SignalState::Inactive arm must contain `if let Some(snap) = &d.last_active`"
    );
    assert!(
        block.contains("draw_delta_grid_frozen(ui, snap, false)"),
        "B-049: SignalState::Inactive arm must call draw_delta_grid_frozen(ui, snap, false)"
    );
    assert!(
        block.contains("draw_inactive_grid(ui)"),
        "B-049: SignalState::Inactive arm must keep draw_inactive_grid(ui) as fallback"
    );
}

/// B-049 不変条件 #1: SignalState::Bypassed 分岐は完全不変。
/// Bypassed arm 開始から次の arm (SignalState::Inactive) までの範囲内に
/// `last_active` / `draw_delta_grid_frozen` 参照が無いことを確認する。
#[test]
fn editor_rs_bypassed_branch_unchanged_no_last_active_ref() {
    let src = std::fs::read_to_string(
        std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
            .join("src")
            .join("editor.rs"),
    )
    .expect("read editor.rs");

    let draw_post_idx = src.find("fn draw_post(").expect("draw_post not found");
    let draw_post = &src[draw_post_idx..];
    let bypassed_idx = draw_post
        .find("SignalState::Bypassed =>")
        .expect("SignalState::Bypassed arm not found in draw_post");
    let after = &draw_post[bypassed_idx..];
    // Bypassed arm の境界 = 次 arm `SignalState::Inactive =>` までを Bypassed 範囲とみなす
    let end_marker = after
        .find("SignalState::Inactive =>")
        .expect("SignalState::Inactive arm must follow Bypassed arm");
    let block = &after[..end_marker];

    assert!(
        !block.contains("last_active"),
        "B-049 invariant #1: SignalState::Bypassed arm must NOT reference last_active"
    );
    assert!(
        !block.contains("draw_delta_grid_frozen"),
        "B-049 invariant #1: SignalState::Bypassed arm must NOT call draw_delta_grid_frozen"
    );
}

// ── W-280 / G-115-248: 再生中 pair 変更 block の配線回帰テスト (B-12) ─────────

/// W-280 B-12 (iii): Audio Thread `process()` が `transport.playing` を
/// `is_playing` AtomicBool に store する経路を固定する。
/// 配線が落ちると GUI が「再生中」を検出できず block が解除される。
#[test]
fn lib_rs_process_stores_transport_playing_to_is_playing_atomic() {
    let src = read("src/lib.rs");
    assert!(
        src.contains("self.is_playing.store(playing"),
        "lib.rs process() must store transport.playing into is_playing AtomicBool (W-280 B-4)"
    );
    assert!(
        src.contains("is_playing: Arc<AtomicBool>"),
        "lib.rs HyphaPost must declare `is_playing: Arc<AtomicBool>` field (W-280 B-1)"
    );
}

/// W-280 B-12 (ii): GUI Thread `update` closure 入口で `is_playing` を
/// snapshot する経路を固定する。配線が落ちると frame 毎の load が消える。
#[test]
fn editor_rs_update_closure_loads_is_playing() {
    let src = read("src/editor.rs");
    assert!(
        src.contains("state.is_playing.load(Ordering::Relaxed)"),
        "editor.rs update closure must snapshot is_playing at frame entry (W-280 B-7)"
    );
    assert!(
        src.contains("pub is_playing: Arc<AtomicBool>"),
        "editor.rs PostEditorArgs / PostEditorState must declare `is_playing` field (W-280 B-5/B-6)"
    );
}

/// W-280 B-12 (i) + B-115: draw_pair_pre_name_field / draw_pair_pre_combo の PRE 候補行ループが
/// `add_enabled_ui(!pair_locked` で囲まれ、pair_locked = pair_lock_active(is_playing, live)
/// （playing かつ live）で算出されることを固定する。配線が落ちると実再生中の pair 変更 block が
/// 解除され、live 軸が落ちると凍結 playing で false-release する。
#[test]
fn editor_rs_pair_widgets_wrapped_in_add_enabled_ui_not_is_playing() {
    let src = read("src/editor.rs");
    let n = src.matches("add_enabled_ui(!pair_locked").count();
    assert!(
        n >= 2,
        "editor.rs must wrap pair widgets with `add_enabled_ui(!pair_locked` at least 2 times \
         (W-280 B-9 draw_pair_pre_name_field + B-10 draw_pair_pre_combo PRE candidates), got {}",
        n
    );
    // B-115: pair_locked は playing かつ live（heartbeat 鮮度）で算出する。live 軸が落ちると
    // 凍結 playing で false-release / signal_state を live の代用にしない。
    assert!(
        src.contains("pair_lock_active(is_playing, live)"),
        "editor.rs must compute pair_locked = pair_lock_active(is_playing, live) (B-115 live 軸配線)"
    );
    assert!(
        src.contains("state.liveness.is_live()"),
        "editor.rs update closure must read evaluator is_live() at frame entry (B-118 単一鮮度源)"
    );
    assert!(
        src.contains("pub liveness: Arc<LivenessEvaluator>"),
        "editor.rs PostEditorArgs / PostEditorState must declare `liveness` evaluator field (B-118)"
    );
    // PAIR_LOCKED_TOOLTIP const が存在し、tooltip 文言が Daisuke 確定 (判断 4) と一致する。
    assert!(
        src.contains(
            r#"const PAIR_LOCKED_TOOLTIP: &str = "Pair selection is locked during playback""#
        ),
        "editor.rs must declare PAIR_LOCKED_TOOLTIP with Daisuke-confirmed string (W-280 B-11)"
    );
    // ComboBox 全体を add_enabled_ui で囲っていないこと (判断 2 / All Stop / All Keep 保護)。
    // ComboBox::from_id_salt の直前行に add_enabled_ui がないことの簡易チェック。
    let combo_idx = src
        .find("ComboBox::from_id_salt(\"hypha_post_pair_pre_dropdown\")")
        .expect("draw_pair_pre_combo ComboBox::from_id_salt entry missing");
    // 直前 300 byte に add_enabled_ui が含まれていないこと (判断 2)。
    let before_start = combo_idx.saturating_sub(300);
    let before_window = &src[before_start..combo_idx];
    assert!(
        !before_window.contains("add_enabled_ui"),
        "W-280 invariant: ComboBox 全体は add_enabled_ui で囲ってはならない \
         (判断 2 / All Stop / All Keep 機能保護)"
    );
}

// ── W-282 / G-115-250: pair 解放時 Δ 表示完全リセット 配線回帰テスト (C-3) ────

/// W-282 C-3: editor.rs TextEdit Enter `sanitized.is_empty()` 経路 (B-1) で
/// `state.delta.lock()` reset が走る配線を invariant 化。配線が落ちると手動
/// 空文字クリア時に B-048 LKG が古い Δ 凍結値を残し続ける regression。
#[test]
fn editor_rs_text_edit_clear_pair_resets_delta_result() {
    let src = read("src/editor.rs");
    // sanitized.is_empty() 経路 (TextEdit) の write 直後に state.delta.lock() reset。
    // `if new_claimed_at == 0.0 {` から始まる block が `state.delta.lock()` +
    // `*d = DeltaResult::default();` を含むこと。
    let text_edit_idx = src
        .find("let sanitized = sanitize_name(&state.pair_pre_name_edit_buffer);")
        .expect("draw_pair_pre_name_field sanitize entry missing");
    // 当該 sanitize から 1500 byte 以内 (W-281 B-2 + W-282 B-1 の write block 範囲 /
    // Japanese 注釈が UTF-8 で 1 char=3 byte となるため余裕を持つ)。
    let window = &src[text_edit_idx..text_edit_idx + 1500.min(src.len() - text_edit_idx)];
    assert!(
        window.contains("if new_claimed_at == 0.0"),
        "W-282 B-1: TextEdit 分岐に `if new_claimed_at == 0.0` 判定がない"
    );
    assert!(
        window.contains("state.delta.lock()") && window.contains("DeltaResult::default()"),
        "W-282 B-1: TextEdit sanitized.is_empty() 経路に state.delta = DeltaResult::default() の reset がない"
    );
}

/// W-282 C-3: editor.rs ComboBox PRE 候補 click `new_name.is_empty()` 経路 (B-2) で
/// `state.delta.lock()` reset が走る配線を invariant 化。
#[test]
fn editor_rs_combobox_clear_pair_resets_delta_result() {
    let src = read("src/editor.rs");
    let combo_idx = src
        .find("let new_name = cand.name.clone().unwrap_or_default();")
        .expect("draw_pair_pre_combo new_name entry missing");
    let window = &src[combo_idx..combo_idx + 1200.min(src.len() - combo_idx)];
    assert!(
        window.contains("if new_claimed_at == 0.0"),
        "W-282 B-2: ComboBox 分岐に `if new_claimed_at == 0.0` 判定がない"
    );
    assert!(
        window.contains("state.delta.lock()") && window.contains("DeltaResult::default()"),
        "W-282 B-2: ComboBox new_name.is_empty() 経路に state.delta = DeltaResult::default() の reset がない"
    );
}

/// W-282 C-3: io_thread_post.rs W-281 C-4 release block (A-1) で
/// `delta_result.lock()` + `DeltaResult::default()` reset が走る配線を invariant 化。
#[test]
fn io_thread_post_release_block_clears_delta_result() {
    let src = fs::read_to_string(
        crate_root()
            .parent()
            .expect("hypha_post -> crates parent")
            .join("kirin_measure/src/io_thread_post.rs"),
    )
    .expect("read io_thread_post.rs");
    // release 経路の起点 = pair_release_notice.write() = Some("Released ...")
    let release_idx = src
        .find(r#"*n = Some("Released (paired elsewhere)".to_string());"#)
        .expect("W-281 C-4 release marker not found");
    // 当該 release write から 1000 byte 以内に delta_result.lock() + DeltaResult::default()
    // (UTF-8 Japanese 注釈 byte 膨張を考慮)。
    let window = &src[release_idx..release_idx + 1000.min(src.len() - release_idx)];
    assert!(
        window.contains("delta_result.lock()") && window.contains("DeltaResult::default()"),
        "W-282 A-1: IO Thread C-4 release block に delta_result = DeltaResult::default() の reset がない"
    );
}

// ── W-283 / G-115-251: pair_pre_name="" 時 GUI draw 強制 absolute + Keep gate 配線テスト
// (W-282 で IO Thread の delta_result clear を実装したが、次 tick の pass-through Δ
// 再計算で last_active が復活する根本問題が残った。GUI draw 分岐で pair_pre_name=""
// を直接判定して構造的に解決する。配線が落ちると pair 解放後も Δ 凍結値が残る regression)。

/// W-283 W-2: editor.rs SignalState::Active + !recording 分岐内で
/// `if pair_empty || d.mode == DeltaMode::Bypassed {`
/// → `draw_watch_absolute_grid(ui, m, false);` の強制経路が存在し、かつその true 枝に
/// `draw_delta_grid_frozen` (B-048 LKG 凍結経路) が含まれないことを invariant 化。
#[test]
fn editor_rs_pair_empty_draws_watch_absolute_not_delta_frozen() {
    let src = read("src/editor.rs");
    let anchor_idx = src
        .find("if pair_empty || d.mode == DeltaMode::Bypassed {")
        .expect(
            "W-283 W-2: pair-empty/PRE-bypassed absolute anchor not found in draw_post Active arm",
        );
    // 真枝 (true branch) のみを抽出: anchor から最初の `} else {` まで。
    let from_anchor = &src[anchor_idx..];
    let else_offset = from_anchor
        .find("} else {")
        .expect("W-283 W-2: matching `} else {` not found after absolute anchor");
    let true_branch = &from_anchor[..else_offset];
    assert!(
        true_branch.contains("draw_watch_absolute_grid(ui, m, false)"),
        "W-283 W-2: pair_empty/PRE-bypassed 真枝に draw_watch_absolute_grid(ui, m, false) が無い (強制経路欠落)"
    );
    assert!(
        !true_branch.contains("draw_delta_grid_frozen"),
        "W-283 W-2: pair_empty=true 真枝に draw_delta_grid_frozen が混入 (B-048 LKG 凍結経路混入 regression)"
    );
    assert!(
        !true_branch.contains("draw_delta_grid("),
        "W-283 W-2: pair_empty=true 真枝に draw_delta_grid (Active/Stale 表示) が混入 (Δ 表示残り regression)"
    );
}

/// W-283 W-3: editor.rs SignalState::Active + !recording 分岐内で
/// `if !pair_empty { draw_button_row(ui, false, ...) }` の Keep ボタン gate
/// 配線が残っていることを invariant 化。pair_pre_name="" で Keep ボタンが
/// 表示されると pair 未設定 trigger_keep が走る regression を防ぐ。
#[test]
fn editor_rs_keep_button_gated_when_pair_empty() {
    let src = read("src/editor.rs");
    let anchor_idx = src
        .find("if !pair_empty {")
        .expect("W-283 W-3: `if !pair_empty {` anchor not found in draw_post Active arm");
    // anchor から 300 byte 以内 (gate block 本体 + 余裕)。
    // UTF-8 char boundary walk-back (前後のコメント変動で byte 300 が
    // multi-byte char の途中に当たる場合に panic 回避)。
    let mut safe_end = (anchor_idx + 300).min(src.len());
    while safe_end > anchor_idx && !src.is_char_boundary(safe_end) {
        safe_end -= 1;
    }
    let window = &src[anchor_idx..safe_end];
    assert!(
        window.contains("draw_button_row(ui, false"),
        "W-283 W-3: `if !pair_empty {{` gate に draw_button_row(ui, false, ...) が含まれない (Keep gate 欠落)"
    );
}

/// POST Record 表示は左列=Δ 3項目 / 右列=POST絶対値 3項目。
#[test]
fn editor_rs_record_grid_keeps_right_absolute_values() {
    let src = read("src/editor.rs");
    let anchor = src
        .find("fn draw_record_section(")
        .expect("draw_record_section must exist");
    let mut safe_end = (anchor + 4500).min(src.len());
    while safe_end > anchor && !src.is_char_boundary(safe_end) {
        safe_end -= 1;
    }
    let block = &src[anchor..safe_end];

    for label in ["\"ΔLUFS\"", "\"ΔTP\"", "\"ΔCrest\""] {
        assert!(
            block.contains(label),
            "Record left Δ label missing: {label}"
        );
    }
    for label in ["\"LUFS-M\"", "\"TP\"", "\"Crest\""] {
        assert!(
            block.contains(label),
            "Record right absolute label missing: {label}"
        );
    }
    assert!(
        !block.contains("\"ΔPSR\"") && !block.contains("\"ΔN\"") && !block.contains("\"ΔSharp\""),
        "Record right column must not be consumed by Phase D delta labels"
    );
}

/// W-285 / G-115-253: editor.rs draw_pair_pre_combo dropdown loop で PRE name=""
/// (空文字 / 旧 schema 不在 None) の候補が gate されることを invariant 化。
/// dropdown 表示文字に UUID8 fallback が含まれるため、name="" の候補を残すと
/// Daisuke が UUID8 文字を見て pair_pre_name に手入力 → pair filter 永久不一致 NoPre
#[test]
fn editor_rs_dropdown_excludes_empty_name_pre() {
    let src = read("src/editor.rs");
    let anchor_idx = src
        .find("let named_candidates: Vec<_>")
        .expect("W-285: named_candidates filter anchor not found");
    // anchor から 1000 byte 以内に gate (UTF-8 Japanese 注釈で gate 自体は ~750 byte 先)。
    let window_end = (anchor_idx + 1500).min(src.len());
    // UTF-8 char boundary walk-back
    let mut safe_end = window_end;
    while safe_end > anchor_idx && !src.is_char_boundary(safe_end) {
        safe_end -= 1;
    }
    let window = &src[anchor_idx..safe_end];
    assert!(
        window.contains("!cand.name.as_deref().unwrap_or(\"\").is_empty()"),
        "W-285 G-115-253: dropdown loop 前に PRE name=\"\" 候補を除外する filter が無い (UUID8 fallback 誤導 regression)"
    );
    assert!(
        window.contains("for cand in named_candidates"),
        "W-285 G-115-253: dropdown loop must render the filtered named candidate set"
    );
}

#[test]
fn editor_rs_pair_dropdown_distinguishes_keep_from_pair_choices() {
    let src = read("src/editor.rs");
    let combo_fn_idx = src
        .find("fn draw_pair_pre_combo(")
        .expect("draw_pair_pre_combo must exist");
    let mut safe_end = (combo_fn_idx + 12000).min(src.len());
    while safe_end > combo_fn_idx && !src.is_char_boundary(safe_end) {
        safe_end -= 1;
    }
    let body = &src[combo_fn_idx..safe_end];

    assert!(
        body.contains("all_keep_dropdown_label(n_ready)"),
        "All Keep row must use a label that names ready POST count"
    );
    assert!(
        body.contains("Pair choices (not Keep targets)"),
        "pair candidate section must be visually separated from Keep actions"
    );
    assert!(
        body.contains("All Stop: recording POSTs"),
        "recording action must be labeled as POST-recording Stop, not a pair candidate"
    );
    assert!(
        body.contains("No pair choices"),
        "empty candidate state must not reuse the ambiguous `No candidates` text"
    );
    assert!(
        body.contains("let named_candidates: Vec<_>"),
        "empty-name PRE candidates must not leave the pair-choice section blank"
    );
    assert!(
        src.contains("format!(\"{prefix}: {name_part}\")") && !src.contains("#{id_prefix}"),
        "PRE candidate rows must carry explicit keepability status without exposing instance_id"
    );
    assert!(
        src.contains("CandidateKeepStatus::KeepReady")
            && src.contains("CandidateKeepStatus::InUseByOther")
            && src.contains("CandidateKeepStatus::Available")
            && src.contains("CandidateKeepStatus::DuplicateName"),
        "pair dropdown must distinguish current ready, already used, duplicate, and available PRE choices"
    );
    assert!(
        body.contains("CandidateKeepStatus::InUseByOther | CandidateKeepStatus::DuplicateName"),
        "already-used and duplicate PRE choices must not be accidentally selectable"
    );
}

/// W-284 / G-115-252: io_thread_post.rs main loop の self_check_pair_claim 発火条件
/// に `!record_sm.is_recording()` gate が含まれることを invariant 化。Record 中の
/// self_check release が pair_pre_name="" / delta_result clear / pair_label 切替で
/// Record 継続を破綻させる regression を防ぐ (Daisuke 2026-05-17 報告)。
#[test]
fn io_thread_post_self_check_gated_when_recording() {
    let src = fs::read_to_string(
        crate_root()
            .parent()
            .expect("hypha_post -> crates parent")
            .join("kirin_measure/src/io_thread_post.rs"),
    )
    .expect("read io_thread_post.rs");
    // W-281 C-3 self_check 発火条件 block (W-284 で Record gate 追加)。
    // anchor = `&& tick_now.duration_since(last_self_check_at)` (条件式末尾の throttle 句)。
    let anchor_idx = src
        .find("&& tick_now.duration_since(last_self_check_at)")
        .expect("W-281 C-3 self_check throttle anchor not found");
    // anchor から 200 byte 前後 (条件式全体を含む) で gate を確認。
    let start = anchor_idx.saturating_sub(300);
    let end = (anchor_idx + 200).min(src.len());
    // UTF-8 char boundary walk-back (Japanese 注釈の途中で切れないように)。
    let mut safe_start = start;
    while safe_start < src.len() && !src.is_char_boundary(safe_start) {
        safe_start += 1;
    }
    let mut safe_end = end;
    while safe_end > 0 && !src.is_char_boundary(safe_end) {
        safe_end -= 1;
    }
    let window = &src[safe_start..safe_end];
    assert!(
        window.contains("!record_sm.is_recording()"),
        "W-284 G-115-252: self_check 発火条件 block に `!record_sm.is_recording()` gate が無い (Record 中 release で pair 破綻する regression)"
    );
}
