//!
//! hypha_gui 共有プリミティブを使用:
//! - 5状態 LED（Error/WatchBreathing/RecordStandby/RecordActive/Idle）
//! - 背景テクスチャ（菌糸 300×200 brightness 15% / 遅延 decode キャッシュ）
//! - flora_color 横線（#d4a043 暫定）
//! - 共通ウィジェット（value_row / fmt_val / tp_color）
//!
//! SS-7: SignalState に基づく表示切替。
//! - Active → 数値表示
//! - Bypassed / Inactive → 全項目 `---`（宣言ベース、即時切替）
//!
//! Record モード（recording=true）: 6 項目表示（LUFS-M/TP/Crest/PSR/N/Sharpness）。
//! Record ACK 成立時に「記録開始」バナーを 3 秒一時表示。
//! サブ1-C 時点では recording / record_acknowledged は default false（サブ2/3 で実配線）。

use crate::sanitize_name;
use hypha_gui::{
    derive_led_state, display_smoothing::DisplaySmoother, fmt_val, led_color, tp_color, val_color,
    BackgroundTexture, BG, COL_FLORA, COL_MUTED, COL_NORMAL,
};
use kirin_measure::{load_signal_state, MeasureResult, SignalState};
use nih_plug::prelude::Editor;
use nih_plug_egui::{
    create_egui_editor,
    egui::{self, Grid, Key, Label, RichText, Sense, Stroke, TextEdit, TextStyle, Vec2},
    EguiState,
};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, LazyLock, Mutex, RwLock};
use std::time::Duration;

/// 記録開始バナーの表示時間（秒）。
const RECORD_BANNER_DURATION_SECS: f64 = 3.0;

//
// Watch 3 項目 + Record 6 項目のラベル / 値 / 単位 3 セルに同一 hover を張る
// ことで、マウス位置による表示揺れを排除する（案 A2）。英文に続いて改行で

const HELP_LUFS_M: &str = "Momentary Loudness — ITU BS.1770 (LUFS)";
const HELP_TP: &str = "True Peak — inter-sample peak, ITU BS.1770 (dBTP)";
const HELP_CREST: &str = "Crest Factor — peak vs RMS difference (dB)";
const HELP_PSR: &str = "Peak to Short-term Loudness Ratio (dB)";
const HELP_N: &str = "Loudness — Zwicker model, ISO 532-1 (sone)";
const HELP_SHARP: &str = "Sharpness — high-frequency content weighting,\nDIN 45692 (acum)";

/// B-023 段階 2: Name 編集 TextEdit の egui focus ID。
///
/// `param_slider.rs:17-20` 同パターンで `LazyLock<egui::Id>` 静的定義
/// (毎フレーム `Id::new(...)` を呼ばない / 同一 ID で memory 経由 focus 状態を判定)。
static NAME_FOCUS_ID: LazyLock<egui::Id> = LazyLock::new(|| egui::Id::new("hypha_pre_name_edit"));

/// B-023 段階 2: Name 編集モード判定 (egui memory ベース)。
///
/// `editing: bool` フィールドを別管理せず egui memory `has_focus(id)` に委ねる
/// (param_slider.rs:96-98 `keyboard_entry_active` 同パターン)。
fn name_edit_active(ui: &egui::Ui) -> bool {
    ui.memory(|mem| mem.has_focus(*NAME_FOCUS_ID))
}

// ── エディタ状態（user_state） ───────────────────────────────────────────

/// PRE エディタの共有状態 + エディタ内部のトランジェント状態。
pub struct PreEditorState {
    pub measure: Arc<Mutex<MeasureResult>>,
    pub measure_alive: Arc<AtomicBool>,
    pub signal_state: Arc<AtomicU8>,
    /// Record モード中か（サブ2 でボタン操作から設定）
    pub recording: Arc<AtomicBool>,
    /// Record 信号が ACK 済か（PRE は常に true と見なしてよい）
    pub record_acknowledged: Arc<AtomicBool>,
    /// preset/*.json が 1 件以上存在するか（POST IO Thread が更新、PRE は読むだけ）。
    pub preset_available: Arc<AtomicBool>,

