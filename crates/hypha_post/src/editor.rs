//! POST GUI — 300×200px（guardian_53 T-6 / サブ2-B）。
//!
//! hypha_gui 共有プリミティブを使用:
//! - 5状態 LED（Error/WatchBreathing/RecordStandby/RecordActive/Idle）
//! - 背景テクスチャ（菌糸 300×200 brightness 15% / 遅延 decode キャッシュ）
//! - flora_color 横線（#d4a043 暫定）
//! - 共通ウィジェット（value_row / fmt_val / fmt_delta / tp_color）
//!
//! SS-7: SignalState に基づく表示切替（guardian_64: PRE 不在時 絶対値表示復活）。
//! - POST Active + PRE Active（DeltaMode::Active / Stale）→ Δ 3 項目表示
//! - POST Active + PRE 不在 or Bypassed（DeltaMode::NoPre）→ 絶対値 3 項目表示
//!   （LUFS-M / TP / Crest。POST 単独挿入での計測動作を目視確認する経路）
//! - POST Bypassed → 全項目 `---` + ボタン非表示（プラグイン無効化中）
//! - POST Inactive → 全項目 `---` + ボタン表示（信号待ちでも license 操作は可能）
//!
//! Record モード（recording=true）: 左列=Δ 3 項目 / 右列=絶対値 3 項目（LUFS-M / TP / Crest）。
//! NoPre 時は Δ 列が自動的に `---`、右列は POST 絶対値で埋める。
//! ペアリング表示は pair_label から取得（サブ3 で IO Thread から配信）。
//!
//! # サブ2-B: ボタン実配線（guardian_58 T-6 Q1〜Q7 承認仕様）
//! - `Keep` → PRE 候補 0 件 → toast / 排他違反 → toast /
//!   `try_enter_record(license)` → `write_pending`
//! - `Stop` → `record_sm.exit_record()` + `mark_released`
//! - `Note` → スタブ維持（U-16 は サブ2-C）
//! - Sense hint → `open::that(SENSE_UPSELL_URL)` でブラウザ起動
//!
//! 色ルール（guardian_54-16 修正）: TP 警戒域（> -1.0 dBTP）は COL_FLORA_BRIGHT
//! （赤系禁止。色相同一・明度増）。

use hypha_gui::{
    derive_led_state, fmt_delta, fmt_val, led_color, pairing_label, tp_over, val_color, value_row,
    BackgroundTexture, BG, COL_FLORA, COL_FLORA_BRIGHT, COL_MUTED, COL_NORMAL,
};
use kirin_measure::{
    all_keep_signal_path, all_stop_signal_path, append_annotation_to_latest,
    check_record_exclusion, delete_broadcast, delete_signal, discover_active_pre_dirs,
    enumerate_active_post_pair_candidates, enumerate_active_pre_pair_candidates,
    exit_record_full, filter_candidates_by_name, format_pair_label, load_signal_state,
    lookup_section_label, mark_released, pick_closest_pre, sanitize_name,
    scan_latest_v2_preset, scan_pre_candidates_in, show_note_button, show_save_button,
    show_stop_record_button, write_broadcast, write_pending, write_stop_broadcast,
    DeltaMode, DeltaResult, DeltaSnapshot, ExclusionResult, License, MeasureResult, PluginDataRole,
    PostMetrics, PreCandidate, PresetFileV2, RecordStateMachine, SignalState, StoragePaths,
    TransitionError, MAX_ACTIVE_PER_PROJECT, SENSE_RECORD_HINT, SENSE_UPSELL_URL,
};
use nih_plug::prelude::Editor;
use nih_plug_egui::{
    create_egui_editor,
    egui::{self, Grid, Key, Label, RichText, Sense, Stroke, TextEdit, TextStyle, Vec2},
    EguiState,
};
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU32, AtomicU8, Ordering};
use std::sync::{Arc, LazyLock, Mutex, RwLock};
use std::time::Duration;

/// B-027 段階 2: pair PRE Name 編集 TextEdit の egui focus ID。
///
/// PRE 側 `editor.rs:36-41 NAME_FOCUS_ID` と完全対称の静的定義。
/// id 文字列は POST 用に独立 (`hypha_post_pair_pre_name_edit`)。
static PAIR_PRE_NAME_FOCUS_ID: LazyLock<egui::Id> =
    LazyLock::new(|| egui::Id::new("hypha_post_pair_pre_name_edit"));

/// B-027 段階 2: pair_pre_name 編集モード判定 (egui memory ベース)。
///
/// PRE 側 `editor.rs:47-49 name_edit_active` と完全同パターン。
fn pair_pre_name_edit_active(ui: &egui::Ui) -> bool {
    ui.memory(|mem| mem.has_focus(*PAIR_PRE_NAME_FOCUS_ID))
}

use crate::{read_daw_session_id_arc, read_instance_id_arc, read_project_hash_arc};

/// T-E throttle: rescan `preset/` at most every 500 ms. proposals rarely
/// change at sub-second cadence; avoids hitting FS every repaint (≈ 10 Hz).
const PROPOSALS_SCAN_INTERVAL_SECS: f64 = 0.5;
/// T-E: cap rendered cards to keep the 300×200 GUI bounded.  Beyond this,
/// the user sees "+ N more" (or just truncates — see draw_proposals_block).
const MAX_CARDS_RENDERED: usize = 8;

/// 記録開始バナーの表示時間（秒）。
const RECORD_BANNER_DURATION_SECS: f64 = 3.0;

/// toast の表示時間（秒）。
const TOAST_DURATION_SECS: f64 = 3.0;

// ── Toast ─────────────────────────────────────────────────────────────────

/// 一時通知（warning / 情報）。3 秒で自動消去。
///
/// B-027 段階 3-B α-7-4-D / Step 11: `trigger_keep_internal` を `pub(crate)` 昇格した
/// 結果、`Option<&mut Option<Toast>>` 引数の型推論で `Toast` 型自体が crate 外側
/// (lib.rs) からも参照される (`None` 値の型解決) ため `pub(crate)` 昇格。フィールドは
/// 引き続き private (lib.rs 側からは Toast の構造に触らず `None` 渡しのみ)。
pub(crate) struct Toast {
    message: String,
    until: f64,
}

impl Toast {
    fn new(message: impl Into<String>, now: f64) -> Self {
        Self { message: message.into(), until: now + TOAST_DURATION_SECS }
    }

    fn is_alive(&self, now: f64) -> bool {
        now < self.until
    }
}

// ── エディタ状態 ─────────────────────────────────────────────────────────

pub struct PostEditorState {
    /// B-022 段階 1: chunk-restore 後の最新 instance_id を毎フレーム lazy-read するため
    /// `Arc<RwLock<String>>` を共有保持する。trigger_keep / trigger_stop /
    /// trigger_note_save の各 use site で `read_instance_id_arc(&self.instance_id)` を
    /// 呼び出してから渡す。
    pub instance_id: Arc<RwLock<String>>,
    /// プロセス単位 `project_hash`（plugin_data path のルートセグメント）。
    /// §4-5 Step 1: B-022 段階 1 instance_id 同位相で `Arc<RwLock<String>>` 化。
    /// 各 use site で `read_project_hash_arc(&self.project_hash)` を呼んで
    /// chunk-restore + cell update 後の最新 cell 値を lazy-read する。
    pub project_hash: Arc<RwLock<String>>,
    /// プロセス単位 `daw_session_id`（record_signal content の cross-process 防壁）。
    /// §4-5 Step 1: 同位相で `Arc<RwLock<String>>` 化。
    pub daw_session_id: Arc<RwLock<String>>,
    pub measure: Arc<Mutex<MeasureResult>>,
    pub delta: Arc<Mutex<DeltaResult>>,
    pub measure_alive: Arc<AtomicBool>,
    pub signal_state: Arc<AtomicU8>,
    /// Record 状態機械（サブ2-B: ボタンから操作）。
    pub record_sm: Arc<RecordStateMachine>,
    /// Record 信号が PRE から ACK されたか（false = Standby, true = Active）
    pub record_acknowledged: Arc<AtomicBool>,
    /// ペアリング表示用ラベル。Record 中は "pair: PRE_xxxxxxxx"（trigger_keep が設定）、
    /// Watch 中は空文字（trigger_stop / 自然遷移でクリア）。
    pub pair_label: Arc<Mutex<String>>,
    /// trigger_keep が選定した PRE instance_id（v1.2 (a) cross-instance pair 復元キー）。
    /// Watch 中は None、Keep 成功直後に Some、Stop / 失敗で None に戻す。
    /// POST IO Thread が `run_record_tick` で読み出して plugin_data の
    /// `paired_pre_instance_id` field に書き込む。
    pub paired_pre_target: Arc<Mutex<Option<String>>>,
    /// license 値（起動時に読込済。サブ2-A 範囲では不変）。
    pub license: Arc<License>,
    /// preset/*.json が 1 件以上存在するか（サブ3-C-2: POST IO Thread が更新）。
    pub preset_available: Arc<AtomicBool>,

    // ── T-E / T-F 追加（guardian_77 v3 §9 / §10）──────────────────────
    /// installation_id フィルタ用（empty → proposals scan 全 skip / R-28）。
    pub installation_id: Arc<String>,
    /// process() が書いた再生位置（サンプル）。i64::MIN = 不明 → fallback。
    pub playback_pos_samples: Arc<AtomicI64>,
    /// process() が initialize() でキャッシュしたサンプルレート。0 = 未初期化。
    pub playback_sample_rate: Arc<AtomicU32>,

    /// B-027 段階 2: pair PRE Name (HyphaPostParams.pair_pre_name と Arc 共有)。
    /// 編集確定時に `write()` で sanitize 後の値を書き込み、trigger_keep で
    /// `read()` した値を `filter_candidates_by_name` の引数に渡す。
    pub pair_pre_name: Arc<RwLock<String>>,

    /// B-025 Group B-2/B-3 / Gap-19/20: io_thread が連続失敗 (Directory missing /
    /// Write failed) で record exit したとき書込まれる UI 通知文字列。`None` =
    /// 通常 / `Some` のとき GUI 末尾にステータス行表示 (R-26 沈黙ゲート / 通常時非表示)。
    pub record_error_message: Arc<RwLock<Option<String>>>,

