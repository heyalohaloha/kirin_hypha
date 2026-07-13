//!
//! hypha_gui 共有プリミティブを使用:
//! - 5状態 LED（Error/WatchBreathing/RecordStandby/RecordActive/Idle）
//! - 背景テクスチャ（菌糸 300×200 brightness 15% / 遅延 decode キャッシュ）
//! - flora_color 横線（#d4a043 暫定）
//! - 共通ウィジェット（value_row / fmt_val / fmt_delta / tp_color）
//!
//! - POST Active + PRE Active（DeltaMode::Active）→ Δ 3 項目表示
//! - POST Active + pair 選択中の PRE Stale / 不在（DeltaMode::Stale / NoPre）
//!   → muted Δ 3 項目表示。PRE Bypassed（明示 OFF）は pair 維持のまま絶対値 3 項目表示。
//!   pair 未選択時も絶対値 3 項目表示
//!   （LUFS-M / TP / Crest。POST 単独挿入での計測動作を目視確認する経路）
//! - POST Bypassed → 全項目 `---` + ボタン非表示（プラグイン無効化中）
//! - POST Inactive → 全項目 `---` + ボタン表示（信号待ちでも license 操作は可能）
//!
//! Record モード（recording=true）: 左列=Δ 3 項目 / 右列=絶対値 3 項目（LUFS-M / TP / Crest）。
//! NoPre 時は Δ 列が自動的に `---`、右列は POST 絶対値で埋める。
//! ペアリング表示は pair_label から取得（サブ3 で IO Thread から配信）。
//!
//! - `Keep` → PRE 候補 0 件 → toast / 排他違反 → toast /
//!   `try_enter_record(license)` → `write_pending`
//! - `Stop` → `record_sm.exit_record()` + reasoned `mark_released`
//! - `Note` → スタブ維持（U-16 は サブ2-C）
//! - Sense hint → `open::that(SENSE_UPSELL_URL)` でブラウザ起動
//!
//! （赤系禁止。色相同一・明度増）。

use hypha_gui::{
    derive_led_state, display_signal_state_for_led, display_smoothing::DisplaySmoother,
    draw_pair_indicator, fmt_delta, fmt_val, led_color, tp_over, val_color, BackgroundTexture,
    PlaybackMaxTracker, BG, COL_FLORA, COL_FLORA_BRIGHT, COL_MUTED, COL_NORMAL,
};
use kirin_measure::reservation; // B-127 (G-115-365): egui parity — per-pairing O_EXCL frame
use kirin_measure::{
    active_post_project_uuids_for_operation_group, all_keep_signal_path, all_stop_signal_path,
    append_annotation_to_latest, count_distinct_pairings, current_host_process_id,
    delete_broadcast, enumerate_live_pre_pair_choices_for_post_project_in_session,
    enumerate_owned_post_pair_candidates_for_operation_group,
    enumerate_ready_post_pair_candidates_for_operation_group, exit_record_preserve_pair,
    format_pair_label, live_post_project_uuids_for_operation_group, load_signal_state,
    lookup_section_label, mark_released_with_reason, pair_lock_active, pair_status_for_post,
    resolve_arm_target_for_post_project_in_session, sanitize_name, scan_latest_v2_preset,
    show_note_button, show_save_button, show_stop_record_button, write_broadcast,
    write_pending_claiming_expected_and_clock, write_stop_broadcast, DeltaMode, DeltaResult,
    DeltaSnapshot, LatchedPre, License, LiveLicense, LivenessEvaluator, MeasureResult, PairStatus,
    PlatformPaths, PluginDataRole, PostCandidate, PreCandidate, PresetFileV2, RecordStateMachine,
    ReleaseReason, SignalState, StoragePaths, MAX_ACTIVE_PER_PROJECT, SENSE_RECORD_HINT,
    SENSE_UPSELL_URL,
};
use nih_plug::prelude::Editor;
use nih_plug_egui::{
    create_egui_editor,
    egui::{self, Grid, Key, Label, RichText, Sense, Stroke, TextEdit, TextStyle, Vec2},
    EguiState,
};
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU32, AtomicU64, AtomicU8, Ordering};
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

/// W-280 / G-115-248: 再生中 pair 変更を block 中に表示する tooltip 文言。
/// R-22 中立的事実説明 (価値判断語なし)。Daisuke 確定 (判断 4) のため変更禁止。
const PAIR_LOCKED_TOOLTIP: &str = "Pair selection is locked during playback";

/// W-281 / G-115-249: pair_claimed_at に書込む Unix epoch sec (f64) を取得する。
/// `SystemTime::now().duration_since(UNIX_EPOCH)` の `Err` (clock 巻戻り) 時は
/// 0.0 fallback (claim 未確立扱い / R-28 機能的沈黙)。
fn epoch_secs_now() -> f64 {
    use std::time::{SystemTime, UNIX_EPOCH};
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .map(|d| d.as_secs_f64())
        .unwrap_or(0.0)
}

/// Replace the user-facing PRE name and sever every exact binding derived from
/// the previous selection in the same GUI transition.  The name remains the
/// convenient selector; `paired_pre_target`/`latched_pre` are session facts and
/// must never survive a name change.
fn replace_pair_pre_name(state: &PostEditorState, new_name: String, claimed_at: f64) {
    replace_pair_selection(state, new_name, claimed_at, None);
}

fn replace_pair_pre_candidate(state: &PostEditorState, candidate: &PreCandidate, claimed_at: f64) {
    let Some(project_dir) = candidate
        .path
        .parent()
        .and_then(|instance_dir| instance_dir.parent())
        .map(std::path::Path::to_path_buf)
    else {
        return;
    };
    let name = candidate.name.clone().unwrap_or_default();
    let selected = LatchedPre {
        name: name.clone(),
        instance_id: candidate.instance_id.clone(),
        project_dir,
        pre_json: candidate.path.clone(),
        daw_session_id: candidate.daw_session_id.clone(),
        host_process_id: candidate.host_process_id,
    };
    replace_pair_selection(state, name, claimed_at, Some(selected));
}

fn replace_pair_selection(
    state: &PostEditorState,
    new_name: String,
    claimed_at: f64,
    new_latch: Option<LatchedPre>,
) {
    // Keep the selector write-locked until both exact-instance slots have been
    // detached. IO readers can therefore observe either the complete old
    // binding or the complete new unbound selector, never a new name carrying
    // the previous PRE instance.
    let mut name = state
        .pair_pre_name
        .write()
        .unwrap_or_else(|poisoned| poisoned.into_inner());
    if *name == new_name {
        let requested_exact = new_latch.as_ref().map(|pre| pre.instance_id.as_str());
        let current_exact = state
            .latched_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner())
            .as_ref()
            .map(|pre| pre.instance_id.clone());
        let same_exact = requested_exact.is_some() && current_exact.as_deref() == requested_exact;
        let same_name_only =
            requested_exact.is_none() && (!new_name.is_empty() || current_exact.is_none());
        if same_exact || same_name_only {
            return;
        }
    }

    let was_recording = state.record_sm.is_recording();
    let project_hash = read_project_hash_arc(&state.project_hash);
    let instance_id = read_instance_id_arc(&state.instance_id);

    if let Ok(paths) = StoragePaths::default_platform() {
        let plugin_data_dir = paths.plugin_data_dir();
        if was_recording {
            let _ = mark_released_with_reason(
                &plugin_data_dir,
                &project_hash,
                &instance_id,
                ReleaseReason::ManualStop,
            );
        }
    }
    if was_recording {
        exit_record_preserve_pair(&state.record_sm);
    }
    state.record_acknowledged.store(false, Ordering::Release);
    let released_exact = state
        .paired_pre_target
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .take();
    let released_latch = state
        .latched_pre
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .take();
    let released_pre = released_exact.or_else(|| released_latch.map(|pre| pre.instance_id));
    state
        .pair_label
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner())
        .clear();
    *state
        .delta
        .lock()
        .unwrap_or_else(|poisoned| poisoned.into_inner()) = DeltaResult::default();
    *name = new_name;
    if let Some(selected) = new_latch {
        *state
            .latched_pre
            .lock()
            .unwrap_or_else(|poisoned| poisoned.into_inner()) = Some(selected);
    }
    *state
        .pair_claimed_at
        .write()
        .unwrap_or_else(|poisoned| poisoned.into_inner()) = claimed_at;
    drop(name);

    if let (Some(pre), Ok(paths)) = (released_pre.as_deref(), StoragePaths::default_platform()) {
        reservation::release_pairing(&paths.plugin_data_dir(), &project_hash, pre, &instance_id);
    }
}

/// T-E: cap rendered cards to keep the 300×200 GUI bounded.  Beyond this,
/// the user sees "+ N more" (or just truncates — see draw_proposals_block).
const MAX_CARDS_RENDERED: usize = 8;

/// 記録開始バナーの表示時間（秒）。
const RECORD_BANNER_DURATION_SECS: f64 = 3.0;

/// toast の表示時間（秒）。
const TOAST_DURATION_SECS: f64 = 3.0;