    /// B-023 段階 2: ユーザー定義 Name (HyphaPreParams.name と Arc 共有)。
    /// editor 描画時に毎フレーム `read()` で最新値を取り、編集確定時に
    /// `write()` で sanitize 後の値を書き込む。
    pub name: Arc<RwLock<String>>,
    /// B-023 段階 2: PRE instance UUID (HyphaPreParams.instance_id と Arc 共有)。
    /// Name 未設定時の表示 fallback として「先頭 8 文字」を出す (論点 2 (a))。
    pub instance_id: Arc<RwLock<String>>,
    /// io_thread が正当に Record を閉じた理由を表示する通知文字列。`None` =
    /// 通常 / `Some` のとき GUI 末尾にステータス行表示 (R-26 沈黙ゲート / 通常時非表示)。
    /// B-245 以降、writer flush failure はここへ書かず Record を維持する。
    pub record_error_message: Arc<RwLock<Option<String>>>,

    // ── エディタローカル（egui state 内で保持） ──────────────────────
    /// 背景テクスチャの遅延 decode キャッシュ
    bg: BackgroundTexture,
    /// 前フレームの ACK 値（false→true エッジ検出）
    prev_ack: bool,
    /// 「記録開始」バナーの終了時刻（`ctx.input(|i| i.time)` 基準）
    banner_until: Option<f64>,
    /// 直前フレームの LED 状態（edge-triggered log 用）。
    prev_led: Option<hypha_gui::LedState>,
    /// B-023 段階 2: クリック起動編集モードの入力 buffer。
    /// 編集モード開始時に現在 Name を copy、changed() で sanitize_name 適用、
    /// Enter で `name.write()` に書き戻し、Escape で discard。
    edit_buffer: String,
    /// GUI 表示専用の安定化フィルタ。Record/TRACE の raw 計測値には触れない。
    display_smoother: DisplaySmoother,
}

impl PreEditorState {
    #[allow(clippy::too_many_arguments)]
    fn new(
        measure: Arc<Mutex<MeasureResult>>,
        measure_alive: Arc<AtomicBool>,
        signal_state: Arc<AtomicU8>,
        recording: Arc<AtomicBool>,
        record_acknowledged: Arc<AtomicBool>,
        preset_available: Arc<AtomicBool>,
        name: Arc<RwLock<String>>,
        instance_id: Arc<RwLock<String>>,
        record_error_message: Arc<RwLock<Option<String>>>,
    ) -> Self {
        Self {
            measure,
            measure_alive,
            signal_state,
            recording,
            record_acknowledged,
            preset_available,
            name,
            instance_id,
            record_error_message,
            bg: BackgroundTexture::new(),
            prev_ack: false,
            banner_until: None,
            prev_led: None,
            edit_buffer: String::new(),
            display_smoother: DisplaySmoother::default(),
        }
    }
}

// ── 公開エントリポイント ─────────────────────────────────────────────────

/// PRE エディタを生成して返す。`Plugin::editor()` から呼ぶ。
#[allow(clippy::too_many_arguments)]
pub fn create_pre_editor(
    egui_state: Arc<EguiState>,
    measure: Arc<Mutex<MeasureResult>>,
    measure_alive: Arc<AtomicBool>,
    signal_state: Arc<AtomicU8>,
    recording: Arc<AtomicBool>,
    record_acknowledged: Arc<AtomicBool>,
    preset_available: Arc<AtomicBool>,
    name: Arc<RwLock<String>>,
    instance_id: Arc<RwLock<String>>,
    record_error_message: Arc<RwLock<Option<String>>>,
) -> Option<Box<dyn Editor>> {
    create_egui_editor(
        egui_state,
        PreEditorState::new(
            measure,
            measure_alive,
            signal_state,
            recording,
            record_acknowledged,
            preset_available,
            name,
            instance_id,
            record_error_message,
        ),
        |ctx, state| {
            let mut visuals = ctx.style().visuals.clone();
            visuals.panel_fill = BG;
            visuals.window_fill = BG;
            ctx.set_visuals(visuals);
            // build は毎 spawn（= 新 Context 作成時）に 1 回実行される
            // （egui_baseview window.rs:128 で Context::default() + 170 で build 実行）。
            // close/reopen 時に古い TextureHandle を抱えたままにならないよう bg を再生成する。
            state.bg = BackgroundTexture::new();
        },
        |ctx, _setter, state| {
            let sig = load_signal_state(&state.signal_state);
            let raw_m = state.measure.lock().map(|g| g.clone()).unwrap_or_default();
            let alive = state.measure_alive.load(Ordering::Relaxed);
            let recording = state.recording.load(Ordering::Relaxed);
            let ack = state.record_acknowledged.load(Ordering::Relaxed);

            // Record ACK 成立エッジで banner 発動
            let now = ctx.input(|i| i.time);
            if ack && !state.prev_ack {
                state.banner_until = Some(now + RECORD_BANNER_DURATION_SECS);
            }
            state.prev_ack = ack;
            let show_banner = state.banner_until.is_some_and(|until| now < until);
            if !show_banner {
                state.banner_until = None;
            }

            let (m, show_values, values_muted) = match sig {
                SignalState::Active => (
                    state.display_smoother.update_measure(&raw_m, now),
                    true,
                    false,
                ),
                SignalState::Inactive => match state.display_smoother.held_measure_display(now) {
                    Some(held) => (held.value, true, held.muted),
                    None => (raw_m, false, false),
                },
                SignalState::Bypassed => {
                    state.display_smoother.reset();
                    (raw_m, false, false)
                }
            };

            let preset_available = state.preset_available.load(Ordering::Relaxed);
            let led = derive_led_state(alive, sig, recording, ack, preset_available);
            if state.prev_led != Some(led) {
                log::info!("[led] state: {:?}", led);
                state.prev_led = Some(led);
            }
            let led_col = led_color(led, now);

            draw_pre(
                ctx,
                state,
                &m,
                recording,
                led_col,
                PreDisplayFlags {
                    show_banner,
                    show_values,
                    values_muted,
                },
            );
        },
    )
}