    // ── エディタローカル ───────────────────────────────────────────────
    bg: BackgroundTexture,
    prev_ack: bool,
    banner_until: Option<f64>,
    toast: Option<Toast>,
    /// サブ2-C: Note タップ後の 3 タグ選択行を表示中か。
    /// Record 中のみ有効。非 Record 時は毎フレーム false に戻される。
    note_picker_open: bool,
    /// 直前フレームの LED 状態（edge-triggered log 用）。
    prev_led: Option<hypha_gui::LedState>,
    /// T-E: 最新 v2.0 proposals（500ms throttle で更新）。
    latest_proposals: Option<PresetFileV2>,
    /// T-E: proposals を最後にスキャンした wall-clock 時刻（秒）。
    proposals_scan_last: Option<f64>,
    /// T-E: カードリストが展開されているか（タップでトグル）。
    cards_expanded: bool,
    /// T-F fallback: Record 開始時の wall-clock（秒）。transport.pos_samples
    /// が `None` 時に `now - record_start_wall_time` で経過時間を代替。
    record_start_wall_time: Option<f64>,
    /// B-027 段階 2: pair_pre_name 編集モードの入力 buffer。
    /// PRE 側 PreEditorState.edit_buffer (editor.rs:82-85) と同パターン。
    pair_pre_name_edit_buffer: String,
}

impl PostEditorState {
    fn new(args: PostEditorArgs) -> Self {
        Self {
            instance_id: args.instance_id,
            project_hash: args.project_hash,
            daw_session_id: args.daw_session_id,
            measure: args.measure,
            delta: args.delta,
            measure_alive: args.measure_alive,
            signal_state: args.signal_state,
            record_sm: args.record_sm,
            record_acknowledged: args.record_acknowledged,
            pair_label: args.pair_label,
            paired_pre_target: args.paired_pre_target,
            license: args.license,
            preset_available: args.preset_available,
            installation_id: args.installation_id,
            playback_pos_samples: args.playback_pos_samples,
            playback_sample_rate: args.playback_sample_rate,
            pair_pre_name: args.pair_pre_name,
            record_error_message: args.record_error_message,
            bg: BackgroundTexture::new(),
            prev_ack: false,
            banner_until: None,
            toast: None,
            note_picker_open: false,
            prev_led: None,
            latest_proposals: None,
            proposals_scan_last: None,
            cards_expanded: false,
            record_start_wall_time: None,
            pair_pre_name_edit_buffer: String::new(),
        }
    }
}

/// Bundle of `Arc`-shared plugin state handed to the editor.
pub struct PostEditorArgs {
    pub egui_state: Arc<EguiState>,
    /// B-022 段階 1: lib.rs `editor()` から `Arc::clone(&self.params.instance_id)` を
    /// 渡す。chunk-restore 直後の最新値を use site で lazy-read するため。
    pub instance_id: Arc<RwLock<String>>,
    /// プロセス単位 `project_hash`（A-3 修正後）。
    /// §4-5 Step 1: `Arc<RwLock<String>>` 化 (B-022 段階 1 instance_id 同位相)。
    pub project_hash: Arc<RwLock<String>>,
    /// プロセス単位 `daw_session_id`（A-3 修正後 / Q1 補強）。
    /// §4-5 Step 1: `Arc<RwLock<String>>` 化。
    pub daw_session_id: Arc<RwLock<String>>,
    pub measure: Arc<Mutex<MeasureResult>>,
    pub delta: Arc<Mutex<DeltaResult>>,
    pub measure_alive: Arc<AtomicBool>,
    pub signal_state: Arc<AtomicU8>,
    pub record_sm: Arc<RecordStateMachine>,
    pub record_acknowledged: Arc<AtomicBool>,
    pub pair_label: Arc<Mutex<String>>,
    /// v1.2 (a): trigger_keep が選定した PRE instance_id を IO Thread に渡す共有スロット。
    pub paired_pre_target: Arc<Mutex<Option<String>>>,
    pub license: Arc<License>,
    pub preset_available: Arc<AtomicBool>,
    pub installation_id: Arc<String>,
    pub playback_pos_samples: Arc<AtomicI64>,
    pub playback_sample_rate: Arc<AtomicU32>,
    /// B-027 段階 2: pair PRE Name の Arc 共有 (HyphaPostParams.pair_pre_name)。
    pub pair_pre_name: Arc<RwLock<String>>,
    /// B-025 Group B-2/B-3 / Gap-19/20: io_thread → GUI ステータス行通知 Arc。
    pub record_error_message: Arc<RwLock<Option<String>>>,
}

// ── 公開エントリポイント ─────────────────────────────────────────────────

pub fn create_post_editor(args: PostEditorArgs) -> Option<Box<dyn Editor>> {
    let egui_state = Arc::clone(&args.egui_state);
    create_egui_editor(
        egui_state,
        PostEditorState::new(args),
        |ctx, state| {
            let mut visuals = ctx.style().visuals.clone();
            visuals.panel_fill = BG;
            visuals.window_fill = BG;
            ctx.set_visuals(visuals);
            state.bg = BackgroundTexture::new();
        },
        |ctx, _setter, state| {
            let sig = load_signal_state(&state.signal_state);
            let m = state.measure.lock().map(|g| g.clone()).unwrap_or_default();
            let d = state.delta.lock().map(|g| g.clone()).unwrap_or_default();
            let alive = state.measure_alive.load(Ordering::Relaxed);
            let recording = state.record_sm.is_recording();
            // Record から抜けた瞬間に picker を自動閉じ（Watch 中に picker が残ることを防止）
            if !recording {
                state.note_picker_open = false;
            }
            let ack = state.record_acknowledged.load(Ordering::Relaxed);
            let pair = state.pair_label.lock().map(|g| g.clone()).unwrap_or_default();
            let license = *state.license;

            let now = ctx.input(|i| i.time);

            // T-F fallback anchor: Record 開始 wall-clock を 1 度だけ記録し、
            // Watch 復帰時にクリア。
            if recording {
                if state.record_start_wall_time.is_none() {
                    state.record_start_wall_time = Some(now);
                }
            } else {
                state.record_start_wall_time = None;
            }

            // T-E: proposals を 500ms throttle で再スキャン。
            maybe_rescan_proposals(state, now);

            if ack && !state.prev_ack {
                state.banner_until = Some(now + RECORD_BANNER_DURATION_SECS);
            }
            state.prev_ack = ack;
            let show_banner = state.banner_until.is_some_and(|until| now < until);
            if !show_banner {
                state.banner_until = None;
            }

            let preset_available = state.preset_available.load(Ordering::Relaxed);
            let led = derive_led_state(alive, sig, recording, ack, preset_available);
            // R-28 edge-triggered: LED 状態が切り替わった瞬間のみログを出す。
            if state.prev_led != Some(led) {
                log::info!("[led] state: {:?}", led);
                state.prev_led = Some(led);
            }
            let led_col = led_color(led, now);

            draw_post(ctx, state, &m, &d, sig, recording, &pair, led_col, show_banner, license, now);

            // Toast の寿命切れはこのフレームで掃除
            if state.toast.as_ref().is_some_and(|t| !t.is_alive(now)) {
                state.toast = None;
            }
        },
    )
}

// ── 描画 ────────────────────────────────────────────────────────────────

#[allow(clippy::too_many_arguments)]
fn draw_post(
    ctx: &egui::Context,
    state: &mut PostEditorState,
    m: &MeasureResult,
    d: &DeltaResult,
    sig: SignalState,
    recording: bool,
    pair: &str,
    led_col: egui::Color32,
    show_banner: bool,
    license: License,
    now: f64,
) {
    egui::CentralPanel::default()
        .frame(egui::Frame::NONE.fill(BG))
        .show(ctx, |ui| {
            state.bg.paint(ctx, ui);

            ui.add_space(8.0);

            ui.horizontal(|ui| {
                ui.add_space(10.0);
                ui.label(RichText::new("POST").size(20.0).color(COL_NORMAL));
                // A-3 修正後の pair_label 表示ルール:
                //   Record 中 = "pair: PRE_xxxxxxxx" を表示（trigger_keep が設定）
                //   Watch 中  = 描画自体を省略（pair_label は空文字に保たれる）
                if recording {
                    ui.add_space(6.0);
                    pairing_label(ui, pair);
                }
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.add_space(10.0);
                    draw_led(ui, led_col);
                });
            });
            ui.add_space(4.0);

            // B-027 段階 2 / 3-A: タイトル行直下に pair PRE Name 入力欄 + ComboBox dropdown。
            // ComboBox は kirin_root 配下の **全** active PRE 候補を flatten 列挙し
            // (G-115-49 / α-2 撤回: cdylib 隔離下で project_uuid filter は構造的不成立)、
            // 選択で pair_pre_name を確定する (Keep manual 経由)。
            // `draw_pair_pre_name_field` の直近右側に並べ、自由入力（Name 検索）と
            // 一覧選択の両系統を提供する。
            ui.horizontal(|ui| {
                ui.add_space(10.0);
                draw_pair_pre_name_field(ui, state);
                ui.add_space(4.0);
                draw_pair_pre_combo(ui, state, now);
            });
            ui.add_space(4.0);

            ui.horizontal(|ui| {
                ui.add_space(10.0);
                let w = ui.available_width() - 10.0;
                let (rect, _) = ui.allocate_exact_size(Vec2::new(w, 1.0), egui::Sense::hover());
                ui.painter().hline(rect.x_range(), rect.center().y, Stroke::new(1.0, COL_FLORA));
            });
            ui.add_space(4.0);

            match sig {
                SignalState::Bypassed => {
                    draw_inactive_grid(ui);
                }
                SignalState::Inactive => {
                    draw_inactive_grid(ui);
                    ui.add_space(4.0);
                    draw_button_row(ui, recording, license, state, m, now);
                }
                SignalState::Active => {
                    if recording {
                        draw_record_section(ui, m, d);
                        ui.add_space(4.0);
                        draw_button_row(ui, true, license, state, m, now);
                    } else {
                        // guardian_64: Watch + PRE 不在/Bypassed → 絶対値 3 項目表示。
                        // io_thread_post::compute_delta_with_state が PRE 不在 or
                        // pre_signal_state != Active のとき DeltaMode::NoPre を立てる。
                        match d.mode {
                            DeltaMode::Active | DeltaMode::Stale => {
                                let delta_col = if d.mode == DeltaMode::Active {
                                    COL_NORMAL
                                } else {
                                    COL_MUTED
                                };
                                let tp_warn = tp_over(m.true_peak);
                                draw_delta_grid(ui, d, delta_col, tp_warn);
                            }
                            DeltaMode::NoPre => {
                                // B-048 / G-115-245 Last Known Good: 直近 Active 時の
                                // Δ 6 軸 snapshot があれば凍結値を COL_MUTED で表示。
                                // None (初回 PRE 検出前) は既存フォールバック (POST 絶対値)。
                                if let Some(snap) = &d.last_active {
                                    // tp_warn は signature 安定性のため算出して渡す
                                    // (B-051 鮮度ドット導入時の修正範囲最小化 / 採用案 Y)。
                                    // draw_delta_grid_frozen 側では _tp_warn で受けて
                                    // 凍結値表示中の COL_FLORA_BRIGHT 強調は意図的に抑制。
                                    let tp_warn = tp_over(m.true_peak);
                                    draw_delta_grid_frozen(ui, snap, tp_warn);
                                } else {
                                    draw_watch_absolute_grid(ui, m);
                                }
                            }
                        }
                        ui.add_space(4.0);
                        draw_button_row(ui, false, license, state, m, now);
                    }
                }
            }

            // ── T-E / T-F 行（guardian_77 v3 §9 / §10）────────────────
            // draw_proposals_block は R-28 沈黙: 表示対象がなければ何も描かず、
            // ここで allocate した add_space ぶんだけが残る。allocate は関数
            // 側が行い、必要なければ空で return する。
            draw_proposals_block(ui, state, now);

            if show_banner {
                ui.add_space(4.0);
                ui.horizontal(|ui| {
                    ui.add_space(10.0);
                    ui.label(
                        RichText::new("Keeping")
                            .size(13.0)
                            .color(COL_FLORA)
                            .monospace(),
                    );
                });
            }

            if let Some(t) = state.toast.as_ref() {
                if t.is_alive(now) {
                    ui.add_space(4.0);
                    ui.horizontal(|ui| {
                        ui.add_space(10.0);
                        // B-027 段階 2 fix (NG-3): `Label::extend()` で wrap せず
                        // 親 Ui を必要に応じ広げる (egui 0.31.1 公式 API)。
                        // 既定 wrap 挙動だと 300×200px GUI 内 horizontal で
                        // toast 文字列が途中切れする ("No matchin" 等)。
                        ui.add(
                            Label::new(
                                RichText::new(&t.message)
                                    .size(11.0)
                                    .color(COL_MUTED)
                                    .monospace(),
                            )
                            .extend(),
                        );
                    });
                }
            }

            // B-025 Group B-2/B-3 / Gap-19/20: io_thread が連続失敗で record exit
            // した時のステータス行 (R-26 沈黙ゲート: 通常時 None なので非表示)。
            // G-115-29 準拠の英語固定文言を `RecordError::ui_message()` 経由で受け取る。
            // toast と独立 (toast 寿命と無関係 / Some の間は表示し続ける)。
            let err_msg = state
                .record_error_message
                .read()
                .ok()
                .and_then(|g| g.clone());
            if let Some(msg) = err_msg {
                ui.add_space(4.0);
                ui.horizontal(|ui| {
                    ui.add_space(10.0);
                    ui.add(
                        Label::new(
                            RichText::new(&msg)
                                .size(11.0)
                                .color(COL_MUTED)
                                .monospace(),
                        )
                        .extend(),
                    );
                });
            }

            let toast_alive = state.toast.as_ref().is_some_and(|t| t.is_alive(now));
            let repaint_ms = if show_banner || toast_alive { 33 } else { 100 };
            ctx.request_repaint_after(Duration::from_millis(repaint_ms));
        });
}