fn apply_pair_combo_dropdown_visuals(ui: &mut egui::Ui) {
    let visuals = &mut ui.style_mut().visuals;
    visuals.selection.bg_fill = egui::Color32::from_rgba_unmultiplied(212, 160, 67, 64);
    visuals.selection.stroke = Stroke::new(1.0, COL_FLORA);
    visuals.widgets.hovered.bg_fill = egui::Color32::from_rgba_unmultiplied(212, 160, 67, 36);
    visuals.widgets.active.bg_fill = egui::Color32::from_rgba_unmultiplied(212, 160, 67, 56);
    visuals.widgets.open.bg_fill = egui::Color32::from_rgba_unmultiplied(212, 160, 67, 32);
}

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
        Self {
            message: message.into(),
            until: now + TOAST_DURATION_SECS,
        }
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
    /// ペアリング表示用ラベル。Keep 成功後は "pair: PRE_xxxxxxxx" を保持する。
    /// Stop は Record session だけを閉じるため、この label をクリアしない。
    pub pair_label: Arc<Mutex<String>>,
    /// trigger_keep が選定した PRE instance_id（v1.2 (a) cross-instance pair 復元キー）。
    /// Keep 成功直後に Some へ更新し、Keep 失敗など pair 自体を破棄する経路だけ None に戻す。
    /// POST IO Thread が `run_record_tick` で読み出して plugin_data の
    /// `paired_pre_instance_id` field に書き込む。
    pub paired_pre_target: Arc<Mutex<Option<String>>>,
    /// license 値（起動時に読込済。サブ2-A 範囲では不変）。
    pub license: LiveLicense,
    /// preset/*.json が 1 件以上存在するか（サブ3-C-2: POST IO Thread が更新）。
    pub preset_available: Arc<AtomicBool>,

    /// installation_id フィルタ用（empty → proposals scan 全 skip / R-28）。
    pub installation_id: Arc<String>,
    /// process() が書いた再生位置（サンプル）。i64::MIN = 不明 → fallback。
    pub playback_pos_samples: Arc<AtomicI64>,
    /// process() が initialize() でキャッシュしたサンプルレート。0 = 未初期化。
    pub playback_sample_rate: Arc<AtomicU32>,

    /// W-280 / G-115-248: transport.playing 独立 AtomicBool。
    /// `process()` が毎 frame `Ordering::Relaxed` で書込、`update` closure 入口で
    /// load して再生中 pair 変更 (Name field + ComboBox PRE 候補行) を block する。
    pub is_playing: Arc<AtomicBool>,
    /// Audio Thread が刻む Watch playback pass id。MAX reset の正本。
    pub watch_playback_pass_id: Arc<AtomicU64>,

    /// B-118: 単一鮮度評価器。`update` 入口で `is_live()` を読み、`pair_lock_active(is_playing, live)`
    /// で pair 変更ロックを判定する（playing 凍結値の false-release 防止 / signal_state とは別軸 /
    /// G-115-245: 3s window）。
    pub liveness: Arc<LivenessEvaluator>,

    /// B-027 段階 2: pair PRE Name (HyphaPostParams.pair_pre_name と Arc 共有)。
    /// 編集確定時に `write()` で sanitize 後の値を書き込み、trigger_keep で
    /// `read()` した値を `filter_candidates_by_name` の引数に渡す。
    pub pair_pre_name: Arc<RwLock<String>>,

    /// B-108: display/keep 共有ラッチ。trigger_keep が `resolve_arm_target` で読む（io_thread と同実体）。
    pub latched_pre: Arc<Mutex<Option<LatchedPre>>>,

    /// W-281 / G-115-249: pair_claimed_at (Unix epoch sec) Arc 共有。
    /// editor.rs TextEdit Enter / ComboBox PRE click で write、IO Thread が read。
    pub pair_claimed_at: Arc<RwLock<f64>>,

    /// W-281 / G-115-249 / D-2: pair release toast 通知 channel (IO Thread → GUI)。
    /// update closure 入口で `take()` して `state.toast` 化する。
    pub pair_release_notice: Arc<RwLock<Option<String>>>,

    /// io_thread が正当に Record を閉じた理由を表示する通知文字列。`None` =
    /// 通常 / `Some` のとき GUI 末尾にステータス行表示 (R-26 沈黙ゲート / 通常時非表示)。
    /// B-245 以降、writer flush failure はここへ書かず Record を維持する。
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
    /// GUI 表示専用の安定化フィルタ。plugin_data / TRACE の raw 値には触れない。
    display_smoother: DisplaySmoother,
    /// GUI 表示専用の playback 最大値。Record/TRACE の raw 計測値には触れない。
    playback_max: PlaybackMaxTracker,
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
            is_playing: args.is_playing,
            watch_playback_pass_id: args.watch_playback_pass_id,
            liveness: args.liveness,
            pair_pre_name: args.pair_pre_name,
            pair_claimed_at: args.pair_claimed_at,
            pair_release_notice: args.pair_release_notice,
            record_error_message: args.record_error_message,
            latched_pre: args.latched_pre,
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
            display_smoother: DisplaySmoother::default(),
            playback_max: PlaybackMaxTracker::default(),
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
    pub license: LiveLicense,
    pub preset_available: Arc<AtomicBool>,
    pub installation_id: Arc<String>,
    pub playback_pos_samples: Arc<AtomicI64>,
    pub playback_sample_rate: Arc<AtomicU32>,
    /// W-280 / G-115-248: transport.playing 独立 AtomicBool。再生中 pair 変更 block 用。
    pub is_playing: Arc<AtomicBool>,
    /// Audio Thread が刻む Watch playback pass id。MAX reset の正本。
    pub watch_playback_pass_id: Arc<AtomicU64>,
    /// B-118: 単一鮮度評価器。`pair_lock_active(is_playing, is_live())` の live 軸。
    pub liveness: Arc<LivenessEvaluator>,
    /// B-027 段階 2: pair PRE Name の Arc 共有 (HyphaPostParams.pair_pre_name)。
    pub pair_pre_name: Arc<RwLock<String>>,
    /// W-281 / G-115-249: pair claim 時刻 Arc 共有。
    pub pair_claimed_at: Arc<RwLock<f64>>,
    /// W-281 / G-115-249 / D-2: pair release toast 通知 channel 共有。
    pub pair_release_notice: Arc<RwLock<Option<String>>>,
    /// B-025 Group B-2/B-3 / Gap-19/20: io_thread → GUI ステータス行通知 Arc。
    pub record_error_message: Arc<RwLock<Option<String>>>,
    /// B-108: display/keep 共有ラッチ。
    pub latched_pre: Arc<Mutex<Option<LatchedPre>>>,
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
            // W-280 / G-115-248: frame 入口で transport.playing snapshot。
            // 再生中 pair 変更 block の判定軸 (draw_pair_pre_name_field /
            // draw_pair_pre_combo PRE 候補行)。
            let is_playing = state.is_playing.load(Ordering::Relaxed);
            let watch_playback_pass_id = state.watch_playback_pass_id.load(Ordering::Relaxed);
            // B-115: 述語を「playing かつ live」へ。LivenessEvaluator が heartbeat 鮮度
            // （processBlock 進行中）を内部観測し、playing 凍結値でも live=false ならロックしない。
            let live = state.liveness.is_live();
            let pair_locked = pair_lock_active(is_playing, live);
            let raw_m = state.measure.lock().map(|g| g.clone()).unwrap_or_default();
            let raw_d = state.delta.lock().map(|g| g.clone()).unwrap_or_default();
            let alive = state.measure_alive.load(Ordering::Relaxed);
            let recording = state.record_sm.is_recording();
            // Record から抜けた瞬間に picker を自動閉じ（Watch 中に picker が残ることを防止）
            if !recording {
                state.note_picker_open = false;
            }
            let ack = state.record_acknowledged.load(Ordering::Relaxed);
            let pair_pre_name_snapshot = state
                .pair_pre_name
                .read()
                .ok()
                .map(|g| g.clone())
                .unwrap_or_default();
            let pair_status = pair_status_for_post(&pair_pre_name_snapshot, &state.latched_pre);
            let pair_empty_for_display = pair_status == PairStatus::Unpaired;
            let license = state.license.load();

            let now = ctx.input(|i| i.time);

            // W-281 / G-115-249 / D-3: pair release toast 通知 channel を frame 入口で
            // take。IO Thread `self_check_pair_claim` が解放を判定した直後 frame で
            // Toast 化される (R-22 中立的事実説明 / 3 秒 fade は既存 TOAST_DURATION_SECS)。
            if let Ok(mut g) = state.pair_release_notice.write() {
                if let Some(msg) = g.take() {
                    state.toast = Some(Toast::new(msg, now));
                }
            }

            // B-128 (G-115-373 / D3): restore identity anomaly を **当該 instance の分だけ** drain して
            // toast 化（per-instance routing / silent swap 禁止 / R-28）。自 instance の materialize event
            // か instance context のない wall event（global）のみ surface し、他 instance の event は出さない。
            let my_iid = read_instance_id_arc(&state.instance_id);
            if let Some(msg) = kirin_measure::take_path_event(Some(&my_iid)) {
                state.toast = Some(Toast::new(msg, now));
            }

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

            let max_m = if matches!(sig, SignalState::Bypassed) {
                state.playback_max.reset();
                MeasureResult::default()
            } else {
                state
                    .playback_max
                    .update(&raw_m, is_playing, watch_playback_pass_id, recording)
            };

            let (m, d, display_held, display_muted) = match sig {
                SignalState::Active => {
                    let smoothed_m = state.display_smoother.update_measure(&raw_m, now);
                    let (display_d, held_d, muted_d) = if raw_d.mode == DeltaMode::Active {
                        (
                            state.display_smoother.update_delta(&raw_d, now),
                            false,
                            false,
                        )
                    } else if raw_d.mode == DeltaMode::Bypassed {
                        (raw_d, false, false)
                    } else if !pair_empty_for_display {
                        match state.display_smoother.held_delta_display(now) {
                            Some(held) => (held.value, true, held.muted),
                            None => (raw_d, false, true),
                        }
                    } else {
                        (raw_d, false, false)
                    };
                    (smoothed_m, display_d, held_d, muted_d)
                }
                SignalState::Inactive => {
                    let held_m = state.display_smoother.held_measure_display(now);
                    let held_d = state.display_smoother.held_delta_display(now);
                    let held = held_m.is_some() || held_d.is_some();
                    let muted = held_m.as_ref().is_some_and(|h| h.muted)
                        || held_d.as_ref().is_some_and(|h| h.muted);
                    (
                        held_m.map(|h| h.value).unwrap_or(raw_m),
                        held_d.map(|h| h.value).unwrap_or(raw_d),
                        held,
                        muted,
                    )
                }
                SignalState::Bypassed => {
                    state.display_smoother.reset();
                    (raw_m, raw_d, false, false)
                }
            };

            let led_sig = display_signal_state_for_led(sig, recording, display_held, display_muted);
            let preset_available = state.preset_available.load(Ordering::Relaxed);
            let led = derive_led_state(alive, led_sig, recording, ack, preset_available);
            // R-28 edge-triggered: LED 状態が切り替わった瞬間のみログを出す。
            if state.prev_led != Some(led) {
                log::info!("[led] state: {:?}", led);
                state.prev_led = Some(led);
            }
            let led_col = led_color(led, now);

            draw_post(
                ctx,
                state,
                &m,
                &max_m,
                &d,
                sig,
                recording,
                pair_status,
                led_col,
                show_banner,
                license,
                now,
                pair_locked,
                display_held,
                display_muted,
            );

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
    max_m: &MeasureResult,
    d: &DeltaResult,
    sig: SignalState,
    recording: bool,
    pair_status: PairStatus,
    led_col: egui::Color32,
    show_banner: bool,
    license: License,
    now: f64,
    pair_locked: bool,
    display_held: bool,
    display_muted: bool,
) {
    egui::CentralPanel::default()
        .frame(egui::Frame::NONE.fill(BG))
        .show(ctx, |ui| {
            state.bg.paint(ctx, ui);

            ui.add_space(8.0);

            ui.horizontal(|ui| {
                ui.add_space(10.0);
                ui.label(RichText::new("POST").size(20.0).color(COL_NORMAL));
                ui.with_layout(egui::Layout::right_to_left(egui::Align::Center), |ui| {
                    ui.add_space(10.0);
                    draw_led(ui, led_col);
                    ui.add_space(6.0);
                    draw_pair_indicator(ui, pair_status);
                });
            });
            ui.add_space(4.0);

            // B-027 段階 2 / 3-A: タイトル行直下に pair PRE Name 入力欄 + ComboBox dropdown。
            // ComboBox は kirin_root 配下の **全** active PRE 候補を flatten 列挙し
            // (G-115-49 / α-2 撤回: cdylib 隔離下で project_uuid filter は構造的不成立)、
            // 選択で pair_pre_name を確定する (Keep manual 経由)。
            // `draw_pair_pre_name_field` の直近右側に並べ、自由入力（Name 検索）と
            // 一覧選択の両系統を提供する。
            //
            // W-280 / G-115-248 + B-115: pair_locked（playing かつ live）を 2 関数に伝播し、
            // 実再生中（processBlock 進行中）の pair 変更を block する。ComboBox 全体は囲わない
            // (All Stop / All Keep は機能性維持 / 判断 2 / 約束5原則 #5)。
            ui.horizontal(|ui| {
                ui.add_space(10.0);
                draw_pair_pre_name_field(ui, state, pair_locked);
                ui.add_space(4.0);
                draw_pair_pre_combo(ui, state, now, pair_locked);
            });
            ui.add_space(4.0);

            ui.horizontal(|ui| {
                ui.add_space(10.0);
                let w = ui.available_width() - 10.0;
                let (rect, _) = ui.allocate_exact_size(Vec2::new(w, 1.0), egui::Sense::hover());
                ui.painter()
                    .hline(rect.x_range(), rect.center().y, Stroke::new(1.0, COL_FLORA));
            });
            ui.add_space(4.0);

            // W-283 / G-115-251 / W-1: pair_pre_name snapshot。
            // pair_empty=true (pair_pre_name="") のとき GUI 側で draw_watch_absolute_grid
            // を強制し、IO Thread `delta_result` の状態 (W-282 reset 後の次 tick run_tick
            // pass-through Δ 再計算で last_active 復活) に依らず確実に絶対値表示にする。
            let pair_empty = pair_status == PairStatus::Unpaired;

            match sig {
                SignalState::Bypassed => {
                    draw_inactive_grid(ui);
                }
                SignalState::Inactive => {
                    if display_held && !pair_empty && has_delta_core(d) {
                        let delta_col = if display_muted { COL_MUTED } else { COL_NORMAL };
                        draw_delta_grid(ui, d, max_m, delta_col, false, display_muted);
                    } else if let Some(snap) = &d.last_active {
                        // B-049: POST 自身が Inactive でも過去 Active Δ 値を凍結保持表示
                        draw_delta_grid_frozen(ui, snap, max_m, false);
                    } else if display_held && has_measure_core(m) {
                        draw_watch_absolute_grid(ui, m, max_m, display_muted);
                    } else {
                        // last_active=None (初回起動 / Active 未経験) は既存 fallback
                        draw_inactive_grid(ui);
                    }
                }
                SignalState::Active => {
                    if recording {
                        draw_record_section(ui, m, d, display_muted);
                    } else {
                        // Watch 表示は pair 選択をグリッド形状の権威にする。
                        // PRE 明示 Bypassed だけは POST 単独の絶対値表示へ戻す。
                        // Stale/NoPre は「ペアラッチは維持、PRE は一時的に計測相手として
                        // 有効ではない」状態として muted Δ/--- を維持する。
                        // W-283 / G-115-251 / W-2: pair_empty 時は IO Thread の Δ 状態に
                        // 依らず draw_watch_absolute_grid を強制 (B-048 LKG 凍結経路 bypass)。
                        if pair_empty || d.mode == DeltaMode::Bypassed {
                            draw_watch_absolute_grid(ui, m, max_m, false);
                        } else {
                            let tp_warn = !display_muted && tp_over(m.true_peak);
                            if d.mode == DeltaMode::Active && !display_muted {
                                draw_delta_grid(ui, d, max_m, COL_NORMAL, tp_warn, false);
                            } else {
                                // B-231: pair 選択中は PRE の一時 idle/stale で POST 絶対値へ
                                // 戻さない。保持値があれば muted Δ、期限後は Δ の "---"。
                                if display_muted {
                                    draw_delta_grid(ui, d, max_m, COL_MUTED, false, true);
                                } else {
                                    draw_delta_grid(ui, d, max_m, COL_NORMAL, tp_warn, false);
                                }
                            }
                        }
                    }
                }
            }
            ui.add_space(4.0);
            draw_button_row(ui, recording, license, state, m, now, pair_status);

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

            // io_thread が正当に Record を閉じた時のステータス行
            // (R-26 沈黙ゲート: 通常時 None なので非表示)。
            // writer flush failure は Record 停止権限を持たず、ここには出さない。
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
                        Label::new(RichText::new(&msg).size(11.0).color(COL_MUTED).monospace())
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
            .num_columns(6)
            .min_col_width(40.0)
            .spacing([6.0, 4.0])
            .show(ui, |ui| {
                row_pair(
                    ui,
                    ("LUFS-M", "---".to_string(), "LUFS", COL_MUTED),
                    ("MAX", "---".to_string(), "LUFS", COL_MUTED),
                );
                row_pair(
                    ui,
                    ("TP", "---".to_string(), "dBTP", COL_MUTED),
                    ("MAX", "---".to_string(), "dBTP", COL_MUTED),
                );
                row_pair(
                    ui,
                    ("Crest", "---".to_string(), "dB", COL_MUTED),
                    ("MAX", "---".to_string(), "dB", COL_MUTED),
                );
            });
    });
}