// ── 描画 ────────────────────────────────────────────────────────────────

struct PreDisplayFlags {
    show_banner: bool,
    show_values: bool,
    values_muted: bool,
}

fn draw_pre(
    ctx: &egui::Context,
    state: &mut PreEditorState,
    m: &MeasureResult,
    recording: bool,
    led_col: egui::Color32,
    flags: PreDisplayFlags,
) {
    egui::CentralPanel::default()
        .frame(egui::Frame::NONE.fill(BG))
        .show(ctx, |ui| {
            // 背景テクスチャ（panel_fill の上に重ねて敷く）
            state.bg.paint(ctx, ui);

            ui.add_space(8.0);

            // タイトル行: 左に「PRE」+ Name (クリック起動編集) / 右端に LED
            ui.horizontal(|ui| {
                ui.add_space(10.0);
                ui.label(RichText::new("PRE").size(20.0).color(COL_NORMAL));
                ui.add_space(6.0);
                draw_name_field(ui, state);
                // 右端まで伸ばして LED を右寄せ
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.add_space(10.0);
                    draw_led(ui, led_col);
                });
            });
            ui.add_space(4.0);

            // flora_color 横線（菌糸の先端を示すアクセント）
            ui.horizontal(|ui| {
                ui.add_space(10.0);
                let w = ui.available_width() - 10.0;
                let (rect, _) = ui.allocate_exact_size(Vec2::new(w, 1.0), egui::Sense::hover());
                ui.painter()
                    .hline(rect.x_range(), rect.center().y, Stroke::new(1.0, COL_FLORA));
            });
            ui.add_space(6.0);

            // Record モード or Watch モードで表示項目を切替
            if recording {
                draw_record_grid(ui, m, flags.show_values, flags.values_muted);
            } else {
                draw_watch_grid(ui, m, flags.show_values, flags.values_muted);
            }

            // "Keeping" バナー（ACK 直後 3 秒のみ / Keep ボタンとの統一感・R-28 静かなニュアンス）
            if flags.show_banner {
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

            // io_thread が正当に Record を閉じた時のステータス行
            // (R-26 沈黙ゲート: 通常時 None なので非表示)。
            // writer flush failure は Record 停止権限を持たず、ここには出さない。
            let err_msg = state
                .record_error_message
                .read()
                .ok()
                .and_then(|g| g.clone());
            if let Some(msg) = err_msg {
                ui.add_space(4.0);
                ui.horizontal(|ui| {
                    ui.add_space(10.0);
                    ui.label(RichText::new(msg).size(11.0).color(COL_MUTED).monospace());
                });
            }

            // 100ms ごとに再描画（10fps）。呼吸アニメのため banner 表示中は 30fps に引き上げ。
            let repaint_ms = if flags.show_banner { 33 } else { 100 };
            ctx.request_repaint_after(Duration::from_millis(repaint_ms));
        });
}