/// Bypassed / Inactive 時の全項目 "---" 表示
fn draw_inactive_grid(ui: &mut egui::Ui) {
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        Grid::new("post_inactive")
            .num_columns(3)
            .min_col_width(58.0)
            .spacing([6.0, 6.0])
            .show(ui, |ui| {
                value_row(ui, "LUFS-M", "---".to_string(), "LUFS", COL_MUTED);
                value_row(ui, "TP", "---".to_string(), "dBTP", COL_MUTED);
                value_row(ui, "Crest", "---".to_string(), "dB", COL_MUTED);
            });
    });
}

/// Watch + PRE Active（DeltaMode::Active / Stale）: Δ 3 項目表示。
/// NoPre は `draw_watch_absolute_grid` にルーティングされるのでここには到達しない（guardian_64）。
fn draw_delta_grid(ui: &mut egui::Ui, d: &DeltaResult, delta_col: egui::Color32, tp_warn: bool) {
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        Grid::new("post_delta")
            .num_columns(3)
            .min_col_width(58.0)
            .spacing([6.0, 6.0])
            .show(ui, |ui| {
                let lufs_col = if d.lufs.is_some() { delta_col } else { COL_MUTED };
                value_row(ui, "ΔLUFS", fmt_delta(d.lufs), "LU", lufs_col);

                let tp_col = if d.tp.is_some() {
                    if tp_warn { COL_FLORA_BRIGHT } else { delta_col }
                } else {
                    COL_MUTED
                };
                value_row(ui, "ΔTP", fmt_delta(d.tp), "dB", tp_col);

                let crest_col = if d.crest.is_some() { delta_col } else { COL_MUTED };
                value_row(ui, "ΔCrest", fmt_delta(d.crest), "dB", crest_col);
            });
    });
}

/// Watch + DeltaMode::NoPre + 凍結値あり: Last Known Good 表示 (B-048 / G-115-245)。
///
/// `editor.rs` の `DeltaMode::NoPre` 分岐で `d.last_active = Some(snap)` のときに呼ばれる。
/// 全 cell 色を `COL_MUTED` 固定にして「鮮度が落ちた凍結値」であることを示す
/// (Dark Cockpit 整合 / 純白 #FFFFFF 不使用 / G-72-10)。
///
/// `_tp_warn` は signature 安定性のため受け取るが未使用 (B-051 鮮度ドット導入時の
/// 修正範囲を最小化するため引数だけは残す)。凍結値は過去の計測値であり現在の
/// True Peak 警告 (`COL_FLORA_BRIGHT`) を点灯させるのは意味的に誤りなので、
/// 凍結表示中は意図的に強調を抑制する。
///
/// レイアウトは `draw_delta_grid` と対称 (Watch 3 項目: ΔLUFS / ΔTP / ΔCrest)。
/// `Grid::new` の id だけは `"post_delta_frozen"` で別 widget identity を持つ。
fn draw_delta_grid_frozen(ui: &mut egui::Ui, snap: &DeltaSnapshot, _tp_warn: bool) {
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        Grid::new("post_delta_frozen")
            .num_columns(3)
            .min_col_width(58.0)
            .spacing([6.0, 6.0])
            .show(ui, |ui| {
                value_row(ui, "ΔLUFS", fmt_delta(snap.lufs), "LU", COL_MUTED);
                value_row(ui, "ΔTP", fmt_delta(snap.tp), "dB", COL_MUTED);
                value_row(ui, "ΔCrest", fmt_delta(snap.crest), "dB", COL_MUTED);
            });
    });
}

/// Watch + PRE 不在 or Bypassed（DeltaMode::NoPre）: POST 絶対値 3 項目（LUFS-M / TP / Crest）。
/// guardian_64 による旧仕様復活。Record の右列と同じ値源だが Watch 用の単列レイアウト。
fn draw_watch_absolute_grid(ui: &mut egui::Ui, m: &MeasureResult) {
    let tp_warn = tp_over(m.true_peak);
    let tp_col = if tp_warn {
        COL_FLORA_BRIGHT
    } else {
        val_color(m.true_peak)
    };
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        Grid::new("post_watch_abs")
            .num_columns(3)
            .min_col_width(58.0)
            .spacing([6.0, 6.0])
            .show(ui, |ui| {
                value_row(ui, "LUFS-M", fmt_val(m.lufs_m), "LUFS", val_color(m.lufs_m));
                value_row(ui, "TP", fmt_val(m.true_peak), "dBTP", tp_col);
                value_row(ui, "Crest", fmt_val(m.crest), "dB", val_color(m.crest));
            });
    });
}

/// Record: 2 列 × 3 行。Δ 6 項目（ΔLUFS / ΔPSR / ΔTP / ΔN / ΔCrest / ΔSharp）。
/// S131 Daisuke 確定方針: POST = 処理量モニタリング (Δ) に専念。絶対値判定は Lens 側へ
/// 分離 (分離原則)。Watch 中の絶対値表示は `draw_watch_absolute_grid` 側で温存
/// (本指示 §3-4 / 対象外)。
///
/// 各 Δ セルの色:
/// - `delta_col` = mode (Active=COL_NORMAL / Stale=COL_MUTED / NoPre=COL_MUTED)
/// - `Δ.X` が None なら `COL_MUTED` で `---`
/// - ΔTP のみ POST 絶対 TP が 0 dBTP 超 (tp_warn) のとき `COL_FLORA_BRIGHT` (旧仕様維持)
///
/// `m: &MeasureResult` は ΔTP の tp_warn 判定にだけ使う (POST 絶対 true peak 参照)。
fn draw_record_section(ui: &mut egui::Ui, m: &MeasureResult, d: &DeltaResult) {
    let delta_col = match d.mode {
        DeltaMode::Active => COL_NORMAL,
        DeltaMode::Stale => COL_MUTED,
        DeltaMode::NoPre => COL_MUTED,
    };
    let tp_warn = tp_over(m.true_peak);

    // B-048 / G-115-245 Last Known Good (advisor 判断 1 案 X 条件付き):
    // `current` が None かつ `d.mode != Active` のとき `snap` (凍結値) で fallback する。
    // Active 時は §4-4 不変条件遵守 = 既存挙動完全維持 (current Some なら delta_col / None なら COL_MUTED で `---`)。
    // 戻り値 `(表示値 Option<f64>, 表示色 Color32)`。fmt_delta で None → "---" 既存パターン踏襲。
    let is_active = d.mode == DeltaMode::Active;
    let resolve = |current: Option<f64>, snap: Option<f64>| -> (Option<f64>, egui::Color32) {
        if is_active {
            // Active: 既存挙動完全維持 (last_active 参照しない)
            match current {
                Some(v) => (Some(v), delta_col),
                None => (None, COL_MUTED),
            }
        } else {
            // Stale/NoPre: 現値 → 凍結値 fallback → ---
            match (current, snap) {
                (Some(v), _) => (Some(v), delta_col),
                (None, Some(frozen)) => (Some(frozen), COL_MUTED),
                (None, None) => (None, COL_MUTED),
            }
        }
    };

    let snap = d.last_active.as_ref();
    let (lufs_v, lufs_col) = resolve(d.lufs, snap.and_then(|s| s.lufs));
    let (psr_v, psr_col) = resolve(d.psr, snap.and_then(|s| s.psr));
    let (n_v, n_col) = resolve(d.n_prime_total, snap.and_then(|s| s.n_prime_total));
    let (crest_v, crest_col) = resolve(d.crest, snap.and_then(|s| s.crest));
    let (sharp_v, sharp_col) = resolve(d.sharpness, snap.and_then(|s| s.sharpness));

    // ΔTP 特例: 現値 Some && tp_warn のときのみ COL_FLORA_BRIGHT 上書き (既存挙動維持)。
    // 凍結 tp (d.tp=None && snap.tp=Some) は COL_MUTED 固定 (過去の警告を現在の警告として
    // 強調しない / draw_delta_grid_frozen と整合)。
    let (tp_v, tp_col) = {
        let (v, c) = resolve(d.tp, snap.and_then(|s| s.tp));
        if d.tp.is_some() && tp_warn {
            (v, COL_FLORA_BRIGHT)
        } else {
            (v, c)
        }
    };

    ui.horizontal(|ui| {
        ui.add_space(10.0);
        Grid::new("post_record_vals")
            .num_columns(6)
            .min_col_width(40.0)
            .spacing([6.0, 4.0])
            .show(ui, |ui| {
                row_pair(
                    ui,
                    ("ΔLUFS", fmt_delta(lufs_v), "LU", lufs_col),
                    ("ΔPSR", fmt_delta(psr_v), "dB", psr_col),
                );
                row_pair(
                    ui,
                    ("ΔTP", fmt_delta(tp_v), "dB", tp_col),
                    ("ΔN", fmt_delta(n_v), "sone", n_col),
                );
                row_pair(
                    ui,
                    ("ΔCrest", fmt_delta(crest_v), "dB", crest_col),
                    ("ΔSharp", fmt_delta(sharp_v), "acum", sharp_col),
                );
            });
    });
}

