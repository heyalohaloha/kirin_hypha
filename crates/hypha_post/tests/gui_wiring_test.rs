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
    // 5000 byte 上限は B-027 段階 2 で Name 入力欄追加に伴う body 増を吸収
    // (元 4000 では draw_proposals_block 呼出 byte index がぎりぎり外に出る)。
    // window 5000 → 6000: B-048 / G-115-245 NoPre 分岐に Last Known Good if-let
    // 追加 (draw_post body 5587 byte 計測 / 6000 確保)。
    // window 6000 → 7000: B-049 / G-115-247 Inactive 分岐に if-let 追加で
    // draw_proposals_block 呼出が rel 5967 byte → 文字列終端 6003 byte で
    // 6000 window から 3 byte はみ出す (contains 不成立)。1000 byte 余裕を確保。
    // UTF-8 char boundary を跨がないよう boundary まで walk-back する
    // (Japanese comments が raw byte index 上で multi-byte char の途中に当たる)。
    let mut safe_end = (draw_post_start + 7000).min(src.len());
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
/// (B-023 段階 4 + G-115-64 / 番人 #137 確定).
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
    // draw_pair_pre_combo 内に push_id ラップが含まれていること。
    // walk-back で UTF-8 char boundary を跨がない上限を取る。
    // window 5000 → 6000: §4-5 Step 1 で click handler 内に project_hash /
    // daw_session_id の lazy-read snapshot 追加 (B-022 段階 1 instance_id 同位相 /
    // §4-4 R-9 主因 a / b 是正) で関数が legitimately さらに拡大した。push_id
    // invariant 自体は維持。
    // window 6000 → 7500: §4-5 Step 4 で click handler 末尾に fs::metadata
    // 存在確認 log (broadcast 寿命診断) を追加 (削除経路特定不能時の fallback /
    // ms 単位削除の境界切り分け目的)。push_id invariant 自体は維持。
    // window 7500 → 9500: α-7' All Stop で recording 時の All Stop 行 click handler
    // を ComboBox dropdown 先頭に追加 (Keep と完全対称形 / 琥珀色)。push_id invariant
    // 自体は維持。
    let mut safe_end = (combo_fn_idx + 9500).min(src.len());
    while safe_end > combo_fn_idx && !src.is_char_boundary(safe_end) {
        safe_end -= 1;
    }
    let body = &src[combo_fn_idx..safe_end];
    assert!(
        body.contains("ui.push_id(cand.instance_id.as_str()"),
        "draw_pair_pre_combo must wrap each entry with `ui.push_id(cand.instance_id.as_str(), ...)` (B-027 段階 3 (a) 仮説 2)"
    );
    // selectable_label 呼出は push_id クロージャ内に居ること (順序確認)。
    let push_idx = body
        .find("ui.push_id(cand.instance_id.as_str()")
        .expect("push_id wrap must precede selectable_label");
    let select_idx = body
        .find("ui.selectable_label(false, label_text)")
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
/// 構造障害が起きる (DEV INBOX §9-3 / S117 判断 2 (P))。delete_broadcast は冪等
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

/// B-048 / G-115-245 Last Known Good: editor.rs `DeltaMode::NoPre` 分岐に
/// 以下 2 パターンが共存することを source-string で保証する。
///
/// - `if let Some(snap) = &d.last_active` (凍結値表示経路 / B-048 主機能)
/// - `draw_watch_absolute_grid(ui, m)` (初回 PRE 検出前フォールバック / 既存挙動)
///
/// どちらか欠けるとサイレント機能停止になる:
/// - 前者欠落 = 常に絶対値経路 / Last Known Good 失効
/// - 後者欠落 = 初回 PRE 検出前に panic or 描画失敗
#[test]
fn editor_rs_no_pre_branch_has_last_known_good_and_fallback() {
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
        window.contains("if let Some(snap) = &d.last_active"),
        "draw_post NoPre branch must contain `if let Some(snap) = &d.last_active` \
         (B-048 / G-115-245 Last Known Good display path)"
    );
    assert!(
        window.contains("draw_watch_absolute_grid(ui, m)"),
        "draw_post NoPre branch must contain `draw_watch_absolute_grid(ui, m)` \
         (B-048 fallback for initial PRE detection / 既存挙動)"
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

    let bypassed_idx = src
        .find("SignalState::Bypassed =>")
        .expect("SignalState::Bypassed arm not found");
    let after = &src[bypassed_idx..];
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

/// W-280 B-12 (i): draw_pair_pre_name_field / draw_pair_pre_combo の
/// PRE 候補行ループが `add_enabled_ui(!is_playing` で囲まれていることを固定する。
/// 配線が落ちると再生中の pair 変更 block が解除される。
#[test]
fn editor_rs_pair_widgets_wrapped_in_add_enabled_ui_not_is_playing() {
    let src = read("src/editor.rs");
    let n = src.matches("add_enabled_ui(!is_playing").count();
    assert!(
        n >= 2,
        "editor.rs must wrap pair widgets with `add_enabled_ui(!is_playing` at least 2 times \
         (W-280 B-9 draw_pair_pre_name_field + B-10 draw_pair_pre_combo PRE candidates), got {}",
        n
    );
    // PAIR_LOCKED_TOOLTIP const が存在し、tooltip 文言が Daisuke 確定 (判断 4) と一致する。
    assert!(
        src.contains(r#"const PAIR_LOCKED_TOOLTIP: &str = "Pair selection is locked during playback""#),
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