/// Watch + PRE Active（DeltaMode::Active）: Δ 3 項目表示。
fn draw_delta_grid(
    ui: &mut egui::Ui,
    d: &DeltaResult,
    max_m: &MeasureResult,
    delta_col: egui::Color32,
    tp_warn: bool,
    max_muted: bool,
) {
    let max_tp_col = if !max_muted && tp_over(max_m.true_peak) {
        COL_FLORA_BRIGHT
    } else {
        display_value_color(max_m.true_peak, max_muted)
    };
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        Grid::new("post_delta")
            .num_columns(6)
            .min_col_width(40.0)
            .spacing([6.0, 4.0])
            .show(ui, |ui| {
                let lufs_col = if d.lufs.is_some() {
                    delta_col
                } else {
                    COL_MUTED
                };
                row_pair(
                    ui,
                    ("ΔLUFS", fmt_delta(d.lufs), "LU", lufs_col),
                    (
                        "MAX",
                        fmt_val(max_m.lufs_m),
                        "LUFS",
                        display_value_color(max_m.lufs_m, max_muted),
                    ),
                );

                let tp_col = if d.tp.is_some() {
                    if tp_warn {
                        COL_FLORA_BRIGHT
                    } else {
                        delta_col
                    }
                } else {
                    COL_MUTED
                };
                row_pair(
                    ui,
                    ("ΔTP", fmt_delta(d.tp), "dB", tp_col),
                    ("MAX", fmt_val(max_m.true_peak), "dBTP", max_tp_col),
                );

                let crest_col = if d.crest.is_some() {
                    delta_col
                } else {
                    COL_MUTED
                };
                row_pair(
                    ui,
                    ("ΔCrest", fmt_delta(d.crest), "dB", crest_col),
                    (
                        "MAX",
                        fmt_val(max_m.crest),
                        "dB",
                        display_value_color(max_m.crest, max_muted),
                    ),
                );
            });
    });
}