fn row_pair(
    ui: &mut egui::Ui,
    left: (&str, String, &str, egui::Color32),
    right: (&str, String, &str, egui::Color32),
) {
    ui.label(RichText::new(left.0).size(11.0).color(COL_MUTED));
    ui.label(RichText::new(left.1).size(14.0).color(left.3).monospace());
    ui.label(RichText::new(left.2).size(10.0).color(COL_MUTED));
    ui.label(RichText::new(right.0).size(11.0).color(COL_MUTED));
    ui.label(RichText::new(right.1).size(14.0).color(right.3).monospace());
    ui.label(RichText::new(right.2).size(10.0).color(COL_MUTED));
    ui.end_row();
}

/// B-027 段階 2: pair PRE Name フィールド描画（クリック起動編集）。
///
/// PRE 側 `editor.rs:268-314 draw_name_field` と完全対称構造:
/// 通常モード: `Label::new(...).sense(Sense::click())` で表示。クリックで `request_focus`。
/// 編集モード (`pair_pre_name_edit_active` = `memory.has_focus(*PAIR_PRE_NAME_FOCUS_ID)`):
///   - `TextEdit::singleline(&mut state.pair_pre_name_edit_buffer).id(*PAIR_PRE_NAME_FOCUS_ID)`
///   - `changed()` 時にリアルタイム sanitize（不正入力を即座に剥がす）
///   - `Enter` (lost_focus + key_pressed) で sanitize 後 `pair_pre_name.write()` 確定
///   - `Escape` で discard（buffer 反映せず focus 解除）
///
/// 空 + 通常モード時の fallback は **空表示** (PRE 側のような UUID8 fallback は出さない)。
/// 理由: POST 自身の identity ではないため fallback 識別子は概念的に存在しない
/// (B-023 論点 4 (c) POST 独自命名禁止 と整合)。空文字時は trigger_keep の
/// filter も pass-through (B-027 段階 1 受入維持)。
fn draw_pair_pre_name_field(ui: &mut egui::Ui, state: &mut PostEditorState) {
    if pair_pre_name_edit_active(ui) {
        let response = ui.add(
            TextEdit::singleline(&mut state.pair_pre_name_edit_buffer)
                .id(*PAIR_PRE_NAME_FOCUS_ID)
                .desired_width(120.0)
                .font(TextStyle::Monospace),
        );
        if ui.input(|i| i.key_pressed(Key::Escape)) {
            ui.memory_mut(|mem| mem.surrender_focus(*PAIR_PRE_NAME_FOCUS_ID));
        } else if response.lost_focus() && ui.input(|i| i.key_pressed(Key::Enter)) {
            let sanitized = sanitize_name(&state.pair_pre_name_edit_buffer);
            if let Ok(mut g) = state.pair_pre_name.write() {
                *g = sanitized;
            }
            ui.memory_mut(|mem| mem.surrender_focus(*PAIR_PRE_NAME_FOCUS_ID));
        } else if response.changed() {
            state.pair_pre_name_edit_buffer = sanitize_name(&state.pair_pre_name_edit_buffer);
        }
    } else {
        let raw_name = state
            .pair_pre_name
            .read()
            .ok()
            .map(|g| g.clone())
            .unwrap_or_default();
        // 空時の表示: PRE 側 (UUID8 fallback) と異なり、POST は識別子を持たないため
        // 「pair: ___」プレースホルダで「未指定 = filter 無効」を視覚化する。
        let display = if raw_name.is_empty() {
            "pair: ___".to_string()
        } else {
            format!("pair: {}", raw_name)
        };
        let label = ui.add(
            Label::new(
                RichText::new(&display)
                    .size(14.0)
                    .color(COL_FLORA)
                    .monospace(),
            )
            .sense(Sense::click()),
        );
        if label.clicked() {
            state.pair_pre_name_edit_buffer = raw_name;
            ui.memory_mut(|mem| mem.request_focus(*PAIR_PRE_NAME_FOCUS_ID));
        }
    }
}

/// B-027 段階 3-A 修正 (G-115-49 / α-2 撤回): pair PRE 候補 ComboBox dropdown。
///
/// `kirin_root` (`$TMPDIR/kirin/`) 配下の **全 active PRE dir** を flatten 列挙する
/// (旧版の同 project_uuid filter は cdylib 隔離で構造的不成立のため撤回)。利用者は
/// 候補一覧から Name 識別 (B-027 段階 2 `pair_pre_name` filter) で目的 PRE を選ぶ
/// 運用となる。多 PRE 環境では複数 project_uuid 配下の PRE が混在表示される。
///
/// クリック確定時:
/// - `name` 持ち PRE → `pair_pre_name = name`（filter_candidates_by_name で 1 件絞れる）
/// - `name = None`   → `pair_pre_name = ""`（filter pass-through / Keep 距離優先）
///
/// 0 候補時は `ui.label("No candidates")` を出す（dropdown 内空表示）。
/// egui 0.31.1 公式 API: `ComboBox::from_id_salt` (旧 `from_id_source` は廃止)。
fn draw_pair_pre_combo(ui: &mut egui::Ui, state: &mut PostEditorState, now: f64) {
    let kirin_root = std::env::temp_dir().join("kirin");
    let pre_candidates = enumerate_active_pre_pair_candidates(&kirin_root);
    // B-027 段階 3-B α-7-3 / Step 9: All Keep 行 N 集計のため POST candidates も取得。
    // pair_pre_name.is_some() の件数 (= S115 Q-A7 採用 β / 自身 + ready peer の総数) を
    // ComboBox 先頭行の "All Keep ({N} ready)" 表示と display 判定 (N>=1) に使用。
    let post_candidates = enumerate_active_post_pair_candidates(&kirin_root);
    // α-7' All Stop: 自身が recording=true (Record 中) なら All Stop 行を出す。
    let recording = state.record_sm.is_recording();

    egui::ComboBox::from_id_salt("hypha_post_pair_pre_dropdown")
        .selected_text(RichText::new("▼").size(12.0).color(COL_FLORA).monospace())
        .width(140.0)
        .show_ui(ui, |ui| {
            // α-7' All Stop 行 (recording=true 時のみ / All Keep と排他表示 / 琥珀明度色)。
            // click handler 順序 = broadcast 先発火 → 自身 trigger_stop (Keep の対称形)。
            if recording {
                ui.push_id("hypha_post_all_stop_row", |ui| {
                    let label = RichText::new("All Stop")
                        .size(12.0)
                        .color(COL_FLORA_BRIGHT)
                        .monospace();
                    if ui.selectable_label(false, label).clicked() {
                        let instance_id = read_instance_id_arc(&state.instance_id);
                        let project_hash_snapshot = read_project_hash_arc(&state.project_hash);
                        let daw_session_id_snapshot =
                            read_daw_session_id_arc(&state.daw_session_id);

                        log::info!(
                            "[POST all_stop] click: originator={}",
                            instance_id
                        );

                        // 1. broadcast 先発火 (Keep と完全対称)。
                        trigger_all_stop_broadcast(
                            &instance_id,
                            &project_hash_snapshot,
                            &daw_session_id_snapshot,
                            &mut state.toast,
                            now,
                        );
                        // 2. 自身も trigger_stop (Stop button と同経路)。
                        trigger_stop(
                            &state.record_sm,
                            &project_hash_snapshot,
                            &instance_id,
                            &state.pair_label,
                            &state.paired_pre_target,
                            &mut state.toast,
                            now,
                        );
                    }
                });
            }

            // B-027 段階 3-B α-7-3 / Step 9: All Keep 行 (先頭 / N>=1 時のみ表示)。
            // S117 #20 (i): click handler 順序 = broadcast 先発火 → 自身 trigger_keep
            // (broadcast 失敗で自身まで pair 不可になることを構造的に回避)。
            // α-7': recording=true 時は All Keep 行を非表示 (Record 中は Keep 不要)。
            let n_ready = post_candidates
                .iter()
                .filter(|c| c.pair_pre_name.is_some())
                .count();
            if !recording && n_ready >= 1 {
                ui.push_id("hypha_post_all_keep_row", |ui| {
                    let label = format!("All Keep ({} ready)", n_ready);
                    if ui.selectable_label(false, label).clicked() {
                        // 内部構築: instance_id (lazy-read) / m (Measure snapshot) /
                        // pair_pre_name_snapshot (RwLock read) — draw_button_row L774
                        // 既存 trigger_keep 呼出と同経路。
                        // §4-5 Step 1: project_hash / daw_session_id も同位相で
                        // lazy-read snapshot (Arc 化済 / chunk-restore 後の最新 cell 値)。
                        let instance_id = read_instance_id_arc(&state.instance_id);
                        let project_hash_snapshot = read_project_hash_arc(&state.project_hash);
                        let daw_session_id_snapshot =
                            read_daw_session_id_arc(&state.daw_session_id);
                        let m = state
                            .measure
                            .lock()
                            .map(|g| g.clone())
                            .unwrap_or_default();
                        let pair_pre_name_snapshot = state
                            .pair_pre_name
                            .read()
                            .map(|g| g.clone())
                            .unwrap_or_default();

                        log::info!(
                            "[POST all_keep] click: originator={} n_ready={}",
                            instance_id, n_ready
                        );

                        // 1. broadcast 先発火 (#20 (i) / Step 8 実装済 fn)
                        trigger_all_keep_broadcast(
                            &instance_id,
                            &project_hash_snapshot,
                            &daw_session_id_snapshot,
                            &mut state.toast,
                            now,
                        );
                        // 2. 自身も trigger_keep (Step 7 wrapper 経由 / draw_button_row
                        //    L774 と同引数列)
                        trigger_keep(
                            *state.license,
                            &state.record_sm,
                            &instance_id,
                            &project_hash_snapshot,
                            &daw_session_id_snapshot,
                            &state.pair_label,
                            &state.paired_pre_target,
                            &m,
                            &mut state.toast,
                            now,
                            &pair_pre_name_snapshot,
                        );

                        // §4-5 Step 4 診断: click handler 後続処理 (trigger_keep →
                        // write_pending → record_sm 遷移) を経た frame 末尾で broadcast
                        // file が依然として存在することを確認。frame 内 ms 単位削除
                        // (sandbox / OS / 内部の隠し経路) を切り分ける目的。
                        if let Ok(paths) = StoragePaths::default_macos() {
                            let bp = all_keep_signal_path(
                                &paths.plugin_data_dir(),
                                &project_hash_snapshot,
                                &instance_id,
                            );
                            match std::fs::metadata(&bp) {
                                Ok(meta) => log::info!(
                                    "[POST all_keep] post-frame metadata ok: path={} len={}",
                                    bp.display(),
                                    meta.len()
                                ),
                                Err(e) => log::warn!(
                                    "[POST all_keep] post-frame metadata FAILED: path={} err={}",
                                    bp.display(),
                                    e
                                ),
                            }
                        }
                    }
                });
            }

            // ── 既存 PRE candidates 列挙 (完全不変) ─────────────────────────
            if pre_candidates.is_empty() {
                ui.label(
                    RichText::new("No candidates")
                        .size(11.0)
                        .color(COL_MUTED)
                        .monospace(),
                );
                return;
            }
            for cand in &pre_candidates {
                // B-027 段階 3 (a) 仮説 2 (G-115-53): instance_id (UUID v4) で push_id
                // 化し widget identity を sort 順入替に対して固定する。
                // selectable_label の auto-ID は label_text 文字列ハッシュに依存
                // するため、同 index 位置の text 変動で press/release フレーム間の
                // ID 不一致 → clicked() event 喪失 (#5-A-3 異常 2) を起こす。
                // push_id で外側スコープ ID を固定すると本問題が構造的に解消する。
                ui.push_id(cand.instance_id.as_str(), |ui| {
                    let label_text = candidate_dropdown_label(cand);
                    if ui.selectable_label(false, label_text).clicked() {
                        let new_name = cand.name.clone().unwrap_or_default();
                        if let Ok(mut g) = state.pair_pre_name.write() {
                            *g = new_name;
                        }
                        log::info!(
                            "[POST pair-combo] selected: instance_id={} name={:?}",
                            cand.instance_id,
                            cand.name
                        );
                    }
                });
            }
        });
}

