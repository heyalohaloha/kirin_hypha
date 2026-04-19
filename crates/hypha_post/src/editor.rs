//! POST GUI — 300×200px（guardian_53 T-6 / サブ2-B）。
//!
//! hypha_gui 共有プリミティブを使用:
//! - 5状態 LED（Error/WatchBreathing/RecordStandby/RecordActive/Idle）
//! - 背景テクスチャ（菌糸 300×200 brightness 15% / 遅延 decode キャッシュ）
//! - flora_color 横線（#d4a043 暫定）
//! - 共通ウィジェット（value_row / fmt_val / fmt_delta / tp_color）
//!
//! SS-7: SignalState に基づく表示切替。
//! - POST Active + PRE Active → Δ表示
//! - POST Active + PRE 非Active（NoPre）→ Δ列は `---`
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
    append_annotation_to_latest, check_record_exclusion, load_signal_state, mark_released,
    pick_closest_pre, scan_pre_candidates, show_note_button, show_save_button,
    show_stop_record_button, write_pending, DeltaMode, DeltaResult, ExclusionResult, License,
    MeasureResult, PluginDataRole, PostMetrics, RecordStateMachine, SignalState, StoragePaths,
    TransitionError, BUS_PHASE1, PROJECT_HASH_PHASE1, SENSE_RECORD_HINT, SENSE_UPSELL_URL,
};
use nih_plug::prelude::Editor;
use nih_plug_egui::{
    create_egui_editor,
    egui::{self, Grid, RichText, Stroke, Vec2},
    EguiState,
};
use std::sync::atomic::{AtomicBool, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::time::Duration;

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
    pub instance_id: String,
    pub measure: Arc<Mutex<MeasureResult>>,
    pub delta: Arc<Mutex<DeltaResult>>,
    pub measure_alive: Arc<AtomicBool>,
    pub signal_state: Arc<AtomicU8>,
    /// Record 状態機械（サブ2-B: ボタンから操作）。
    pub record_sm: Arc<RecordStateMachine>,
    /// Record 信号が PRE から ACK されたか（false = Standby, true = Active）
    pub record_acknowledged: Arc<AtomicBool>,
    /// ペアリング表示用ラベル（例: "PRE abc12345…"）。空文字 → `---`。
    /// サブ3 で IO Thread が更新する。
    pub pair_label: Arc<Mutex<String>>,
    /// license 値（起動時に読込済。サブ2-A 範囲では不変）。
    pub license: Arc<License>,

    // ── エディタローカル ───────────────────────────────────────────────
    bg: BackgroundTexture,
    prev_ack: bool,
    banner_until: Option<f64>,
    toast: Option<Toast>,
    /// サブ2-C: Note タップ後の 3 タグ選択行を表示中か。
    /// Record 中のみ有効。非 Record 時は毎フレーム false に戻される。
    note_picker_open: bool,
}

impl PostEditorState {
    #[allow(clippy::too_many_arguments)]
    fn new(
        instance_id: String,
        measure: Arc<Mutex<MeasureResult>>,
        delta: Arc<Mutex<DeltaResult>>,
        measure_alive: Arc<AtomicBool>,
        signal_state: Arc<AtomicU8>,
        record_sm: Arc<RecordStateMachine>,
        record_acknowledged: Arc<AtomicBool>,
        pair_label: Arc<Mutex<String>>,
        license: Arc<License>,
    ) -> Self {
        Self {
            instance_id,
            measure,
            delta,
            measure_alive,
            signal_state,
            record_sm,
            record_acknowledged,
            pair_label,
            license,
            bg: BackgroundTexture::new(),
            prev_ack: false,
            banner_until: None,
            toast: None,
            note_picker_open: false,
        }
    }
}

// ── 公開エントリポイント ─────────────────────────────────────────────────

#[allow(clippy::too_many_arguments)]
pub fn create_post_editor(
    egui_state: Arc<EguiState>,
    instance_id: String,
    measure: Arc<Mutex<MeasureResult>>,
    delta: Arc<Mutex<DeltaResult>>,
    measure_alive: Arc<AtomicBool>,
    signal_state: Arc<AtomicU8>,
    record_sm: Arc<RecordStateMachine>,
    record_acknowledged: Arc<AtomicBool>,
    pair_label: Arc<Mutex<String>>,
    license: Arc<License>,
) -> Option<Box<dyn Editor>> {
    create_egui_editor(
        egui_state,
        PostEditorState::new(
            instance_id,
            measure,
            delta,
            measure_alive,
            signal_state,
            record_sm,
            record_acknowledged,
            pair_label,
            license,
        ),
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
            if ack && !state.prev_ack {
                state.banner_until = Some(now + RECORD_BANNER_DURATION_SECS);
            }
            state.prev_ack = ack;
            let show_banner = state.banner_until.is_some_and(|until| now < until);
            if !show_banner {
                state.banner_until = None;
            }

            let led = derive_led_state(alive, sig, recording, ack);
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
                ui.add_space(6.0);
                pairing_label(ui, pair);
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.add_space(10.0);
                    draw_led(ui, led_col);
                });
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
                        let delta_col = match d.mode {
                            DeltaMode::Active => COL_NORMAL,
                            DeltaMode::Stale => COL_MUTED,
                            DeltaMode::NoPre => COL_MUTED,
                        };
                        let tp_warn = tp_over(m.true_peak);
                        draw_delta_grid(ui, d, delta_col, tp_warn);
                        ui.add_space(4.0);
                        draw_button_row(ui, false, license, state, m, now);
                    }
                }
            }

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