/// B-023 段階 2: Name フィールド描画（クリック起動編集 / Enter 確定 / Escape 破棄）。
///
/// 通常モード: `Label::new(...).sense(Sense::click())` で表示。クリックで `request_focus`。
/// 編集モード (`name_edit_active` = `memory.has_focus(*NAME_FOCUS_ID)`):
///   - `TextEdit::singleline(&mut state.edit_buffer).id(*NAME_FOCUS_ID)`
///   - `changed()` 時にリアルタイム sanitize（不正入力を即座に剥がす）
///   - `Enter` (lost_focus + key_pressed) で sanitize 後 `name.write()` 確定
///   - `Escape` で discard（buffer 反映せず focus 解除）
///
/// Name 空 + 通常モード時は instance_id 先頭 8 文字を fallback 表示。
fn draw_name_field(ui: &mut egui::Ui, state: &mut PreEditorState) {
    if name_edit_active(ui) {
        let response = ui.add(
            TextEdit::singleline(&mut state.edit_buffer)
                .id(*NAME_FOCUS_ID)
                .desired_width(120.0)
                .font(TextStyle::Monospace),
        );
        if ui.input(|i| i.key_pressed(Key::Escape)) {
            ui.memory_mut(|mem| mem.surrender_focus(*NAME_FOCUS_ID));
        } else if response.lost_focus() && ui.input(|i| i.key_pressed(Key::Enter)) {
            let sanitized = sanitize_name(&state.edit_buffer);
            if let Ok(mut g) = state.name.write() {
                *g = sanitized;
            }
            ui.memory_mut(|mem| mem.surrender_focus(*NAME_FOCUS_ID));
        } else if response.changed() {
            state.edit_buffer = sanitize_name(&state.edit_buffer);
        }
    } else {
        let raw_name = state
            .name
            .read()
            .ok()
            .map(|g| g.clone())
            .unwrap_or_default();
        let display = if raw_name.is_empty() {
            let iid = state
                .instance_id
                .read()
                .ok()
                .map(|g| g.clone())
                .unwrap_or_default();
            iid.chars().take(8).collect::<String>()
        } else {
            raw_name.clone()
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
            state.edit_buffer = raw_name;
            ui.memory_mut(|mem| mem.request_focus(*NAME_FOCUS_ID));
        }
    }
}

fn value_color(v: Option<f64>, is_tp: bool, muted: bool) -> egui::Color32 {
    if muted {
        COL_MUTED
    } else if is_tp {
        tp_color(v)
    } else {
        val_color(v)
    }
}

/// Watch モード: 3 項目（LUFS-M / TP / Crest）。
///
fn draw_watch_grid(ui: &mut egui::Ui, m: &MeasureResult, show_values: bool, muted: bool) {
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        Grid::new("pre_watch_vals")
            .num_columns(3)
            .min_col_width(58.0)
            .spacing([6.0, 6.0])
            .show(ui, |ui| {
                if show_values {
                    value_row_with_hover(
                        ui,
                        "LUFS-M",
                        fmt_val(m.lufs_m),
                        "LUFS",
                        value_color(m.lufs_m, false, muted),
                        HELP_LUFS_M,
                    );
                    value_row_with_hover(
                        ui,
                        "TP",
                        fmt_val(m.true_peak),
                        "dBTP",
                        value_color(m.true_peak, true, muted),
                        HELP_TP,
                    );
                    value_row_with_hover(
                        ui,
                        "Crest",
                        fmt_val(m.crest),
                        "dB",
                        value_color(m.crest, false, muted),
                        HELP_CREST,
                    );
                } else {
                    value_row_with_hover(
                        ui,
                        "LUFS-M",
                        "---".to_string(),
                        "LUFS",
                        COL_MUTED,
                        HELP_LUFS_M,
                    );
                    value_row_with_hover(ui, "TP", "---".to_string(), "dBTP", COL_MUTED, HELP_TP);
                    value_row_with_hover(
                        ui,
                        "Crest",
                        "---".to_string(),
                        "dB",
                        COL_MUTED,
                        HELP_CREST,
                    );
                }
            });
    });
}