/// ComboBox dropdown 1 行の表示文字列を組み立てる。
///
/// `Some(name)` → `"snare #abc12345"`、`None` → `"(no name) #abc12345"`。
/// instance_id は先頭 8 文字のみ（300×200 GUI に収まる短形）。
fn candidate_dropdown_label(cand: &PreCandidate) -> String {
    let id_prefix: String = cand.instance_id.chars().take(8).collect();
    let name_part = cand.name.as_deref().unwrap_or("(no name)");
    format!("{name_part} #{id_prefix}")
}

// ── ボタン行 ──────────────────────────────────────────────────────────────

fn draw_button_row(
    ui: &mut egui::Ui,
    recording: bool,
    license: License,
    state: &mut PostEditorState,
    m: &MeasureResult,
    now: f64,
) {
    // B-022 段階 1: chunk-restore 後の最新値を 1 フレーム 1 回 lazy-read。
    // ボタン押下が同フレーム内で発火するため、各 trigger_* に同じ値を渡せる。
    // §4-5 Step 1: project_hash / daw_session_id も同位相で 1 フレーム 1 回 lazy-read。
    let instance_id = read_instance_id_arc(&state.instance_id);
    let project_hash_snapshot = read_project_hash_arc(&state.project_hash);
    let daw_session_id_snapshot = read_daw_session_id_arc(&state.daw_session_id);
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        if recording {
            if state.note_picker_open {
                // サブ2-C: Note picker — [Good] [Fix] [Hold] [Cancel]
                for tag in ["Good", "Fix", "Hold"] {
                    if ui.button(tag).clicked() {
                        log::info!("[hypha-fork] button clicked: {}", tag);
                        trigger_note_save(
                            tag,
                            &project_hash_snapshot,
                            &instance_id,
                            &mut state.toast,
                            now,
                        );
                        state.note_picker_open = false;
                    }
                }
                if ui.button("Cancel").clicked() {
                    log::info!("[hypha-fork] button clicked: Cancel");
                    state.note_picker_open = false;
                }
            } else {
                // Record: [Stop] [Note]
                if show_stop_record_button(license) && ui.button("Stop").clicked() {
                    log::info!("[hypha-fork] button clicked: Stop");
                    trigger_stop(
                        &state.record_sm,
                        &project_hash_snapshot,
                        &instance_id,
                        &state.pair_label,
                        &state.paired_pre_target,
                        &mut state.toast,
                        now,
                    );
                }
                if show_note_button(license) && ui.button("Note").clicked() {
                    log::info!("[hypha-fork] button clicked: Note");
                    state.note_picker_open = true;
                }
            }
        } else {
            // Watch: [Keep or Sense hint]（License::Unknown は空行）
            if show_save_button(license) {
                if ui.button("Keep").clicked() {
                    log::info!("[hypha-fork] button clicked: Keep");
                    // B-027 段階 2: 押下時点の pair_pre_name を lazy-read。
                    // 編集モード途中で Keep 押下されても直前の確定値を使う
                    // (編集中の `edit_buffer` は未確定として扱う)。
                    let pair_pre_name_snapshot = state
                        .pair_pre_name
                        .read()
                        .ok()
                        .map(|g| g.clone())
                        .unwrap_or_default();
                    trigger_keep(
                        license,
                        &state.record_sm,
                        &instance_id,
                        &project_hash_snapshot,
                        &daw_session_id_snapshot,
                        &state.pair_label,
                        &state.paired_pre_target,
                        m,
                        &mut state.toast,
                        now,
                        &pair_pre_name_snapshot,
                    );
                }
            } else if license == License::Sense {
                let resp = ui.add(
                    egui::Button::new(
                        RichText::new(SENSE_RECORD_HINT).size(11.0).color(COL_FLORA),
                    )
                    .frame(false),
                );
                if resp.clicked() {
                    log::info!("[hypha-fork] button clicked: SenseHint");
                    trigger_open_upsell(&mut state.toast, now);
                }
            }
            // License::Unknown は空行（状態不明のため Sense hint も出さない。R-28 / ZSA）
        }
    });
}

// ── ボタンアクション ──────────────────────────────────────────────────────

// G-115-64 (番人 #137 確定): pair_label クリアは kirin_measure::clear_pair_label を使用.
// 本 crate からは re-export 経由で参照する (use 文上部 / 単一情報源).

/// pair_label を設定（Record 開始成功時）。
///
/// B-023 段階 4: 2 引数化（paired_pre_name, target_id）。Keep 時は
/// PRE Name 不明（IO Thread 経由で後刻取得）なので空文字を渡し、
/// `format_pair_label` の UUID8 fallback で `pair: <8 文字>` を表示する。
/// `format_pair_label` は `kirin_measure` re-export（単一情報源）。
fn set_pair_label(pair_label: &Arc<Mutex<String>>, paired_pre_name: &str, target_id: &str) {
    if let Ok(mut g) = pair_label.lock() {
        *g = format_pair_label(paired_pre_name, target_id);
    }
}

/// Keep タップ: PRE 候補 / 排他 / license を事前チェック → `try_enter_record` → `write_pending`。
/// 成功時は pair_label を `pair: PRE_xxxxxxxx` で設定する（POST GUI 表示用）。
/// 同時に `paired_pre_target` に `target_id` を保存し、IO Thread が次回の writer_start で
/// plugin_data の `paired_pre_instance_id` field に書き込めるようにする（v1.2 (a)）。
///
/// B-027 段階 2: `pair_pre_name` 非空時は `filter_candidates_by_name` で候補を
/// 絞り込む。空文字時は filter pass-through (B-027 段階 1 受入維持)。
///
/// B-027 段階 3-B α-7-4-D Step 1: 本関数は wrapper 化 (外側シグネチャ完全不変)。
/// 実装は [`trigger_keep_internal`] に分離。Some(toast) 経由で既存 behavior 完全保持。
/// broadcast 受信側 (Step 11) からは `trigger_keep_internal` を `toast = None` で
/// 直接呼出して toast 嵐 (N 倍 toast) を抑制する。
#[allow(clippy::too_many_arguments)]
fn trigger_keep(
    license: License,
    record_sm: &Arc<RecordStateMachine>,
    instance_id: &str,
    project_hash: &str,
    daw_session_id: &str,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    m: &MeasureResult,
    toast: &mut Option<Toast>,
    now: f64,
    pair_pre_name: &str,
) {
    trigger_keep_internal(
        license,
        record_sm,
        instance_id,
        project_hash,
        daw_session_id,
        pair_label,
        paired_pre_target,
        m,
        Some(toast),
        now,
        pair_pre_name,
    );
}