/// Watch: Δ値のみ（3 項目）。NoPre 時は d.lufs/tp/crest が None → `fmt_delta` が `---` を返す。
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

// ── ボタン行 ──────────────────────────────────────────────────────────────

fn draw_button_row(
    ui: &mut egui::Ui,
    recording: bool,
    license: License,
    state: &mut PostEditorState,
    m: &MeasureResult,
    now: f64,
) {
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        if recording {
            if state.note_picker_open {
                // サブ2-C: Note picker — [Good] [Fix] [Hold] [Cancel]
                for tag in ["Good", "Fix", "Hold"] {
                    if ui.button(tag).clicked() {
                        log::info!("[hypha-fork] button clicked: {}", tag);
                        trigger_note_save(tag, &mut state.toast, now);
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
                    trigger_stop(&state.record_sm, &mut state.toast, now);
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
                    trigger_keep(
                        license,
                        &state.record_sm,
                        &state.instance_id,
                        m,
                        &mut state.toast,
                        now,
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

/// Keep タップ: PRE 候補 / 排他 / license を事前チェック → `try_enter_record` → `write_pending`。
fn trigger_keep(
    license: License,
    record_sm: &Arc<RecordStateMachine>,
    instance_id: &str,
    m: &MeasureResult,
    toast: &mut Option<Toast>,
    now: f64,
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

    // 2. PRE 候補スキャン
    let candidates = scan_pre_candidates(&tmp_base, PROJECT_HASH_PHASE1, BUS_PHASE1);
    if candidates.is_empty() {
        log::info!("[POST keep] no PRE candidate");
        *toast = Some(Toast::new("No PRE plugin on this bus", now));
        return;
    }

    // 3. 排他チェック
    match check_record_exclusion(&plugin_data_dir, PROJECT_HASH_PHASE1, BUS_PHASE1) {
        ExclusionResult::Ok => {}
        ExclusionResult::Conflict { role, heartbeat, .. } => {
            log::info!(
                "[POST keep] exclusion conflict: role={:?} heartbeat={}",
                role, heartbeat
            );
            *toast = Some(Toast::new("This bus already has an active recording", now));
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
            // candidates 非空なら必ず Some が返るはずだが保険
            log::warn!("[POST keep] pick_closest_pre returned None despite candidates");
            *toast = Some(Toast::new("No PRE plugin on this bus", now));
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

    // 6. record_signal.json を pending で書き込み
    match write_pending(
        &plugin_data_dir,
        PROJECT_HASH_PHASE1,
        BUS_PHASE1,
        instance_id.to_string(),
        target_id.clone(),
    ) {
        Ok(_) => {
            log::info!(
                "[POST keep] write_pending ok: requested_by={} target={}",
                instance_id, target_id
            );
        }
        Err(e) => {
            log::warn!("[POST keep] write_pending failed: {}", e);
            // rollback
            record_sm.exit_record();
            *toast = Some(Toast::new("Failed to start record", now));
        }
    }
}

/// Stop タップ: Watch へ戻し、record_signal.json を released に更新。
fn trigger_stop(record_sm: &Arc<RecordStateMachine>, toast: &mut Option<Toast>, now: f64) {
    record_sm.exit_record();
    match StoragePaths::default_macos() {
        Ok(paths) => {
            match mark_released(&paths.plugin_data_dir(), PROJECT_HASH_PHASE1, BUS_PHASE1) {
                Ok(true) => log::info!("[POST stop] mark_released ok"),
                Ok(false) => log::info!("[POST stop] no signal to release"),
                Err(e) => {
                    log::warn!("[POST stop] mark_released failed: {}", e);
                    *toast = Some(Toast::new("Record stop error", now));
                }
            }
        }
        Err(e) => {
            log::warn!("[POST stop] StoragePaths error: {:?}", e);
        }
    }
}

/// Note タグ確定: 最新 `plugin_data/.../post/*.json` に annotation を追記。
///
/// サブ2-C 仕様: post/*.json が不在（Record 開始前 / サブ3 未統合）の場合は
/// `log::warn!` のみでスタブ動作。toast は成功時と同じく表示（UI 一貫性）。
fn trigger_note_save(tag: &str, toast: &mut Option<Toast>, now: f64) {
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
        PROJECT_HASH_PHASE1,
        BUS_PHASE1,
        PluginDataRole::Post,
        tag.to_string(),
    ) {
        Ok(true) => {
            log::info!("[note] appended to latest post record: {}", tag);
            *toast = Some(Toast::new(format!("Note saved: {tag}"), now));
        }
        Ok(false) => {
            log::warn!("[note] no active record file");
            // スタブ動作: post/*.json 不在でも toast は表示（サブ3 統合前の暫定）
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