/// Record モード: 6 項目（LUFS-M / TP / Crest / PSR / N / Sharpness）。
/// 2 列 × 3 行で配置。
///
fn draw_record_grid(ui: &mut egui::Ui, m: &MeasureResult, show_values: bool, muted: bool) {
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        Grid::new("pre_record_vals")
            .num_columns(6)
            .min_col_width(40.0)
            .spacing([6.0, 4.0])
            .show(ui, |ui| {
                if show_values {
                    row_pair(
                        ui,
                        (
                            "LUFS-M",
                            fmt_val(m.lufs_m),
                            "LUFS",
                            value_color(m.lufs_m, false, muted),
                            HELP_LUFS_M,
                        ),
                        (
                            "PSR",
                            fmt_val(m.psr),
                            "dB",
                            value_color(m.psr, false, muted),
                            HELP_PSR,
                        ),
                    );
                    row_pair(
                        ui,
                        (
                            "TP",
                            fmt_val(m.true_peak),
                            "dBTP",
                            value_color(m.true_peak, true, muted),
                            HELP_TP,
                        ),
                        (
                            "N",
                            fmt_val(m.n_prime_total),
                            "sone",
                            value_color(m.n_prime_total, false, muted),
                            HELP_N,
                        ),
                    );
                    row_pair(
                        ui,
                        (
                            "Crest",
                            fmt_val(m.crest),
                            "dB",
                            value_color(m.crest, false, muted),
                            HELP_CREST,
                        ),
                        (
                            "Sharp",
                            fmt_val(m.sharpness),
                            "acum",
                            value_color(m.sharpness, false, muted),
                            HELP_SHARP,
                        ),
                    );
                } else {
                    row_pair(
                        ui,
                        ("LUFS-M", "---".to_string(), "LUFS", COL_MUTED, HELP_LUFS_M),
                        ("PSR", "---".to_string(), "dB", COL_MUTED, HELP_PSR),
                    );
                    row_pair(
                        ui,
                        ("TP", "---".to_string(), "dBTP", COL_MUTED, HELP_TP),
                        ("N", "---".to_string(), "sone", COL_MUTED, HELP_N),
                    );
                    row_pair(
                        ui,
                        ("Crest", "---".to_string(), "dB", COL_MUTED, HELP_CREST),
                        ("Sharp", "---".to_string(), "acum", COL_MUTED, HELP_SHARP),
                    );
                }
            });
    });
}

/// B-032: PRE Watch 用の hover 付き value_row（PRE 内 private）。
///
/// `hypha_gui::common::value_row` (pub API) は POST editor からも参照されており、
/// crate 外 pub API シグネチャ不変（申し送り #32）の制約下で hover を追加する
/// ため、PRE editor 内に同型 helper を新設する（POST 側は B-032 スコープ外）。
/// ラベル / 値 / 単位の 3 セル全部に同一 hover を張り、マウス位置による表示
/// 揺れを排除する。
fn value_row_with_hover(
    ui: &mut egui::Ui,
    label: &str,
    value: String,
    unit: &str,
    color: egui::Color32,
    hover_text: &str,
) {
    ui.label(RichText::new(label).size(13.0).color(COL_MUTED))
        .on_hover_text(hover_text);
    ui.label(RichText::new(value).size(16.0).color(color).monospace())
        .on_hover_text(hover_text);
    ui.label(RichText::new(unit).size(11.0).color(COL_MUTED))
        .on_hover_text(hover_text);
    ui.end_row();
}

/// 2 つの (label, value, unit, color, hover_text) をグリッド 1 行に流し込む。
///
/// B-032: タプル末尾に `hover_text: &str` を追加。各セル（左右計 6 セル）に
/// 同一 hover を張ることでマウス位置による表示揺れを排除する。
fn row_pair(
    ui: &mut egui::Ui,
    left: (&str, String, &str, egui::Color32, &str),
    right: (&str, String, &str, egui::Color32, &str),
) {
    // 左
    ui.label(RichText::new(left.0).size(11.0).color(COL_MUTED))
        .on_hover_text(left.4);
    ui.label(RichText::new(left.1).size(14.0).color(left.3).monospace())
        .on_hover_text(left.4);
    ui.label(RichText::new(left.2).size(10.0).color(COL_MUTED))
        .on_hover_text(left.4);
    // 右
    ui.label(RichText::new(right.0).size(11.0).color(COL_MUTED))
        .on_hover_text(right.4);
    ui.label(RichText::new(right.1).size(14.0).color(right.3).monospace())
        .on_hover_text(right.4);
    ui.label(RichText::new(right.2).size(10.0).color(COL_MUTED))
        .on_hover_text(right.4);
    ui.end_row();
}

/// LED を 1 点描画（タイトル右端配置）。
fn draw_led(ui: &mut egui::Ui, color: egui::Color32) {
    let (rect, _) = ui.allocate_exact_size(Vec2::splat(12.0), egui::Sense::hover());
    ui.painter().circle_filled(rect.center(), 5.0, color);
}