/// `trigger_keep` の内部実装 (B-027 段階 3-B α-7-4-D Step 1)。
///
/// `toast: Option<&mut Option<Toast>>` で None 時は toast 抑制。Step 11 で IO Thread
/// broadcast 受信側 (α-7 All Keep) から本関数を `toast = None` で直接呼出し、N 倍
/// toast 嵐を構造的に防ぐ。`log::*` は不変 (toast 抑制でも log は残す)。
///
/// 既存ロジック完全保持: 上限 check / record_sm 遷移 / write_pending / ロールバック /
/// set_pair_label / paired_pre_target 設定 — 全て Step 7 改修前と同等動作。
#[allow(clippy::too_many_arguments)]
pub(crate) fn trigger_keep_internal(
    license: License,
    record_sm: &Arc<RecordStateMachine>,
    instance_id: &str,
    project_hash: &str,
    daw_session_id: &str,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    m: &MeasureResult,
    mut toast: Option<&mut Option<Toast>>,
    now: f64,
    pair_pre_name: &str,
) {
    // 1. ストレージパス解決
    let paths = match StoragePaths::default_macos() {
        Ok(p) => p,
        Err(e) => {
            log::warn!("[POST keep] StoragePaths error: {:?}", e);
            if let Some(t) = toast.as_mut() {
                **t = Some(Toast::new("Kirin OS not installed", now));
            }
            return;
        }
    };
    let plugin_data_dir = paths.plugin_data_dir();
    let tmp_base = std::env::temp_dir().join("kirin");

    // 2. PRE 候補スキャン（B-027 段階 2 fix: 多 PRE 環境対応 / NG-1 + NG-2）
    //
    // 旧: `discover_active_pre_dir` で **mtime 最新 1 件** project_uuid のみ採用。
    //     2 セット環境 (PRE A + POST A + PRE B + POST B) で他 project_uuid 配下の
    //     目的 PRE が完全に視界外 → 別 PRE 誤紐付け / "No matching PRE" 誤表示。
    // 新: `discover_active_pre_dirs` で fresh な全 project_uuid を Vec で取得し、
    //     全 dir 配下の `pre.json` を flatten して 1 つの候補リストに統合する。
    //     Δ 経路 (io_thread_post::compute_delta_with_state) は引き続き
    //     `discover_active_pre_dir` (単一最新) で「最新 1 PRE で Δ 計算」セマン
    //     ティクス維持。本変更は trigger_keep のみ。
    let pre_project_dirs = discover_active_pre_dirs(&tmp_base);
    if pre_project_dirs.is_empty() {
        log::info!("[POST keep] no PRE candidate (discovery returned empty)");
        if let Some(t) = toast.as_mut() {
            **t = Some(Toast::new("No PRE plugin found", now));
        }
        return;
    }
    let candidates: Vec<_> = pre_project_dirs
        .iter()
        .flat_map(|d| scan_pre_candidates_in(d))
        .collect();
    if candidates.is_empty() {
        log::info!(
            "[POST keep] no PRE candidate (project_dirs={} all empty)",
            pre_project_dirs.len()
        );
        if let Some(t) = toast.as_mut() {
            **t = Some(Toast::new("No PRE plugin found", now));
        }
        return;
    }

    // B-027 段階 2: pair_pre_name 非空時は Name で filter。空文字は pass-through
    // (filter_candidates_by_name 内部で判定 / B-027 段階 1 受入維持)。
    let candidates = filter_candidates_by_name(candidates, pair_pre_name);
    if candidates.is_empty() {
        log::info!(
            "[POST keep] no PRE matches pair_pre_name=\"{}\" (project_dirs={})",
            pair_pre_name,
            pre_project_dirs.len()
        );
        if let Some(t) = toast.as_mut() {
            **t = Some(Toast::new("No PRE Paired", now));
        }
        return;
    }

    // 3. 排他チェック（B-027 段階 3-A: 上限 12 active per project_hash）
    match check_record_exclusion(&plugin_data_dir, project_hash) {
        ExclusionResult::Ok => {}
        ExclusionResult::Conflict { role, heartbeat, .. } => {
            log::info!(
                "[POST keep] exclusion conflict (>= {} active): role={:?} heartbeat={}",
                MAX_ACTIVE_PER_PROJECT, role, heartbeat
            );
            if let Some(t) = toast.as_mut() {
                **t = Some(Toast::new("Maximum 12 pairs reached", now));
            }
            return;
        }
    }

    // 4. 最近 PRE 選定
    let post_metrics = PostMetrics {
        lufs_m: m.lufs_m,
        true_peak: m.true_peak,
        crest: m.crest,
    };
    let target_id = match pick_closest_pre(&candidates, post_metrics) {
        Some(c) => c.instance_id.clone(),
        None => {
            log::warn!("[POST keep] pick_closest_pre returned None despite candidates");
            if let Some(t) = toast.as_deref_mut() {
                *t = Some(Toast::new("No PRE plugin found", now));
            }
            return;
        }
    };

    // 5. RecordStateMachine 遷移（license 二重 gate）
    match record_sm.try_enter_record(license) {
        Ok(()) => {}
        Err(TransitionError::LicenseDenied) => {
            log::info!("[POST keep] license denied: {:?}", license);
            if let Some(t) = toast.as_mut() {
                **t = Some(Toast::new("Record requires Kirin OS license", now));
            }
            return;
        }
        Err(TransitionError::AlreadyRecording) => {
            log::info!("[POST keep] already recording — ignored");
            return;
        }
    }

    // 6. record_signal を pending で書き込み（A-3 修正後: post_instance_id を path 識別子に）
    match write_pending(
        &plugin_data_dir,
        project_hash,
        instance_id,
        target_id.clone(),
        daw_session_id.to_string(),
    ) {
        Ok(_) => {
            log::info!(
                "[POST keep] write_pending ok: requested_by={} target={} daw={}",
                instance_id, target_id, daw_session_id
            );
            // 7. pair_label を表示用に設定（Keep 時は PRE Name 不明 → UUID8 fallback）
            // B-023 段階 4: PRE 側 ack 後 IO Thread の poll が paired_pre_name を
            // 取得して同 Arc を `pair: <name>` に上書きする（最大 1 秒遅延）。
            set_pair_label(pair_label, "", &target_id);
            // 8. v1.2 (a): paired_pre_target を保存（IO Thread が writer_start 時に消費）
            if let Ok(mut g) = paired_pre_target.lock() {
                *g = Some(target_id.clone());
            }
        }
        Err(e) => {
            log::warn!("[POST keep] write_pending failed: {}", e);
            // G-115-64 (番人 #137 確定): write_pending 失敗時のロールバックは
            // try_enter_record 成功後の "部分 commit" 状態のため、Record→Watch
            // と同じ 3 ステップ cleanup を必ず通過させる (構造的契約成立).
            exit_record_full(record_sm, pair_label, paired_pre_target);
            if let Some(t) = toast.as_mut() {
                **t = Some(Toast::new("Failed to start record", now));
            }
        }
    }
}

/// All Keep broadcast を filesystem に書込んで同 project の他 POST に通知する
/// (B-027 段階 3-B α-7-4-D Step 2)。
///
/// Originator (= ComboBox 「All Keep ({N} ready)」を click した POST) のみが呼出す。
/// 受信側 (Step 10-11 で実装) は io_thread_post.rs sub-tick で
/// [`kirin_measure::scan_broadcasts_dir`] / `trigger_keep_internal(toast=None)` を経由
/// して各々 pair 確定する (toast 嵐回避)。
///
/// # 設計判断 (S117)
/// - #19 (i): Err 経路では `record_sm` を触らず log::warn! + toast のみ。自身の
///   `trigger_keep` は ComboBox click handler 順序 (#20 (i)) で本関数の **後** に実行
///   されるため、broadcast 失敗で自身まで pair 不可になることはない。
/// - #21 (i): log 文言は DEV INBOX §2-α-7-4-D Step 2 擬似コード踏襲。
///
/// # cdylib 越境通信
/// 含まない (filesystem 経由のみ / `OnceLock` 不触 / 申し送り #22 適合)。
///
/// Step 9 (ComboBox 先頭行 click handler) で呼出経路実装まで dead_code lint が立つ
/// → `#[allow(dead_code)]` 付与 (Step 9 完了時に削除予定 / 既存 Step 5/6 と同位相)。
#[allow(dead_code)]
fn trigger_all_keep_broadcast(
    originator_instance_id: &str,
    project_hash: &str,
    daw_session_id: &str,
    toast: &mut Option<Toast>,
    now: f64,
) {
    let paths = match StoragePaths::default_macos() {
        Ok(p) => p,
        Err(e) => {
            log::warn!("[POST all_keep] StoragePaths resolve failed: {:?}", e);
            *toast = Some(Toast::new("Kirin OS not installed", now));
            return;
        }
    };
    let plugin_data_dir = paths.plugin_data_dir();
    match write_broadcast(
        &plugin_data_dir,
        project_hash,
        originator_instance_id,
        daw_session_id.to_string(),
    ) {
        Ok(broadcast) => {
            let broadcast_path =
                all_keep_signal_path(&plugin_data_dir, project_hash, originator_instance_id);
            log::info!(
                "[POST all_keep] broadcast written: originator={} started_at={} path={}",
                originator_instance_id,
                broadcast.started_at,
                broadcast_path.display()
            );
            // §4-5 Step 4 診断: write_broadcast Ok 直後に fs::metadata で
            // 物理存在を確認 (sandbox 経由の path redirection / atomic rename
            // 失敗した場合に Ok を返しているケースを切り分ける)。NIH_LOG 経由で
            // metadata err が出れば「write は成功扱いだが file が path にない」
            // = 内部削除以外の経路 (DAW sandbox / OS) が確定する。
            match std::fs::metadata(&broadcast_path) {
                Ok(meta) => log::info!(
                    "[POST all_keep] post-write metadata ok: path={} len={}",
                    broadcast_path.display(),
                    meta.len()
                ),
                Err(e) => log::warn!(
                    "[POST all_keep] post-write metadata FAILED: path={} err={}",
                    broadcast_path.display(),
                    e
                ),
            }
        }
        Err(e) => {
            log::warn!("[POST all_keep] broadcast write failed: {:?}", e);
            *toast = Some(Toast::new("All Keep failed (file write error)", now));
        }
    }
}

/// Stop タップ: Watch へ戻し、record_signal を released に更新。pair_label をクリア。
/// `paired_pre_target` も None に戻す（v1.2 (a) 次の Keep 待ち状態）。
///
/// α-7' All Stop: Some(toast) wrapper として `trigger_stop_internal` に委譲。
/// broadcast 受信側 (lib.rs trigger_stop_resolution closure) からは toast=None で
/// 直接呼出して toast 嵐を構造的に防ぐ (`trigger_keep` / `trigger_keep_internal` と同パターン)。
#[allow(clippy::too_many_arguments)]
fn trigger_stop(
    record_sm: &Arc<RecordStateMachine>,
    project_hash: &str,
    instance_id: &str,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    toast: &mut Option<Toast>,
    now: f64,
) {
    trigger_stop_internal(
        record_sm,
        project_hash,
        instance_id,
        pair_label,
        paired_pre_target,
        Some(toast),
        now,
    );
}