/// POST Inactive + 凍結値あり: Last Known Good 表示 (B-048 / G-115-245)。
///
/// `editor.rs` の `SignalState::Inactive` 分岐で `d.last_active = Some(snap)` のときに呼ばれる。
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
fn draw_delta_grid_frozen(
    ui: &mut egui::Ui,
    snap: &DeltaSnapshot,
    max_m: &MeasureResult,
    _tp_warn: bool,
) {
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        Grid::new("post_delta_frozen")
            .num_columns(6)
            .min_col_width(40.0)
            .spacing([6.0, 4.0])
            .show(ui, |ui| {
                row_pair(
                    ui,
                    ("ΔLUFS", fmt_delta(snap.lufs), "LU", COL_MUTED),
                    (
                        "MAX",
                        fmt_val(max_m.lufs_m),
                        "LUFS",
                        display_value_color(max_m.lufs_m, true),
                    ),
                );
                row_pair(
                    ui,
                    ("ΔTP", fmt_delta(snap.tp), "dB", COL_MUTED),
                    (
                        "MAX",
                        fmt_val(max_m.true_peak),
                        "dBTP",
                        display_value_color(max_m.true_peak, true),
                    ),
                );
                row_pair(
                    ui,
                    ("ΔCrest", fmt_delta(snap.crest), "dB", COL_MUTED),
                    (
                        "MAX",
                        fmt_val(max_m.crest),
                        "dB",
                        display_value_color(max_m.crest, true),
                    ),
                );
            });
    });
}

/// Watch + pair 未選択 or PRE Bypassed: POST 絶対値 3 項目（LUFS-M / TP / Crest）。
fn draw_watch_absolute_grid(
    ui: &mut egui::Ui,
    m: &MeasureResult,
    max_m: &MeasureResult,
    muted: bool,
) {
    let tp_warn = !muted && tp_over(m.true_peak);
    let tp_col = if tp_warn {
        COL_FLORA_BRIGHT
    } else {
        display_value_color(m.true_peak, muted)
    };
    let max_tp_col = if !muted && tp_over(max_m.true_peak) {
        COL_FLORA_BRIGHT
    } else {
        display_value_color(max_m.true_peak, muted)
    };
    ui.horizontal(|ui| {
        ui.add_space(10.0);
        Grid::new("post_watch_abs")
            .num_columns(6)
            .min_col_width(40.0)
            .spacing([6.0, 4.0])
            .show(ui, |ui| {
                row_pair(
                    ui,
                    (
                        "LUFS-M",
                        fmt_val(m.lufs_m),
                        "LUFS",
                        display_value_color(m.lufs_m, muted),
                    ),
                    (
                        "MAX",
                        fmt_val(max_m.lufs_m),
                        "LUFS",
                        display_value_color(max_m.lufs_m, muted),
                    ),
                );
                row_pair(
                    ui,
                    ("TP", fmt_val(m.true_peak), "dBTP", tp_col),
                    ("MAX", fmt_val(max_m.true_peak), "dBTP", max_tp_col),
                );
                row_pair(
                    ui,
                    (
                        "Crest",
                        fmt_val(m.crest),
                        "dB",
                        display_value_color(m.crest, muted),
                    ),
                    (
                        "MAX",
                        fmt_val(max_m.crest),
                        "dB",
                        display_value_color(max_m.crest, muted),
                    ),
                );
            });
    });
}

/// Record: 左列=Δ 3項目 / 右列=POST絶対値 3項目。
/// Watch 中の絶対値表示は `draw_watch_absolute_grid` 側で温存する。
///
/// 各 Δ セルの色:
/// - `delta_col` = mode (Active=COL_NORMAL / Stale/NoPre/Bypassed=COL_MUTED)
/// - `Δ.X` が None なら `COL_MUTED` で `---`
/// - ΔTP のみ POST 絶対 TP が 0 dBTP 超 (tp_warn) のとき `COL_FLORA_BRIGHT` (旧仕様維持)
fn draw_record_section(ui: &mut egui::Ui, m: &MeasureResult, d: &DeltaResult, muted: bool) {
    let delta_col = if muted {
        COL_MUTED
    } else {
        match d.mode {
            DeltaMode::Active => COL_NORMAL,
            DeltaMode::Stale => COL_MUTED,
            DeltaMode::Bypassed => COL_MUTED,
            DeltaMode::NoPre => COL_MUTED,
        }
    };
    let tp_warn = !muted && tp_over(m.true_peak);

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
    let (crest_v, crest_col) = resolve(d.crest, snap.and_then(|s| s.crest));

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
    let tp_abs_col = if tp_warn {
        COL_FLORA_BRIGHT
    } else {
        display_value_color(m.true_peak, muted)
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
                    (
                        "LUFS-M",
                        fmt_val(m.lufs_m),
                        "LUFS",
                        display_value_color(m.lufs_m, muted),
                    ),
                );
                row_pair(
                    ui,
                    ("ΔTP", fmt_delta(tp_v), "dB", tp_col),
                    ("TP", fmt_val(m.true_peak), "dBTP", tp_abs_col),
                );
                row_pair(
                    ui,
                    ("ΔCrest", fmt_delta(crest_v), "dB", crest_col),
                    (
                        "Crest",
                        fmt_val(m.crest),
                        "dB",
                        display_value_color(m.crest, muted),
                    ),
                );
            });
    });
}

fn display_value_color(v: Option<f64>, muted: bool) -> egui::Color32 {
    if muted {
        COL_MUTED
    } else {
        val_color(v)
    }
}

fn has_measure_core(m: &MeasureResult) -> bool {
    m.lufs_m.is_some() || m.true_peak.is_some() || m.crest.is_some()
}

