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
    append_annotation_to_latest, check_record_exclusion, discover_active_pre_dir,
    filter_candidates_by_name, format_pair_label, load_signal_state, lookup_section_label,
    mark_released, pick_closest_pre, sanitize_name, scan_latest_v2_preset, scan_pre_candidates_in,
    show_note_button, show_save_button, show_stop_record_button, write_pending, DeltaMode,
    DeltaResult, ExclusionResult, License, MeasureResult, PluginDataRole, PostMetrics,
    PresetFileV2, RecordStateMachine, SignalState, StoragePaths, TransitionError,
    SENSE_RECORD_HINT, SENSE_UPSELL_URL,
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

use crate::read_instance_id_arc;

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
struct Toast {
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
    pub project_hash: String,
    /// プロセス単位 `daw_session_id`（record_signal content の cross-process 防壁）。
    pub daw_session_id: String,
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
    pub project_hash: String,
    /// プロセス単位 `daw_session_id`（A-3 修正後 / Q1 補強）。
    pub daw_session_id: String,
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

            // B-027 段階 2: タイトル行直下に pair PRE Name 入力欄 (PRE 同パターン)。
            ui.horizontal(|ui| {
                ui.add_space(10.0);
                draw_pair_pre_name_field(ui, state);
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
                                draw_watch_absolute_grid(ui, m);
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
                        ui.label(
                            RichText::new(&t.message)
                                .size(11.0)
                                .color(COL_MUTED)
                                .monospace(),
                        );
                    });
                }
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