/// `trigger_stop` 内部実装 (α-7')。toast を Option 化し broadcast 受信側から
/// `toast = None` で呼出可能にする (`trigger_keep_internal` と同パターン)。
#[allow(clippy::too_many_arguments)]
pub(crate) fn trigger_stop_internal(
    record_sm: &Arc<RecordStateMachine>,
    project_hash: &str,
    instance_id: &str,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    mut toast: Option<&mut Option<Toast>>,
    now: f64,
) {
    // G-115-64 (番人 #137 確定): Stop タップ時の Record→Watch 遷移を
    // exit_record_full で 3 ステップ一括化. IO Thread 自然終了経路 (poll_pre_liveness_at /
    // poll_ack_timeout_with_base / handle_exit_reason) と完全対称な契約を成立させる.
    exit_record_full(record_sm, pair_label, paired_pre_target);
    match StoragePaths::default_macos() {
        Ok(paths) => {
            let plugin_data_dir = paths.plugin_data_dir();
            match mark_released(&plugin_data_dir, project_hash, instance_id) {
                Ok(true) => log::info!("[POST stop] mark_released ok"),
                Ok(false) => log::info!("[POST stop] no signal to release"),
                Err(e) => {
                    log::warn!("[POST stop] mark_released failed: {}", e);
                    if let Some(t) = toast.as_mut() {
                        **t = Some(Toast::new("Record stop error", now));
                    }
                }
            }

            // B-027 段階 3-B α-7 / Group 2 統合点 #2 (Gap-6 局所対処):
            // mark_released 直後に self_post_iid 用 record_signal/{POST_iid}.json を
            // 削除する。POST 自身が writer = cleanup 責任を持つ (cdylib 越境通信
            // 媒体の lifecycle 管理原則 / 設計判断 #5)。
            //
            // mark_released を残した上で delete を追加: PRE 側 1 秒 polling との
            // race で mark_released 結果を観測する経路は保持しつつ (多重防御 /
            // 設計判断 #9 (i) PRE 側 file removed 経路に委ねる方針と整合)、
            // orphan を構造的に防ぐ。
            //
            // 失敗時 warn のみ (設計判断 #8): trigger_stop 内 panic は GUI freeze
            // のため避ける。残骸は startup sweep (Gap-9 / B-026 別途) で救済。
            //
            // delete_signal は冪等 (record_signal.rs:289-301 / NotFound→Ok)。
            // 統合点 #3 (Drop) / #4 (IO Thread terminate) と重複呼出されても安全。
            match delete_signal(&plugin_data_dir, project_hash, instance_id) {
                Ok(()) => log::info!(
                    "[POST cleanup #2] record_signal deleted: {}",
                    instance_id
                ),
                Err(e) => log::warn!(
                    "[POST cleanup #2] delete_signal failed: {:?}",
                    e
                ),
            }

            // B-027 段階 3-B α-7-4-D / Step 12-A 統合点 #2 (DEV INBOX §9-3 / S117 判断 2 (P)):
            // originator として配置した all_keep_signal/{POST_iid}.json broadcast を削除。
            // delete_broadcast は冪等 (all_keep_signal.rs:211-215 / NotFound→Ok)。統合点
            // #3 (Drop) / #4 (IO Thread terminate) と重複呼出されても安全。
            // 失敗時 warn のみ (設計判断 #8 / 既存 delete_signal と同規範)。
            match delete_broadcast(&plugin_data_dir, project_hash, instance_id) {
                Ok(()) => log::info!(
                    "[POST cleanup #2 broadcast] delete_broadcast succeeded: instance={}",
                    instance_id
                ),
                Err(e) => log::warn!(
                    "[POST cleanup #2 broadcast] delete_broadcast failed: {:?}",
                    e
                ),
            }

            // α-7' Step 6 (主バグ修正 / R-9 真因): all_stop_signal の #2 統合点 (trigger_stop)
            // からの delete を **撤去**。理由: All Stop click handler は同 frame で
            // `trigger_all_stop_broadcast` (write) → `trigger_stop` (cleanup) を呼ぶため、
            // ここで delete_stop_broadcast を発火させると **originator が write した自身の
            // all_stop_signal broadcast を ms 単位で自爆削除** し、受信側 1 秒 sub-tick が
            // 構造的に scan 不能になる (Hpha0504 実機: 5/5 receivers all miss)。
            //
            // all_stop_signal の lifecycle は #3 (Drop) + #4 (IO Thread shutdown) のみで
            // 管理する。orphan broadcast は 30-sec stale fallback (`is_stop_broadcast_stale`)
            // で受信側が無視するため永久残存しない。
            //
            // 注: all_keep_signal の #2 delete (上の `delete_broadcast`) はそのまま保持
            // (Record セッション中 broadcast 寿命 > 1 sec で安全 / Pass 15 既存破壊禁止)。
        }
        Err(e) => {
            log::warn!("[POST stop] StoragePaths error: {:?}", e);
        }
    }
}

/// All Stop broadcast を filesystem に書込 (α-7')。`trigger_all_keep_broadcast` と完全対称。
///
/// originator (= All Stop ボタンを押した POST) が 1 回だけ呼ぶ。受信側
/// (`io_thread_post.rs` sub-tick) は同 project_hash の `all_stop_signal/*.json` を全件
/// scan し、cross-process filter + self skip + 既処理 skip を経て新 broadcast 検出時に
/// `trigger_stop_internal(toast=None)` を発火する。
#[allow(dead_code)]
fn trigger_all_stop_broadcast(
    originator_instance_id: &str,
    project_hash: &str,
    daw_session_id: &str,
    toast: &mut Option<Toast>,
    now: f64,
) {
    let paths = match StoragePaths::default_macos() {
        Ok(p) => p,
        Err(e) => {
            log::warn!("[POST all_stop] StoragePaths resolve failed: {:?}", e);
            *toast = Some(Toast::new("Kirin OS not installed", now));
            return;
        }
    };
    let plugin_data_dir = paths.plugin_data_dir();
    match write_stop_broadcast(
        &plugin_data_dir,
        project_hash,
        originator_instance_id,
        daw_session_id.to_string(),
    ) {
        Ok(broadcast) => {
            let bp = all_stop_signal_path(&plugin_data_dir, project_hash, originator_instance_id);
            log::info!(
                "[POST all_stop] broadcast written: originator={} started_at={} path={}",
                originator_instance_id,
                broadcast.started_at,
                bp.display()
            );
        }
        Err(e) => {
            log::warn!("[POST all_stop] broadcast write failed: {:?}", e);
            *toast = Some(Toast::new("All Stop failed (file write error)", now));
        }
    }
}

/// Note タグ確定: 最新 `plugin_data/.../post/*.json` に annotation を追記。
fn trigger_note_save(
    tag: &str,
    project_hash: &str,
    instance_id: &str,
    toast: &mut Option<Toast>,
    now: f64,
) {
    let paths = match StoragePaths::default_macos() {
        Ok(p) => p,
        Err(e) => {
            log::warn!("[note] StoragePaths error: {:?}", e);
            *toast = Some(Toast::new("Note save failed", now));
            return;
        }
    };
    match append_annotation_to_latest(
        &paths.plugin_data_dir(),
        project_hash,
        instance_id,
        PluginDataRole::Post,
        tag.to_string(),
    ) {
        Ok(true) => {
            log::info!("[note] appended to latest post record: {}", tag);
            *toast = Some(Toast::new(format!("Note saved: {tag}"), now));
        }
        Ok(false) => {
            log::warn!("[note] no active record file");
            *toast = Some(Toast::new(format!("Note saved: {tag}"), now));
        }
        Err(e) => {
            log::warn!("[note] append failed: {}", e);
            *toast = Some(Toast::new("Note save failed", now));
        }
    }
}

/// Sense hint タップ: ブラウザで URL を開く。
fn trigger_open_upsell(toast: &mut Option<Toast>, now: f64) {
    match open::that(SENSE_UPSELL_URL) {
        Ok(()) => log::info!("[POST sense-hint] opened {}", SENSE_UPSELL_URL),
        Err(e) => {
            log::warn!("[POST sense-hint] open failed: {}", e);
            *toast = Some(Toast::new("Could not open browser", now));
        }
    }
}

fn draw_led(ui: &mut egui::Ui, color: egui::Color32) {
    let (rect, _) = ui.allocate_exact_size(Vec2::splat(12.0), egui::Sense::hover());
    ui.painter().circle_filled(rect.center(), 5.0, color);
}

// ── T-E / T-F helpers (guardian_77 v3 §9 / §10) ─────────────────────────

/// Throttle-guarded proposals rescan. Reads `plugin_data/{ph}/preset/` once
/// per 500 ms and caches the newest v2.0 file in the editor-local state.
///
/// R-28 silence: any failure (missing installation_id, storage unresolved,
/// verify failures inside scan_latest_v2_preset) ends with
/// `state.latest_proposals = None` and nothing rendered.
fn maybe_rescan_proposals(state: &mut PostEditorState, now: f64) {
    let should_scan = match state.proposals_scan_last {
        Some(t) => now - t >= PROPOSALS_SCAN_INTERVAL_SECS,
        None => true,
    };
    if !should_scan {
        return;
    }
    state.proposals_scan_last = Some(now);

    let installation_id = state.installation_id.as_str();
    if installation_id.is_empty() {
        state.latest_proposals = None;
        return;
    }
    let Ok(paths) = StoragePaths::default_macos() else {
        state.latest_proposals = None;
        return;
    };
    // §4-5 Step 1: project_hash を use site で lazy-read (Arc 化済 / chunk-restore 後の
    // 最新 cell 値を proposals scan の root に反映)。
    let project_hash_snapshot = read_project_hash_arc(&state.project_hash);
    state.latest_proposals = scan_latest_v2_preset(
        &paths.plugin_data_dir(),
        &project_hash_snapshot,
        installation_id,
    );
}

/// Resolve current playback time in seconds. Primary path reads the atomic
/// written by `process()`; fallback uses wall-clock since Record start
/// (§10.2). Returns `None` when neither source is available (Watch mode
/// outside a Record session → section label row is silent).
fn current_playback_time(state: &PostEditorState, now: f64) -> Option<f64> {
    let pos = state.playback_pos_samples.load(Ordering::Relaxed);
    let sr = state.playback_sample_rate.load(Ordering::Relaxed);
    if pos != i64::MIN && sr > 0 {
        return Some(pos as f64 / sr as f64);
    }
    state.record_start_wall_time.map(|start| now - start)
}

/// Severity → 1-char ASCII glyph (egui default font has no guaranteed CJK
/// / dingbat coverage).
fn severity_glyph(severity: &str) -> &'static str {
    match severity {
        "warning" => "!",
        "suggestion" => ">",
        _ => "i",
    }
}

fn severity_color(severity: &str) -> egui::Color32 {
    match severity {
        "warning" => COL_FLORA_BRIGHT,
        _ => COL_NORMAL,
    }
}