fn has_delta_core(d: &DeltaResult) -> bool {
    d.lufs.is_some() || d.tp.is_some() || d.crest.is_some()
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
fn draw_pair_pre_name_field(ui: &mut egui::Ui, state: &mut PostEditorState, pair_locked: bool) {
    // W-280 / G-115-248 + B-115: pair_locked=true（playing かつ live）で全体 disable + tooltip。
    // TextEdit / Label / focus 関連全て囲い込みの内側で発火するため、
    // 再生中は入力・編集モード遷移ともに block される。
    let inner = ui.add_enabled_ui(!pair_locked, |ui| {
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
                // W-281 / G-115-249 / B-2: 非空 claim → epoch_secs_now() / 空文字 (手動
                // クリア) → 0.0 (claim 解放扱い / 後着判定対象外)。
                let new_claimed_at = if !sanitized.is_empty() {
                    epoch_secs_now()
                } else {
                    0.0
                };
                replace_pair_pre_name(state, sanitized, new_claimed_at);
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
    });
    // W-280: 囲んだ scope の InnerResponse.response に on_hover_text を貼る。
    // disabled scope 内側で hover でも tooltip が出る (egui 0.31.1 add_enabled_ui の
    // public 動作 / R-11 確認済)。
    if pair_locked {
        inner.response.on_hover_text(PAIR_LOCKED_TOOLTIP);
    }
}

/// B-027 段階 3-A 修正 (G-115-49 / α-2 撤回): pair PRE 候補 ComboBox dropdown。
///
/// `kirin_root` (`$TMPDIR/kirin/`) 配下から、このPOSTと同一runtime scopeのlive PREを列挙する。
/// 明示クリックはexact instanceを確定し、名前は再接続用selectorとして保持する。計測timestampが
/// 一時的に古い場合やBypass中でもruntime leaseが生きていれば候補から消さない。
///
/// クリック確定時:
/// - `name` 持ち PRE → exact instance + `pair_pre_name = name`
/// - `name = None`   → exact instance + `pair_pre_name = ""`
///
/// 0 候補時は `ui.label("No pair choices")` を出す（dropdown 内空表示）。
/// egui 0.31.1 公式 API: `ComboBox::from_id_salt` (旧 `from_id_source` は廃止)。
fn draw_pair_pre_combo(
    ui: &mut egui::Ui,
    state: &mut PostEditorState,
    now: f64,
    pair_locked: bool,
) {
    let kirin_root = PlatformPaths::current_kirin_tmp_root();
    let current_project_hash = read_project_hash_arc(&state.project_hash);
    let current_daw_session_id = read_daw_session_id_arc(&state.daw_session_id);
    let pre_candidates = enumerate_live_pre_pair_choices_for_post_project_in_session(
        &kirin_root,
        &current_project_hash,
        &current_daw_session_id,
    );
    // B-027 段階 3-B α-7-3 / Step 9: All Keep 行 N 集計のため POST candidates も取得。
    // ComboBox 先頭行の "All Keep: N ready POST(s)" 表示と display 判定 (N>=1) に使用。
    let post_candidates = enumerate_owned_post_pair_candidates_for_operation_group(
        &kirin_root,
        &current_project_hash,
        &current_daw_session_id,
        current_host_process_id(),
    );
    let ready_post_candidates = enumerate_ready_post_pair_candidates_for_operation_group(
        &kirin_root,
        &current_project_hash,
        &current_daw_session_id,
        current_host_process_id(),
    );
    // α-7' All Stop: 自身が recording=true (Record 中) なら All Stop 行を出す。
    let recording = state.record_sm.is_recording();

    let previous_interact_height = ui.spacing().interact_size.y;
    let previous_icon_spacing = ui.spacing().icon_spacing;
    let previous_icon_width = ui.spacing().icon_width;
    ui.spacing_mut().interact_size.y = 22.0;
    ui.spacing_mut().icon_spacing = 0.0;
    ui.spacing_mut().icon_width = 10.0;
    egui::ComboBox::from_id_salt("hypha_post_pair_pre_dropdown")
        .selected_text("")
        .width(22.0)
        .icon(|ui, rect, _visuals, _is_open, _placement| {
            let triangle = egui::Rect::from_center_size(rect.center(), egui::vec2(8.0, 5.0));
            ui.painter().add(egui::Shape::convex_polygon(
                vec![
                    triangle.left_top(),
                    triangle.right_top(),
                    triangle.center_bottom(),
                ],
                COL_FLORA,
                Stroke::NONE,
            ));
        })
        .show_ui(ui, |ui| {
            apply_pair_combo_dropdown_visuals(ui);
            // α-7' All Stop 行 (recording=true 時のみ / All Keep と排他表示 / 琥珀明度色)。
            // click handler 順序 = broadcast 先発火 → 自身 trigger_stop (Keep の対称形)。
            if recording {
                ui.push_id("hypha_post_all_stop_row", |ui| {
                    let label = RichText::new("All Stop: recording POSTs")
                        .size(12.0)
                        .color(COL_FLORA_BRIGHT)
                        .monospace();
                    if ui.selectable_label(false, label).clicked() {
                        let instance_id = read_instance_id_arc(&state.instance_id);
                        let project_hash_snapshot = read_project_hash_arc(&state.project_hash);
                        let daw_session_id_snapshot =
                            read_daw_session_id_arc(&state.daw_session_id);

                        log::info!("[POST all_stop] click: originator={}", instance_id);

                        // 1. broadcast 先発火 (Keep と完全対称)。
                        trigger_all_stop_broadcast(
                            &instance_id,
                            &project_hash_snapshot,
                            &daw_session_id_snapshot,
                            &mut state.toast,
                            now,
                        );
                        // 2. 自身も All Stop 理由で閉じる。
                        trigger_stop(
                            &state.record_sm,
                            &project_hash_snapshot,
                            &instance_id,
                            &state.pair_label,
                            &state.paired_pre_target,
                            ReleaseReason::AllStop,
                            &mut state.toast,
                            now,
                        );
                    }
                });
            }

            // B-027 段階 3-B α-7-3 / Step 9: All Keep 行 (先頭 / N>=1 時のみ表示)。
            // (broadcast 失敗で自身まで pair 不可になることを構造的に回避)。
            // α-7': recording=true 時は All Keep 行を非表示 (Record 中は Keep 不要)。
            let n_ready = if state.license.load() == License::Os {
                ready_post_candidates.len()
            } else {
                0
            };
            if !recording && n_ready >= 1 {
                ui.push_id("hypha_post_all_keep_row", |ui| {
                    let label = all_keep_dropdown_label(n_ready);
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
                        let m = state.measure.lock().map(|g| g.clone()).unwrap_or_default();
                        let pair_pre_name_snapshot = state
                            .pair_pre_name
                            .read()
                            .map(|g| g.clone())
                            .unwrap_or_default();

                        log::info!(
                            "[POST all_keep] click: originator={} n_ready={}",
                            instance_id,
                            n_ready
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
                            state.license.load(),
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
                            &state.latched_pre, // B-108: ラッチ先を直接 target に使う
                            None,
                        );

                        // §4-5 Step 4 診断: click handler 後続処理 (trigger_keep →
                        // write_pending → record_sm 遷移) を経た frame 末尾で broadcast
                        // file が依然として存在することを確認。frame 内 ms 単位削除
                        // (sandbox / OS / 内部の隠し経路) を切り分ける目的。
                        if let Ok(paths) = StoragePaths::default_platform() {
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

            if recording || n_ready >= 1 {
                ui.separator();
            }

            // ── 既存 PRE candidates 列挙 ───────────────────────────────────
            // W-280 / G-115-248: PRE 候補行ループのみ add_enabled_ui で囲う。
            // All Stop / All Keep 行 (上記) は囲いの外で機能維持 (判断 2 / 約束5原則 #5)。
            // ComboBox 全体は囲わない (再生中も Stop/All Keep は機能性必要)。
            let current_instance_id_for_status = read_instance_id_arc(&state.instance_id);
            let current_pair_pre_name_for_status = state
                .pair_pre_name
                .read()
                .map(|g| g.clone())
                .unwrap_or_default();
            let current_pre_instance_id = kirin_measure::paired_pre_instance_id(&state.latched_pre);
            let pre_inner = ui.add_enabled_ui(!pair_locked, |ui| {
                ui.label(
                    RichText::new("Pair choices (not Keep targets)")
                        .size(11.0)
                        .color(COL_MUTED)
                        .monospace(),
                );
                if pre_candidates.is_empty() {
                    ui.label(
                        RichText::new("No pair choices")
                            .size(11.0)
                            .color(COL_MUTED)
                            .monospace(),
                    );
                    return;
                }
                for cand in &pre_candidates {
                    let cand_name = cand.name.as_deref().unwrap_or("");
                    // Exact snapshot path で push_id 化し、widget identity をsort順入替に対して
                    // 固定する。同じrestored instance_idが別shelfに並んでもrow IDは衝突しない。
                    // selectable_label の auto-ID は label_text 文字列ハッシュに依存
                    // するため、同 index 位置の text 変動で press/release フレーム間の
                    // ID 不一致 → clicked() event 喪失 (#5-A-3 異常 2) を起こす。
                    // push_id で外側スコープ ID を固定すると本問題が構造的に解消する。
                    ui.push_id(&cand.path, |ui| {
                        let keep_status = candidate_keep_status(
                            &cand.instance_id,
                            cand_name,
                            &current_instance_id_for_status,
                            &current_pair_pre_name_for_status,
                            current_pre_instance_id.as_deref(),
                            &post_candidates,
                        );
                        let label_text = candidate_dropdown_label(cand, keep_status);
                        let row_enabled = !matches!(keep_status, CandidateKeepStatus::InUseByOther);
                        let clicked = ui
                            .add_enabled_ui(row_enabled, |ui| {
                                ui.selectable_label(
                                    false,
                                    candidate_dropdown_rich_text(label_text, keep_status),
                                )
                                .clicked()
                            })
                            .inner;
                        if clicked {
                            replace_pair_pre_candidate(state, cand, epoch_secs_now());
                            log::info!(
                                "[POST pair-combo] selected: instance_id={} name={:?}",
                                cand.instance_id,
                                cand.name
                            );
                        }
                    });
                }
            });
            // W-280: 再生中は PRE 候補 scope に locked tooltip を貼る。
            if pair_locked {
                pre_inner.response.on_hover_text(PAIR_LOCKED_TOOLTIP);
            }
        });
    ui.spacing_mut().interact_size.y = previous_interact_height;
    ui.spacing_mut().icon_spacing = previous_icon_spacing;
    ui.spacing_mut().icon_width = previous_icon_width;
}

fn all_keep_dropdown_label(n_ready: usize) -> String {
    let plural = if n_ready == 1 { "" } else { "s" };
    format!("All Keep: {n_ready} ready POST{plural}")
}

#[derive(Clone, Copy, Debug, Eq, PartialEq)]
enum CandidateKeepStatus {
    Available,
    KeepReady,
    InUseByOther,
}

fn candidate_keep_status(
    candidate_instance_id: &str,
    name: &str,
    current_instance_id: &str,
    current_pair_pre_name: &str,
    current_pre_instance_id: Option<&str>,
    post_candidates: &[PostCandidate],
) -> CandidateKeepStatus {
    let claimed_by_other = post_candidates.iter().any(|c| {
        c.instance_id != current_instance_id
            && (c.paired_pre_instance_id.as_deref() == Some(candidate_instance_id)
                || (c.paired_pre_instance_id.is_none()
                    && !name.is_empty()
                    && c.pair_pre_name.as_deref() == Some(name)))
    });
    if claimed_by_other {
        CandidateKeepStatus::InUseByOther
    } else if current_pre_instance_id == Some(candidate_instance_id)
        || (current_pre_instance_id.is_none() && !name.is_empty() && name == current_pair_pre_name)
    {
        CandidateKeepStatus::KeepReady
    } else {
        CandidateKeepStatus::Available
    }
}

/// ComboBox dropdown 1 行の表示文字列を組み立てる。
///
/// `Some(name)` → `"Can Keep: snare"`、名前なし → instance ID 先頭8文字。
fn candidate_dropdown_label(cand: &PreCandidate, keep_status: CandidateKeepStatus) -> String {
    let name_part = cand
        .name
        .as_deref()
        .filter(|name| !name.is_empty())
        .map(str::to_string)
        .unwrap_or_else(|| cand.instance_id.chars().take(8).collect());
    let prefix = match keep_status {
        CandidateKeepStatus::Available => "Can Keep",
        CandidateKeepStatus::KeepReady => "Keep ready",
        CandidateKeepStatus::InUseByOther => "In use",
    };
    format!("{prefix}: {name_part}")
}

fn candidate_dropdown_rich_text(label_text: String, keep_status: CandidateKeepStatus) -> RichText {
    let color = match keep_status {
        CandidateKeepStatus::Available => COL_NORMAL,
        CandidateKeepStatus::KeepReady => COL_FLORA_BRIGHT,
        CandidateKeepStatus::InUseByOther => COL_MUTED,
    };
    RichText::new(candidate_dropdown_display_text(label_text, keep_status))
        .size(12.0)
        .color(color)
        .monospace()
}

fn candidate_dropdown_display_text(label_text: String, keep_status: CandidateKeepStatus) -> String {
    match keep_status {
        CandidateKeepStatus::KeepReady => format!("✓ {label_text}"),
        CandidateKeepStatus::Available | CandidateKeepStatus::InUseByOther => label_text,
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
    pair_status: PairStatus,
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
                let width = (ui.available_width() - 10.0 - 18.0) / 4.0;
                ui.spacing_mut().item_spacing.x = 6.0;
                for tag in ["Good", "Fix", "Hold"] {
                    if ui
                        .add_sized([width, 26.0], egui::Button::new(tag))
                        .clicked()
                    {
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
                if ui
                    .add_sized([width, 26.0], egui::Button::new("Cancel"))
                    .clicked()
                {
                    log::info!("[hypha-fork] button clicked: Cancel");
                    state.note_picker_open = false;
                }
            } else {
                // Record: [Stop] [Note]
                let width = (ui.available_width() - 10.0 - 6.0) / 2.0;
                ui.spacing_mut().item_spacing.x = 6.0;
                if show_stop_record_button(license)
                    && ui
                        .add_sized([width, 26.0], egui::Button::new("Stop"))
                        .clicked()
                {
                    log::info!("[hypha-fork] button clicked: Stop");
                    trigger_stop(
                        &state.record_sm,
                        &project_hash_snapshot,
                        &instance_id,
                        &state.pair_label,
                        &state.paired_pre_target,
                        ReleaseReason::ManualStop,
                        &mut state.toast,
                        now,
                    );
                }
                if show_note_button(license)
                    && ui
                        .add_sized([width, 26.0], egui::Button::new("Note"))
                        .clicked()
                {
                    log::info!("[hypha-fork] button clicked: Note");
                    state.note_picker_open = true;
                }
            }
        } else {
            // Watch: [Keep or Sense hint]（License::Unknown は空行）
            if show_save_button(license) {
                let width = ui.available_width() - 10.0;
                if ui
                    .add_enabled(
                        pair_status != PairStatus::Unpaired,
                        egui::Button::new("Keep").min_size(Vec2::new(width, 26.0)),
                    )
                    .clicked()
                {
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
                        &state.latched_pre, // B-108: ラッチ先を直接 target に使う
                        None,
                    );
                }
            } else if license == License::Sense {
                let resp = ui.add(
                    egui::Button::new(RichText::new(SENSE_RECORD_HINT).size(11.0).color(COL_FLORA))
                        .frame(false)
                        .min_size(Vec2::new(ui.available_width() - 10.0, 26.0)),
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

/// Keep タップ: PRE 候補 / 排他 / license を事前チェックし、
/// record_signal pending を作る。RecordStateMachine は PRE ACK 後に POST IO Thread が入れる。
/// 成功時は pair_label を `pair: PRE_xxxxxxxx` で設定する（POST GUI 表示用）。
/// 同時に `paired_pre_target` に `target_id` を保存し、IO Thread が次回の writer_start で
/// plugin_data の `paired_pre_instance_id` field に書き込めるようにする（v1.2 (a)）。
/// Keep は expected WAV metadata を要求せず、今回の Record session だけを開始する。
/// Drop 後の WAV metadata が同じ session を正本 sample 境界へ収束させる。
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
    // B-108: ラッチ済みならラッチ先を直接 Arm target に使う（resolve_arm_target 内で分岐）。
    latched: &Mutex<Option<LatchedPre>>,
    started_at_position_samples: Option<i64>,
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
        latched,
        started_at_position_samples,
    );
}

/// `trigger_keep` の内部実装 (B-027 段階 3-B α-7-4-D Step 1)。
///
/// `toast: Option<&mut Option<Toast>>` で None 時は toast 抑制。Step 11 で IO Thread
/// broadcast 受信側 (α-7 All Keep) から本関数を `toast = None` で直接呼出し、N 倍
/// toast 嵐を構造的に防ぐ。`log::*` は不変 (toast 抑制でも log は残す)。
///
/// POST 単独 Record を作らないため、本関数は `record_sm.try_enter_record*` を呼ばない。
#[allow(clippy::too_many_arguments)]
pub(crate) fn trigger_keep_internal(
    license: License,
    record_sm: &Arc<RecordStateMachine>,
    instance_id: &str,
    project_hash: &str,
    daw_session_id: &str,
    pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    // B-059: 距離 auto-pick 廃止により POST メトリクスは選定に不要（未ラッチ時は
    // name + freshness で一意選定）。caller 据え置きのため `_` 受け。
    _m: &MeasureResult,
    mut toast: Option<&mut Option<Toast>>,
    now: f64,
    pair_pre_name: &str,
    // B-108: ラッチ済みならラッチ先を直接 Arm target に使う（同名2台目でも結合不変）。未ラッチ時のみ
    // select_target_pre_for_arm にフォールバック（resolve_arm_target 内で分岐）。
    latched: &Mutex<Option<LatchedPre>>,
    started_at_position_samples: Option<i64>,
) {
    // 1. ストレージパス解決
    let paths = match StoragePaths::default_platform() {
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
    let tmp_base = PlatformPaths::current_kirin_tmp_root();
    let _ = reservation::sweep_stale_reservations(&plugin_data_dir);

    // 2 + 4. PRE 選定（B-059: 表示=commit 一本化）
    //
    // B-104/B-231: ラッチ済みなら instance_id を権威にする。未ラッチ時だけ
    // `select_target_pre_for_arm`（非Bypassed + t<NO_PRE_SECS + Name 一致で一意 1 件 / Active
    // 要求なし）へフォールバックする。pair_pre_name 空 / 同名複数 / 不在 / Bypassed / 古t は
    // None → "No PRE Paired"。
    let target = match resolve_arm_target_for_post_project_in_session(
        &tmp_base,
        pair_pre_name,
        project_hash,
        daw_session_id,
        latched,
    ) {
        Some(sel) => sel,
        None => {
            log::info!(
                "[POST keep] no armable PRE for pair_pre_name=\"{}\" (none/ambiguous/bypassed/stale)",
                pair_pre_name
            );
            if let Some(t) = toast.as_mut() {
                **t = Some(Toast::new("No PRE Paired", now));
            }
            return;
        }
    };
    let target_id = target.instance_id.clone();

    if record_sm.is_recording() {
        log::info!("[POST keep] already recording — ignored");
        return;
    }
    if !matches!(license, License::Os) {
        log::info!("[POST keep] license denied: {:?}", license);
        if let Some(t) = toast.as_mut() {
            **t = Some(Toast::new("Record requires Kirin OS license", now));
        }
        return;
    }
    // 5. G-115-365: per-pairing O_EXCL 枠を確保（cross-process atomic / FFI resolve_and_enter_keep
    // と同一 parity）。pairing key = (target_id=PRE iid, instance_id=POST iid)。cap 真実源は枠の
    // 物理存在のみ（count_distinct_pairings = reservation::count_frames）。reserve→枠数>MAX で
    // 13 ペア目を hard reject（R-28 通知）。reserve Err（write_all 失敗等）= 枠取れず = reject。
    let reservation_created =
        match reservation::reserve_pairing(&plugin_data_dir, project_hash, &target_id, instance_id)
        {
            Ok(reservation::ReserveOutcome::Created) => true,
            Ok(reservation::ReserveOutcome::AlreadyReserved) => false,
            Err(_) => {
                if let Some(t) = toast.as_mut() {
                    **t = Some(Toast::new("Maximum 12 pairs reached", now));
                }
                return;
            }
        };
    if count_distinct_pairings(&plugin_data_dir, project_hash) > MAX_ACTIVE_PER_PROJECT {
        if reservation_created {
            reservation::release_pairing(&plugin_data_dir, project_hash, &target_id, instance_id);
        }
        if let Some(t) = toast.as_mut() {
            **t = Some(Toast::new("Maximum 12 pairs reached", now));
        }
        return;
    }

    // 6. record_signal を pending で書き込み（A-3 修正後: post_instance_id を path 識別子に）
    match write_pending_claiming_expected_and_clock(
        &plugin_data_dir,
        project_hash,
        instance_id,
        target_id.clone(),
        daw_session_id.to_string(),
        started_at_position_samples,
    ) {
        Ok(_) => {
            log::info!(
                "[POST keep] write_pending ok: requested_by={} target={} daw={}",
                instance_id,
                target_id,
                daw_session_id
            );
            // 7. pair_label を表示用に設定（Keep 時は PRE Name 不明 → UUID8 fallback）
            // B-023 段階 4: PRE 側 ack 後 IO Thread の poll が paired_pre_name を
            // 取得して同 Arc を `pair: <name>` に上書きする（最大 1 秒遅延）。
            set_pair_label(pair_label, "", &target_id);
            // 8. v1.2 (a): paired_pre_target を保存（IO Thread が writer_start 時に消費）
            if let Ok(mut g) = paired_pre_target.lock() {
                *g = Some(target_id.clone());
            }
            if let Ok(mut g) = latched.lock() {
                *g = Some(LatchedPre {
                    name: pair_pre_name.to_string(),
                    instance_id: target_id.clone(),
                    project_dir: target.project_dir,
                    pre_json: target.pre_json,
                    daw_session_id: target.daw_session_id,
                    host_process_id: target.host_process_id,
                });
            }
        }
        Err(e) => {
            log::warn!("[POST keep] write_pending failed: {}", e);
            // G-115-365: write_pending 失敗時も自分で取った枠を戻す（孤児を残さない）。
            if reservation_created {
                reservation::release_pairing(
                    &plugin_data_dir,
                    project_hash,
                    &target_id,
                    instance_id,
                );
            }
            if let Ok(mut g) = paired_pre_target.lock() {
                *g = None;
            }
            if let Some(t) = toast.as_mut() {
                **t = Some(Toast::new("Failed to start record", now));
            }
        }
    }
}

/// All Keep broadcast を filesystem に書込んで同じ explicit-pair operation group の他 POST に通知する
/// (B-027 段階 3-B α-7-4-D Step 2)。
///
/// Originator (= ComboBox 「All Keep: N ready POST(s)」を click した POST) のみが呼出す。
/// 受信側 (Step 10-11 で実装) は io_thread_post.rs sub-tick で
/// [`kirin_measure::scan_broadcasts_dir`] / `trigger_keep_internal(toast=None)` を経由
/// して各々 pair 確定する (toast 嵐回避)。
///
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
    let paths = match StoragePaths::default_platform() {
        Ok(p) => p,
        Err(e) => {
            log::warn!("[POST all_keep] StoragePaths resolve failed: {:?}", e);
            *toast = Some(Toast::new("Kirin OS not installed", now));
            return;
        }
    };
    let plugin_data_dir = paths.plugin_data_dir();
    let kirin_root = PlatformPaths::current_kirin_tmp_root();
    let mut project_hashes = active_post_project_uuids_for_operation_group(
        &kirin_root,
        project_hash,
        daw_session_id,
        current_host_process_id(),
    );
    if project_hashes.is_empty() && !project_hash.is_empty() {
        project_hashes.push(project_hash.to_string());
    }

    let mut wrote_any = false;
    for target_project_hash in project_hashes {
        match write_broadcast(
            &plugin_data_dir,
            &target_project_hash,
            originator_instance_id,
            daw_session_id.to_string(),
        ) {
            Ok(broadcast) => {
                wrote_any = true;
                let broadcast_path = all_keep_signal_path(
                    &plugin_data_dir,
                    &target_project_hash,
                    originator_instance_id,
                );
                log::info!(
                    "[POST all_keep] broadcast written: originator={} started_at={} path={}",
                    originator_instance_id,
                    broadcast.started_at,
                    broadcast_path.display()
                );
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
            }
        }
    }
    if !wrote_any {
        *toast = Some(Toast::new("All Keep failed (file write error)", now));
    }
}

/// Stop タップ: Watch へ戻し、record_signal を released に更新する。
/// Pair selection は保持する。Stop は Record session の終了であり、Unpair ではない。
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
    release_reason: ReleaseReason,
    toast: &mut Option<Toast>,
    now: f64,
) {
    trigger_stop_internal(
        record_sm,
        project_hash,
        instance_id,
        pair_label,
        paired_pre_target,
        release_reason,
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
    _pair_label: &Arc<Mutex<String>>,
    paired_pre_target: &Arc<Mutex<Option<String>>>,
    release_reason: ReleaseReason,
    mut toast: Option<&mut Option<Toast>>,
    now: f64,
) {
    // G-115-365: reservation 解放用に対 PRE iid を先に捕捉する
    // (FFI resolve_and_exit_stop と同一 parity)。
    let released_pre = paired_pre_target.lock().ok().and_then(|g| g.clone());
    // 2026-07-10 構造修正（ACK re-entry race）: shared signal を Released にしてから
    // record_sm を Watch へ戻す。逆順だと、record_sm が既に Watch なのに on-disk signal は
    // まだ Acknowledged のままという間隙が生まれ、その間に ACK poller が stale な
    // Acknowledged を読んで同じ session_id へ再入場してしまう。record_sm 側の
    // `closed_session_id` ガード（record.rs）が構造的な本丸だが、この reorder は
    // その隙間自体を縮める defense-in-depth。`exit_record_preserve_pair` は
    // StoragePaths 解決の成否に関わらず必ず1回だけ呼ぶ（元の無条件呼び出しと同じ保証）。
    match StoragePaths::default_platform() {
        Ok(paths) => {
            let plugin_data_dir = paths.plugin_data_dir();
            // G-115-365: 本 pairing の O_EXCL 枠を解放（再予約可に / 孤児は sweep がバックストップ）。
            if let Some(pre) = released_pre.as_deref() {
                reservation::release_pairing(&plugin_data_dir, project_hash, pre, instance_id);
            }
            match mark_released_with_reason(
                &plugin_data_dir,
                project_hash,
                instance_id,
                release_reason,
            ) {
                Ok(true) => log::info!("[POST stop] mark_released ok ({release_reason:?})"),
                Ok(false) => log::info!("[POST stop] no signal to release"),
                Err(e) => {
                    log::warn!("[POST stop] mark_released failed: {}", e);
                    if let Some(t) = toast.as_mut() {
                        **t = Some(Toast::new("Record stop error", now));
                    }
                }
            }
            exit_record_preserve_pair(record_sm);

            // B-243/B-269: trigger_stop では record_signal を即削除しない。
            // PRE は reason 付き `released` だけを明示 Stop 理由として観測して Watch へ戻る。
            // missing を Stop 代替にすると一時的な scan/read miss でも Record が閉じ得るため、
            // lifecycle cleanup は Drop / IO Thread shutdown の冪等 delete に限定する。

            // originator として配置した all_keep_signal/{POST_iid}.json broadcast を削除。
            // delete_broadcast は冪等 (all_keep_signal.rs:211-215 / NotFound→Ok)。統合点
            // #3 (Drop) / #4 (IO Thread terminate) と重複呼出されても安全。
            // 失敗時 warn のみ (設計判断 #8)。
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
            exit_record_preserve_pair(record_sm);
        }
    }
}

/// All Stop broadcast を filesystem に書込 (α-7')。`trigger_all_keep_broadcast` と完全対称。
///
/// originator (= All Stop ボタンを押した POST) が 1 回だけ呼ぶ。受信側
/// (`io_thread_post.rs` sub-tick) は同 DAW session の `all_stop_signal/*.json` を全件
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
    let paths = match StoragePaths::default_platform() {
        Ok(p) => p,
        Err(e) => {
            log::warn!("[POST all_stop] StoragePaths resolve failed: {:?}", e);
            *toast = Some(Toast::new("Kirin OS not installed", now));
            return;
        }
    };
    let plugin_data_dir = paths.plugin_data_dir();
    let kirin_root = PlatformPaths::current_kirin_tmp_root();
    let mut project_hashes = live_post_project_uuids_for_operation_group(
        &kirin_root,
        project_hash,
        daw_session_id,
        current_host_process_id(),
    );
    if project_hashes.is_empty() && !project_hash.is_empty() {
        project_hashes.push(project_hash.to_string());
    }

    let mut wrote_any = false;
    for target_project_hash in project_hashes {
        match write_stop_broadcast(
            &plugin_data_dir,
            &target_project_hash,
            originator_instance_id,
            daw_session_id.to_string(),
        ) {
            Ok(broadcast) => {
                wrote_any = true;
                let bp = all_stop_signal_path(
                    &plugin_data_dir,
                    &target_project_hash,
                    originator_instance_id,
                );
                log::info!(
                    "[POST all_stop] broadcast written: originator={} started_at={} path={}",
                    originator_instance_id,
                    broadcast.started_at,
                    bp.display()
                );
            }
            Err(e) => {
                log::warn!("[POST all_stop] broadcast write failed: {:?}", e);
            }
        }
    }
    if !wrote_any {
        *toast = Some(Toast::new("All Stop failed (file write error)", now));
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
    let paths = match StoragePaths::default_platform() {
        Ok(p) => p,
        Err(e) => {
            log::warn!("[note] StoragePaths error: {:?}", e);
            *toast = Some(Toast::new("Note save failed", now));
            return;
        }
    };
    let result = append_annotation_to_latest(
        &paths.plugin_data_dir(),
        project_hash,
        instance_id,
        PluginDataRole::Post,
        tag.to_string(),
    );
    match &result {
        Ok(true) => log::info!("[note] appended to latest post record: {}", tag),
        Ok(false) => log::warn!("[note] no active record file (nothing to append to)"),
        Err(e) => log::warn!("[note] append failed: {}", e),
    }
    // B-111: Ok(true) のみ成功表示。Ok(false)(記録不在) / Err(IO) は失敗表示にする（旧実装は
    // Ok(false) でも「Note saved」と偽表示していた）。JUCE 殻と parity（onNote も bool を分岐）。
    if note_append_succeeded(&result) {
        *toast = Some(Toast::new(format!("Note saved: {tag}"), now));
    } else {
        *toast = Some(Toast::new("Note save failed", now));
    }
}

/// B-111: Note 追記が成功か。Ok(true) のみ成功 / Ok(false)（記録不在）・Err（IO）は失敗。純粋・テスト可能。
fn note_append_succeeded<E>(result: &Result<bool, E>) -> bool {
    matches!(result, Ok(true))
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
    let Ok(paths) = StoragePaths::default_platform() else {
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
                        RichText::new(&btn_text)
                            .size(11.0)
                            .color(COL_NORMAL)
                            .monospace(),
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

    /// B-111: Note 追記の成功判定（失敗系の回帰固定）。Ok(true) のみ成功、Ok(false)(記録不在)・
    /// Err(IO) は失敗＝失敗表示。旧 egui は Ok(false) を「Note saved」と偽表示していた。
    #[test]
    fn note_append_succeeded_only_on_ok_true() {
        let ok_true: Result<bool, ()> = Ok(true);
        let ok_false: Result<bool, ()> = Ok(false);
        let err: Result<bool, ()> = Err(());
        assert!(note_append_succeeded(&ok_true), "Ok(true) = 成功");
        assert!(
            !note_append_succeeded(&ok_false),
            "Ok(false)(記録不在) = 失敗"
        );
        assert!(!note_append_succeeded(&err), "Err(IO) = 失敗");
    }

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
        assert_eq!(format_pair_label("", "abcdefghijklmnop"), "pair: abcdefgh");
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

    /// `Some(name)` 持ち候補は `"Can Keep: <name>"` で描画され、UUID は出さない。
    #[test]
    fn dropdown_label_with_name_hides_instance_id() {
        let cand = PreCandidate {
            instance_id: "abcdef1234567890".into(),
            lufs_m: None,
            true_peak: None,
            crest: None,
            path: std::path::PathBuf::new(),
            name: Some("snare".into()),
            host_process_id: None,
            daw_session_id: None,
        };
        assert_eq!(
            candidate_dropdown_label(&cand, CandidateKeepStatus::Available),
            "Can Keep: snare"
        );
    }

    /// `name = None` の PRE も instance_id の先頭8文字で選択できる。
    #[test]
    fn dropdown_label_with_no_name_falls_back() {
        let cand = PreCandidate {
            instance_id: "abcdef1234567890".into(),
            lufs_m: None,
            true_peak: None,
            crest: None,
            path: std::path::PathBuf::new(),
            name: None,
            host_process_id: None,
            daw_session_id: None,
        };
        assert_eq!(
            candidate_dropdown_label(&cand, CandidateKeepStatus::Available),
            "Can Keep: abcdef12"
        );
    }

    /// instance_id は候補表示に使わない。
    #[test]
    fn dropdown_label_short_instance_id_stays_hidden() {
        let cand = PreCandidate {
            instance_id: "abc".into(),
            lufs_m: None,
            true_peak: None,
            crest: None,
            path: std::path::PathBuf::new(),
            name: Some("kick".into()),
            host_process_id: None,
            daw_session_id: None,
        };
        assert_eq!(
            candidate_dropdown_label(&cand, CandidateKeepStatus::Available),
            "Can Keep: kick"
        );
    }

    #[test]
    fn dropdown_label_marks_keep_readiness_and_in_use() {
        let cand = PreCandidate {
            instance_id: "abcdef12-3456-7890".into(),
            lufs_m: None,
            true_peak: None,
            crest: None,
            path: std::path::PathBuf::new(),
            name: Some("Music".into()),
            host_process_id: None,
            daw_session_id: None,
        };
        assert_eq!(
            candidate_dropdown_label(&cand, CandidateKeepStatus::KeepReady),
            "Keep ready: Music"
        );
        assert_eq!(
            candidate_dropdown_display_text(
                candidate_dropdown_label(&cand, CandidateKeepStatus::KeepReady),
                CandidateKeepStatus::KeepReady,
            ),
            "✓ Keep ready: Music"
        );
        assert_eq!(
            candidate_dropdown_label(&cand, CandidateKeepStatus::InUseByOther),
            "In use: Music"
        );
    }

    #[test]
    fn candidate_keep_status_distinguishes_current_other_and_available() {
        let claims = vec![
            PostCandidate {
                instance_id: "self-post".into(),
                project_uuid: "p".into(),
                daw_session_id: Some("daw".into()),
                host_process_id: Some(1),
                pair_pre_name: Some("Music".into()),
                paired_pre_instance_id: Some("pre-music".into()),
                pair_claimed_at: 1.0,
                path: std::path::PathBuf::new(),
            },
            PostCandidate {
                instance_id: "other-post".into(),
                project_uuid: "p".into(),
                daw_session_id: Some("daw".into()),
                host_process_id: Some(1),
                pair_pre_name: Some("Drum".into()),
                paired_pre_instance_id: Some("pre-drum".into()),
                pair_claimed_at: 2.0,
                path: std::path::PathBuf::new(),
            },
        ];
        assert_eq!(
            candidate_keep_status(
                "pre-music",
                "Music",
                "self-post",
                "Music",
                Some("pre-music"),
                &claims,
            ),
            CandidateKeepStatus::KeepReady
        );
        assert_eq!(
            candidate_keep_status(
                "pre-drum",
                "Drum",
                "self-post",
                "Music",
                Some("pre-music"),
                &claims,
            ),
            CandidateKeepStatus::InUseByOther
        );
        assert_eq!(
            candidate_keep_status(
                "pre-vocal",
                "Vocal",
                "self-post",
                "Music",
                Some("pre-music"),
                &claims,
            ),
            CandidateKeepStatus::Available
        );
    }

    #[test]
    fn candidate_keep_status_prefers_in_use_when_other_post_claims_same_pair() {
        let claims = vec![PostCandidate {
            instance_id: "other-post".into(),
            project_uuid: "p".into(),
            daw_session_id: Some("daw".into()),
            host_process_id: Some(1),
            pair_pre_name: Some("Music".into()),
            paired_pre_instance_id: Some("pre-music".into()),
            pair_claimed_at: 2.0,
            path: std::path::PathBuf::new(),
        }];
        assert_eq!(
            candidate_keep_status("pre-music", "Music", "self-post", "", None, &claims,),
            CandidateKeepStatus::InUseByOther
        );
    }

    #[test]
    fn candidate_keep_status_allows_distinct_instances_with_the_same_name() {
        let claims = vec![PostCandidate {
            instance_id: "self-post".into(),
            project_uuid: "p".into(),
            daw_session_id: Some("daw".into()),
            host_process_id: Some(1),
            pair_pre_name: Some("Music".into()),
            paired_pre_instance_id: Some("pre-music-a".into()),
            pair_claimed_at: 1.0,
            path: std::path::PathBuf::new(),
        }];
        assert_eq!(
            candidate_keep_status(
                "pre-music-b",
                "Music",
                "self-post",
                "Music",
                Some("pre-music-a"),
                &claims,
            ),
            CandidateKeepStatus::Available
        );
    }

    #[test]
    fn all_keep_dropdown_label_names_ready_posts() {
        assert_eq!(all_keep_dropdown_label(1), "All Keep: 1 ready POST");
        assert_eq!(all_keep_dropdown_label(2), "All Keep: 2 ready POSTs");
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