/// Record: 2 列 × 3 行。左=Δ 3 項目 / 右=絶対値 3 項目（LUFS-M / TP / Crest）。
/// NoPre 時は Δ 列が自動的に `---`（d.lufs/tp/crest が None）、右列は POST 絶対値で埋める。
fn draw_record_section(ui: &mut egui::Ui, m: &MeasureResult, d: &DeltaResult) {
    let delta_col = match d.mode {
        DeltaMode::Active => COL_NORMAL,
        DeltaMode::Stale => COL_MUTED,
        DeltaMode::NoPre => COL_MUTED,
    };
    let tp_warn = tp_over(m.true_peak);
    let tp_abs_col = if tp_warn {
        COL_FLORA_BRIGHT
    } else {
        val_color(m.true_peak)
    };
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        Grid::new("post_record_vals")
            .num_columns(6)
            .min_col_width(40.0)
            .spacing([6.0, 4.0])
            .show(ui, |ui| {
                let lufs_col = if d.lufs.is_some() { delta_col } else { COL_MUTED };
                row_pair(
                    ui,
                    ("ΔLUFS", fmt_delta(d.lufs), "LU", lufs_col),
                    ("LUFS-M", fmt_val(m.lufs_m), "LUFS", val_color(m.lufs_m)),
                );
                let tp_col = if d.tp.is_some() {
                    if tp_warn { COL_FLORA_BRIGHT } else { delta_col }
                } else {
                    COL_MUTED
                };
                row_pair(
                    ui,
                    ("ΔTP", fmt_delta(d.tp), "dB", tp_col),
                    ("TP", fmt_val(m.true_peak), "dBTP", tp_abs_col),
                );
                let crest_col = if d.crest.is_some() { delta_col } else { COL_MUTED };
                row_pair(
                    ui,
                    ("ΔCrest", fmt_delta(d.crest), "dB", crest_col),
                    ("Crest", fmt_val(m.crest), "dB", val_color(m.crest)),
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
    let instance_id = read_instance_id_arc(&state.instance_id);
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
                            &state.project_hash,
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
                        &state.project_hash,
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
                        &state.project_hash,
                        &state.daw_session_id,
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

/// pair_label をクリア（Watch 復帰時 / Record 失敗時）。
fn clear_pair_label(pair_label: &Arc<Mutex<String>>) {
    if let Ok(mut g) = pair_label.lock() {
        g.clear();
    }
}

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
    // 1. ストレージパス解決
    let paths = match StoragePaths::default_macos() {
        Ok(p) => p,
        Err(e) => {
            log::warn!("[POST keep] StoragePaths error: {:?}", e);
            *toast = Some(Toast::new("Kirin OS not installed", now));
            return;
        }
    };
    let plugin_data_dir = paths.plugin_data_dir();
    let tmp_base = std::env::temp_dir().join("kirin");

    // 2. PRE 候補スキャン（B-021 Phase 1A 追加修正）
    //
    // 旧: `scan_pre_candidates(&tmp_base, project_hash)` で POST 自身の
    //     `project_hash` 配下のみ scan。cdylib 隔離下で PRE と project_uuid が
    //     乖離しているため空 → 「No PRE plugin found」誤表示の根本。
    // 新: `discover_active_pre_dir` で kirin_root 横断走査して active な
    //     `{project_uuid}/` dir を採用 → そこを `scan_pre_candidates_in` で読む。
    //     Δ 計算経路 (io_thread_post::run_tick) と同じ discovery を使うため、
    //     単一 PRE 前提 (guardian_50 §G-50-35) では同一 PRE が選ばれる。
    let pre_project_dir = match discover_active_pre_dir(&tmp_base) {
        Some(p) => p,
        None => {
            log::info!("[POST keep] no PRE candidate (discovery returned None)");
            *toast = Some(Toast::new("No PRE plugin found", now));
            return;
        }
    };
    let candidates = scan_pre_candidates_in(&pre_project_dir);
    if candidates.is_empty() {
        log::info!(
            "[POST keep] no PRE candidate (project_dir={} empty)",
            pre_project_dir.display()
        );
        *toast = Some(Toast::new("No PRE plugin found", now));
        return;
    }

    // B-027 段階 2: pair_pre_name 非空時は Name で filter。空文字は pass-through
    // (filter_candidates_by_name 内部で判定 / B-027 段階 1 受入維持)。
    let candidates = filter_candidates_by_name(candidates, pair_pre_name);
    if candidates.is_empty() {
        log::info!(
            "[POST keep] no PRE matches pair_pre_name=\"{}\" (project_dir={})",
            pair_pre_name,
            pre_project_dir.display()
        );
        *toast = Some(Toast::new("No matching PRE", now));
        return;
    }

    // 3. 排他チェック（A-3 修正後: bus 引数なし、project_hash 全体で 1 record）
    match check_record_exclusion(&plugin_data_dir, project_hash) {
        ExclusionResult::Ok => {}
        ExclusionResult::Conflict { role, heartbeat, .. } => {
            log::info!(
                "[POST keep] exclusion conflict: role={:?} heartbeat={}",
                role, heartbeat
            );
            *toast = Some(Toast::new("Another recording is active", now));
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
            *toast = Some(Toast::new("No PRE plugin found", now));
            return;
        }
    };

    // 5. RecordStateMachine 遷移（license 二重 gate）
    match record_sm.try_enter_record(license) {
        Ok(()) => {}
        Err(TransitionError::LicenseDenied) => {
            log::info!("[POST keep] license denied: {:?}", license);
            *toast = Some(Toast::new("Record requires Kirin OS license", now));
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
            record_sm.exit_record();
            clear_pair_label(pair_label);
            if let Ok(mut g) = paired_pre_target.lock() {
                *g = None;
            }
            *toast = Some(Toast::new("Failed to start record", now));
        }
    }
}

/// Stop タップ: Watch へ戻し、record_signal を released に更新。pair_label をクリア。
/// `paired_pre_target` も None に戻す（v1.2 (a) 次の Keep 待ち状態）。
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
    record_sm.exit_record();
    clear_pair_label(pair_label);
    if let Ok(mut g) = paired_pre_target.lock() {
        *g = None;
    }
    match StoragePaths::default_macos() {
        Ok(paths) => match mark_released(&paths.plugin_data_dir(), project_hash, instance_id) {
            Ok(true) => log::info!("[POST stop] mark_released ok"),
            Ok(false) => log::info!("[POST stop] no signal to release"),
            Err(e) => {
                log::warn!("[POST stop] mark_released failed: {}", e);
                *toast = Some(Toast::new("Record stop error", now));
            }
        },
        Err(e) => {
            log::warn!("[POST stop] StoragePaths error: {:?}", e);
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
    state.latest_proposals = scan_latest_v2_preset(
        &paths.plugin_data_dir(),
        &state.project_hash,
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
}