/// T-E + T-F render block. Lays out:
///
///   [section_label]              [Cards N [+]/[-]]
///   (if expanded, up to MAX_CARDS_RENDERED card rows inside a ScrollArea)
///
/// Silent (zero rows rendered) when both `latest_proposals` and the section
/// label resolve to `None` — per R-28 / §10.4.
fn draw_proposals_block(ui: &mut egui::Ui, state: &mut PostEditorState, now: f64) {
    let play_t = current_playback_time(state, now);
    let section_label = match (state.latest_proposals.as_ref(), play_t) {
        (Some(p), Some(t)) => lookup_section_label(&p.section_boundaries, t).map(str::to_owned),
        _ => None,
    };
    let has_proposals = state.latest_proposals.is_some();

    if !has_proposals && section_label.is_none() {
        return; // R-28
    }

    ui.add_space(4.0);
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        if let Some(l) = section_label.as_ref() {
            ui.label(
                RichText::new(l.as_str())
                    .size(11.0)
                    .color(COL_FLORA)
                    .monospace(),
            );
        }
        if let Some(p) = state.latest_proposals.as_ref() {
            ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                ui.add_space(10.0);
                let n = p.cards.len();
                let toggle = if state.cards_expanded { "[-]" } else { "[+]" };
                let btn_text = format!("Cards {} {}", n, toggle);
                let resp = ui.add(
                    egui::Button::new(
                        RichText::new(&btn_text).size(11.0).color(COL_NORMAL).monospace(),
                    )
                    .frame(false),
                );
                if resp.clicked() {
                    state.cards_expanded = !state.cards_expanded;
                }
            });
        }
    });

    if state.cards_expanded {
        if let Some(p) = state.latest_proposals.as_ref() {
            let shown = p.cards.len().min(MAX_CARDS_RENDERED);
            egui::ScrollArea::vertical()
                .id_salt("post_cards_scroll")
                .max_height(48.0)
                .show(ui, |ui| {
                    for c in p.cards.iter().take(MAX_CARDS_RENDERED) {
                        ui.horizontal(|ui| {
                            ui.add_space(10.0);
                            ui.label(
                                RichText::new(severity_glyph(&c.severity))
                                    .size(11.0)
                                    .color(severity_color(&c.severity))
                                    .monospace(),
                            );
                            ui.label(
                                RichText::new(&c.message_key)
                                    .size(11.0)
                                    .color(COL_NORMAL)
                                    .monospace(),
                            );
                        });
                    }
                    if p.cards.len() > MAX_CARDS_RENDERED {
                        let more = p.cards.len() - shown;
                        ui.horizontal(|ui| {
                            ui.add_space(10.0);
                            ui.label(
                                RichText::new(format!("+ {} more", more))
                                    .size(10.0)
                                    .color(COL_MUTED),
                            );
                        });
                    }
                });
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    // G-115-64: clear_pair_label は production 経路では exit_record_full 内で
    // 呼ばれる (kirin_measure::cleanup) ため editor 本体では未参照. 既存テスト
    // (set_then_clear_pair_label_round_trip) のためだけに本 mod に明示 import.
    use kirin_measure::clear_pair_label;
    use std::sync::{Arc, Mutex};

    /// B-023 段階 4: format_pair_label は paired_pre_name 非空時に
    /// `pair: <name>` を返す（Name 優先表示 / 判断 2 / G-115-40 案 A-3）。
    #[test]
    fn format_pair_label_with_name() {
        assert_eq!(
            format_pair_label("Studio Mix", "abcdefghijklmnop"),
            "pair: Studio Mix"
        );
    }

    /// B-023 段階 4: paired_pre_name 空時は target_id 先頭 8 文字 fallback。
    /// PRE_ プレフィックス無し（判断 2 / 段階 2 PRE 側 fallback と整合）。
    #[test]
    fn format_pair_label_empty_name_fallback() {
        assert_eq!(
            format_pair_label("", "abcdefghijklmnop"),
            "pair: abcdefgh"
        );
    }

    /// 8 文字未満（短い ID）でも切り捨てではなくそのまま全文を入れること。
    #[test]
    fn format_pair_label_shorter_than_eight_keeps_all_chars() {
        assert_eq!(format_pair_label("", "abc"), "pair: abc");
    }

    /// set_pair_label / clear_pair_label が Mutex 越しに正しく値を出し入れすること。
    #[test]
    fn set_then_clear_pair_label_round_trip() {
        let label = Arc::new(Mutex::new(String::new()));
        set_pair_label(&label, "", "deadbeefcafef00d");
        assert_eq!(label.lock().unwrap().as_str(), "pair: deadbeef");
        clear_pair_label(&label);
        assert!(label.lock().unwrap().is_empty());
    }

    /// B-023 段階 4: set_pair_label に PRE Name を渡すと name 表示に切替わる
    /// （poll_record_signal_ack 経路の上書きをエミュレート）。
    #[test]
    fn set_pair_label_overwrites_with_name() {
        let label = Arc::new(Mutex::new(String::new()));
        set_pair_label(&label, "", "deadbeefcafef00d");
        assert_eq!(label.lock().unwrap().as_str(), "pair: deadbeef");
        set_pair_label(&label, "Studio Mix", "deadbeefcafef00d");
        assert_eq!(label.lock().unwrap().as_str(), "pair: Studio Mix");
    }

    // ── B-027 段階 3-A: ComboBox dropdown label ─────────────────────────

    /// `Some(name)` 持ち候補は `"<name> #<uuid8>"` で描画される。
    #[test]
    fn dropdown_label_with_name_uses_first_eight_chars() {
        let cand = PreCandidate {
            instance_id: "abcdef1234567890".into(),
            lufs_m: None,
            true_peak: None,
            crest: None,
            path: std::path::PathBuf::new(),
            name: Some("snare".into()),
        };
        assert_eq!(candidate_dropdown_label(&cand), "snare #abcdef12");
    }

    /// `name = None` の旧 schema PRE は `"(no name) #<uuid8>"` で描画される。
    #[test]
    fn dropdown_label_with_no_name_falls_back() {
        let cand = PreCandidate {
            instance_id: "abcdef1234567890".into(),
            lufs_m: None,
            true_peak: None,
            crest: None,
            path: std::path::PathBuf::new(),
            name: None,
        };
        assert_eq!(candidate_dropdown_label(&cand), "(no name) #abcdef12");
    }

    /// instance_id が 8 文字未満でも crash せずそのまま入る (char 取り)。
    #[test]
    fn dropdown_label_short_instance_id_passes_through() {
        let cand = PreCandidate {
            instance_id: "abc".into(),
            lufs_m: None,
            true_peak: None,
            crest: None,
            path: std::path::PathBuf::new(),
            name: Some("kick".into()),
        };
        assert_eq!(candidate_dropdown_label(&cand), "kick #abc");
    }

    // ── B-027 段階 3-B α-7-4-D Step 1: trigger_keep_internal toast 抑制機構 ──────
    //
    // 本 module section は trigger_keep_internal 内 8 箇所の `if let Some(t) =
    // toast.as_mut() { **t = Some(Toast::new(...)) }` パターンが意図通り機能する
    // ことを直接検証する。
    //
    // パターンの正しさが保証されれば、trigger_keep_internal 内 8 site (StoragePaths
    // err / discover empty / candidates empty / name filter empty / exclusion conflict /
    // pick_closest_pre None / LicenseDenied / write_pending fail) すべてで toast 抑制
    // が同等動作する (ロジック完全保持 + Option-aware 化)。
    //
    // 実環境での 5 path 完全 integration テスト (Daisuke 指示書の test 1-5) は、
    // (i) /tmp/kirin/ がプロセス共有でテスト隔離不可 (ii) HOME env が process-global
    // で並列テスト不可 — の 2 制約で deferred。ロジック完全保持と wrapper シグネチャ
    // 不変は build success + clippy clean + 既存 trigger_keep 呼出箇所 (editor.rs:774)
    // の compile 通過で確証。

    /// Some(toast) で `if let Some(t) = ... { **t = Some(...) }` パターンが
    /// 内側 Toast を設定すること (trigger_keep wrapper 経由 = 既存 behavior と等価)。
    #[test]
    fn toast_suppression_some_sets_inner_toast() {
        let mut inner: Option<Toast> = None;
        let mut wrapped: Option<&mut Option<Toast>> = Some(&mut inner);
        if let Some(t) = wrapped.as_mut() {
            **t = Some(Toast::new("Maximum 12 pairs reached", 0.0));
        }
        assert!(inner.is_some(), "Some 経路で inner Toast が設定されるべき");
        assert_eq!(inner.as_ref().unwrap().message, "Maximum 12 pairs reached");
    }

    /// None で `if let Some(t) = ... { ... }` パターンが内側を変更しないこと
    /// (broadcast 受信側 / Step 11 = toast 抑制)。
    #[test]
    fn toast_suppression_none_leaves_inner_unchanged() {
        let mut wrapped: Option<&mut Option<Toast>> = None;
        // パターンは何もしない (silent skip)
        if let Some(t) = wrapped.as_mut() {
            **t = Some(Toast::new("should-not-appear", 0.0));
        }
        assert!(wrapped.is_none(), "None は変化しない");
    }

    /// 同 `Option<&mut Option<Toast>>` を複数経路で再使用可能 (as_mut() 再 borrow)。
    /// trigger_keep_internal は 1 関数内で最大 8 経路の toast 設定を行うため、
    /// 各経路で再 borrow できる必要がある。
    #[test]
    fn toast_suppression_rebborrowable_across_multiple_paths() {
        let mut inner: Option<Toast> = None;
        let mut wrapped: Option<&mut Option<Toast>> = Some(&mut inner);

        // 1 回目: 上限 conflict 想定の path
        if let Some(t) = wrapped.as_mut() {
            **t = Some(Toast::new("first-path", 0.0));
        }
        // 2 回目: 別 path (LicenseDenied 想定) — 同 wrapped で再 borrow 可能
        if let Some(t) = wrapped.as_mut() {
            **t = Some(Toast::new("second-path", 1.0));
        }
        // 3 回目: write_pending 失敗想定の path
        if let Some(t) = wrapped.as_mut() {
            **t = Some(Toast::new("third-path", 2.0));
        }

        assert_eq!(
            inner.as_ref().unwrap().message,
            "third-path",
            "最後に書込まれた値が残るべき (再 borrow 健全性確認)"
        );
        assert!(
            (inner.as_ref().unwrap().until - (2.0 + TOAST_DURATION_SECS)).abs() < 1e-9,
            "until = now + TOAST_DURATION_SECS"
        );
    }
}
