//! kirin_measure — Kirin Hypha 共通計測ライブラリ。
//!
//! napi-rs 依存を持たない純粋な Rust ライブラリ。
//! nih-plug の Audio Thread から独立した Measure Thread / IO Thread で使用する。

pub mod delta;
pub mod engine;
pub mod exclusion;
pub mod hardware;
pub mod identity;
pub mod io_thread_post;
pub mod io_thread_pre;
pub mod license;
pub mod measure_thread;
pub mod phase_d;
pub mod plugin_data;
pub mod preset;
pub mod preset_dispatch;
pub mod preset_v2;
pub mod record;
pub mod record_signal;
pub mod record_writer;
pub mod resampler;
pub mod storage;
pub mod watchdog;

pub use delta::{DeltaMode, DeltaResult};
pub use engine::MeasureEngine;
pub use exclusion::{
    check_record_exclusion, check_record_exclusion_at, is_heartbeat_fresh, ExclusionResult,
    STALE_SECONDS,
};
pub use hardware::{HardwareComponents, Match};
pub use identity::{Identity, License};
pub use io_thread_post::{serialize_post_json, spawn_io_thread_post};
pub use io_thread_pre::{serialize_pre_json, spawn_io_thread_pre};
pub use license::{
    can_enter_record, can_read_preset, can_write_plugin_data, load_license_safe, show_note_button,
    show_save_button, show_stop_record_button, SENSE_RECORD_HINT, SENSE_UPSELL_URL,
};
pub use measure_thread::spawn_measure_thread;
pub use plugin_data::{
    append_annotation_to_latest, compact_wall_clock, verify_checksum, Annotation, BounceMarker,
    Frame, PluginDataFile, PluginDataWriter, PsbSnapshot, Role as PluginDataRole,
    Status as PluginDataStatus, WriterError as PluginDataWriterError, WriterPaths,
};
pub use preset::{
    compute_preset_checksum, preset_dir, region_resolved, scan_valid_presets, verify_preset,
    PresetFile, Region as PresetRegion, VerifyError as PresetVerifyError, PRESET_SUBDIR,
};
pub use preset_dispatch::{
    dispatch_one as dispatch_preset_one, scan_any_presets, scan_latest_v2_preset,
    DispatchError as PresetDispatchError, PresetVariant,
};
pub use preset_v2::{
    compute_preset_v2_checksum, lookup_section_label, preset_dir_v2, scan_valid_presets_v2,
    verify_preset_v2, Card as PresetV2Card, PresetFileV2,
    SectionBoundary as PresetV2SectionBoundary, Summary as PresetV2Summary, VerifyErrorV2,
};
pub use record::{RecordState, RecordStateMachine, TransitionError};
pub use record_signal::{
    delete_signal, is_timed_out, mark_acknowledged, mark_released, pick_closest_pre, read_signal,
    scan_pre_candidates, scan_signals_dir, signal_path, signals_dir, write_pending, write_signal,
    PostMetrics, PreCandidate, RecordSignal, SignalError, SignalStatus, ACK_TIMEOUT_SECONDS,
    SIGNALS_SUBDIR, SIGNAL_FILENAME,
};
pub use storage::{
    cleanup_legacy_v1, load_installation_id_safe, load_or_recover, read_identity, write_both,
    write_identity_atomic, CleanupReport, IdentityCache, LoadStatus, LoadedIdentity, StorageError,
    StoragePaths, CLEANUP_V1_DONE_FILENAME,
};
pub use watchdog::{spawn_watchdog, WatchdogParams};

use std::sync::atomic::{AtomicU8, Ordering};
use std::sync::{Arc, OnceLock, RwLock};

// ── 共有定数 ────────────────────────────────────────────────────────────────

/// Audio Thread → Measure Thread リングバッファの保持長（秒）。
/// 2 秒: Measure Thread 再起動時の空白を吸収できる余裕（guardian_53 T-2）。
pub const RING_BUFFER_SECONDS: usize = 2;

/// 対応チャンネル数（ステレオ固定）。
pub const N_CHANNELS: usize = 2;

// ── プロセス単位識別子（B-020 / γ-3 chunk-persistent UUID 後）─────────────
//
// 履歴:
// - Phase 1.0: `PROJECT_HASH_PHASE1="default"` 固定値で全インスタンス共有 →
//   複数 Bus / 複数プロジェクトで衝突（致命級 A-3 / A-2）
// - A-3 中間策: プロセス単位 OnceLock<String> で起動時 1 度だけ生成 →
//   DAW 再起動で path が変わるためバウンス再計測の比較が困難
// - B-020 / γ-3 (本実装): nih-plug `#[persist = "project_uuid"]` でプロジェクト
//   chunk に UUID を保存。再オープンで同一値が復元され、PRE/POST が共有する
//
// 値は `OnceLock<Arc<RwLock<String>>>` セルにキャッシュされ、Plugin の
// `initialize()` から chunk-persist 値で `set_project_uuid()` / `set_daw_session_id()`
// により更新される。ファイル階層は `plugin_data/{project_uuid}/{instance_id}/{pre|post}/`
// で区切られる（bus 概念は path から削除済）。

fn project_uuid_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

fn daw_session_id_cell() -> &'static Arc<RwLock<String>> {
    static CELL: OnceLock<Arc<RwLock<String>>> = OnceLock::new();
    CELL.get_or_init(|| Arc::new(RwLock::new(String::new())))
}

/// 新規 project_uuid を生成する（UUID v4 の文字列形式）。
///
/// chunk-persist 機構に保存される値の初期値として使用。Plugin が
/// `Default::default()` で `RwLock<String>` field を初期化する際の
/// fallback。プロジェクト保存後は chunk-persist 値が常に優先される。
pub fn generate_project_hash() -> String {
    uuid::Uuid::new_v4().to_string()
}

/// プロセス単位 `project_uuid` の現在値を読み取る。
///
/// セル空のときは lazy fallback で生成 UUID を返す（テスト・diagnostics 用途）。
/// 通常運用では Plugin の `initialize()` が `set_project_uuid()` で
/// chunk-persist 値をセットした直後に呼ばれる。
///
/// 関数名は backward compat のため `process_project_hash` のまま。値の
/// セマンティクスは「chunk-persistent project UUID」へ変わった。
pub fn process_project_hash() -> String {
    let cell = project_uuid_cell();
    let current = cell.read().map(|g| g.clone()).unwrap_or_default();
    if current.is_empty() {
        let fresh = generate_project_hash();
        if let Ok(mut g) = cell.write() {
            if g.is_empty() {
                *g = fresh.clone();
            }
            g.clone()
        } else {
            fresh
        }
    } else {
        current
    }
}

/// プロセス単位 `project_uuid` セルを上書きする（chunk-persist 値の反映用）。
///
/// Plugin の `initialize()` が `params.project_uuid`（`#[persist = "project_uuid"]`）
/// から取得した値で本関数を呼ぶ想定。空文字列を渡してもセルを空にする
/// （次回 `process_project_hash()` 呼び出しで lazy fallback が走る）。
pub fn set_project_uuid(uuid: String) {
    if let Ok(mut g) = project_uuid_cell().write() {
        *g = uuid;
    }
}

/// セル現在値を peek する（lazy fallback なし）。
///
/// `process_project_hash` と異なり、セルが空（誰も `set_project_uuid` を
/// 呼んでいない状態）であれば空文字列を返す。POST 側の
/// `sync_project_uuid_from_pre` で「PRE が既に値をセットしたか」を判定する
/// のに使う。lazy fallback されると POST 自身が cell を初期化してしまい、
/// PRE 後発時に上書きされる race の起点になるため、明示的に区別する。
pub fn peek_project_uuid() -> String {
    project_uuid_cell()
        .read()
        .map(|g| g.clone())
        .unwrap_or_default()
}

/// プロセス単位 `daw_session_id` の現在値を読み取る。
///
/// B-020 以降は `#[persist = "daw_session_uuid"]` で chunk に保存される
/// 値が `set_daw_session_id()` 経由でセルに反映される。`record_signal.json`
/// の content に同梱され、別 DAW プロセス起源の signal を PRE が誤って
/// ack することを防ぐ cross-process 防壁として使う。
pub fn daw_session_id() -> String {
    let cell = daw_session_id_cell();
    let current = cell.read().map(|g| g.clone()).unwrap_or_default();
    if current.is_empty() {
        let fresh = uuid::Uuid::new_v4().to_string();
        if let Ok(mut g) = cell.write() {
            if g.is_empty() {
                *g = fresh.clone();
            }
            g.clone()
        } else {
            fresh
        }
    } else {
        current
    }
}

/// プロセス単位 `daw_session_id` セルを上書きする（chunk-persist 値の反映用）。
pub fn set_daw_session_id(uuid: String) {
    if let Ok(mut g) = daw_session_id_cell().write() {
        *g = uuid;
    }
}

/// プロセス起動後 1 回だけ旧構造（`default/MIX/`）の cleanup を実行する。
///
/// `Plugin::default()` から呼び出す想定。OnceLock で 1 度きりの実行を保証。
/// 既に `.cleanup_v1_done` flag が立っていれば中身はノーオペ（再 cleanup なし）。
pub fn ensure_legacy_cleanup_done() {
    static DONE: OnceLock<()> = OnceLock::new();
    DONE.get_or_init(|| {
        if let Ok(paths) = storage::StoragePaths::default_macos() {
            let report = storage::cleanup_legacy_v1(&paths);
            log::info!(
                "[startup] cleanup_v1: ran={} removed={} errors={}",
                report.ran, report.removed, report.errors
            );
        }
    });
}

// ── SignalState（advisor_signal_state_spec SS-1）──────────────────────────

/// Audio Thread が宣言する信号状態。パイプライン全体がこの値に従う。
///
/// - Audio Thread が毎 `process()` で `AtomicU8` に書き込む（ロックフリー）
/// - Measure Thread が毎サイクル先頭で読み取り、Active 以外なら compute() スキップ
/// - IO Thread が JSON に `"signal_state"` として出力
#[repr(u8)]
#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum SignalState {
    /// 信号あり・バイパスなし → 計測する
    Active = 0,
    /// DAW バイパス中 → 計測しない
    Bypassed = 1,
    /// transport 停止 or バッファ全ゼロ → 計測しない
    Inactive = 2,
}

impl SignalState {
    /// `AtomicU8` の値から変換。未知の値は `Inactive`（安全側に倒す）。
    pub fn from_u8(v: u8) -> Self {
        match v {
            0 => Self::Active,
            1 => Self::Bypassed,
            _ => Self::Inactive,
        }
    }

    /// JSON 出力用の文字列。
    pub fn as_str(self) -> &'static str {
        match self {
            Self::Active => "active",
            Self::Bypassed => "bypassed",
            Self::Inactive => "inactive",
        }
    }
}

/// `AtomicU8` から `SignalState` を読み取るヘルパー。
pub fn load_signal_state(atom: &AtomicU8) -> SignalState {
    SignalState::from_u8(atom.load(Ordering::Relaxed))
}

/// `AtomicU8` に `SignalState` を書き込むヘルパー。
pub fn store_signal_state(atom: &AtomicU8, state: SignalState) {
    atom.store(state as u8, Ordering::Relaxed);
}

// ── 計測結果型 ───────────────────────────────────────────────────────────────

/// Perceptual Spectral Balance 要約（low / mid / high）。
///
/// Kirin-original metric。
///
/// guardian_61 C-3 (Daisuke 判断 経路A、破壊的変更):
/// - low  : Bark 1–8   ISO 532-1 specific loudness 由来 [dB]   (~20 Hz–920 Hz)
/// - mid  : Bark 9–16  ISO 532-1 specific loudness 由来 [dB]   (~920 Hz–3400 Hz)
/// - high : Bark 21–24 + 15.5k–20kHz **FFT エネルギー由来** [dB] (5800 Hz–20000 Hz)
///
/// **重要**: high の意味論は C-3 で変更された。旧仕様 (Bark 17–20 specific loudness)
/// は廃止し、並存させない。high のみ FFT power → 10·log10 で dB 化しているため、
/// low/mid (sone/Bark 由来) と数値の order が異なる場合がある。
/// 比較・加算する際は単位が異なることに注意。
///
/// ISO 532-1 由来のフィールド (n_prime[20] / psb_bark[20] / sharpness / n_prime_total)
/// は引き続き Bark 1–20 specific loudness ベースで C-3 では変更されない。
#[derive(Debug, Clone, Default)]
pub struct PsbSummary {
    /// Bark 1–8 specific loudness sum (sone/Bark) → dB
    pub low: f64,
    /// Bark 9–16 specific loudness sum (sone/Bark) → dB
    pub mid: f64,
    /// Bark 21–24 + 15.5k–20kHz FFT energy (linear power) → dB
    pub high: f64,
}

/// 7 項目計測結果（G-52-02 4項目 + Phase D 3項目）。
///
/// Measure Thread が更新し、IO Thread と GUI Thread が読む。
/// `Arc<Mutex<MeasureResult>>` で共有する。
#[derive(Debug, Clone, Default)]
pub struct MeasureResult {
    /// LUFS-M: ITU-R BS.1770-4 Momentary Loudness（400ms sliding）。
    /// 信号なし・ウィンドウ未満は `None`（GUI 表示 `---`）。
    pub lufs_m: Option<f64>,

    /// True Peak: ITU-R BS.1770-4 Annex 2（4× oversampling, dBTP）。
    /// プレイバック開始からの累積最大値（running max）。
    /// 0 dBTP 超 → GUI で赤表示（G-53-02）。
    pub true_peak: Option<f64>,

    /// Crest Factor: peak_dBFS − RMS_dBFS（400ms window）。
    /// peak はサンプルピーク（True Peak ではない。PSR 計算との整合）。
    pub crest: Option<f64>,

    /// PSR: peak_dBFS − LUFS_S（3s Short-term Loudness）。
    /// Watch GUI では非表示。IO Thread が /tmp/ に書き込む（G-52-02）。
    /// 3 秒未満は `None`。
    pub psr: Option<f64>,

    /// Phase D: Filtered total loudness N'(t) [sone]。
    /// Phase D 初期化中（起動直後数フレーム）は `None`。
    /// 48 kHz 以外は常に `None`。
    pub n_prime_total: Option<f64>,

    /// Phase D: DIN 45692 Sharpness S(t) [acum]。
    pub sharpness: Option<f64>,

    /// Phase D: PSB 要約（low / mid / high）[dB]。
    pub psb_summary: Option<PsbSummary>,

    /// Phase D: 20-Bark 帯域別 N'(t,z) [sone/Bark]（サブ3-A-1）。
    /// plugin_data/.../post/*.json `Frame.n_prime[20]` に直接書き込む値。
    /// Phase D 初期化中 / 48 kHz 以外は `None`。
    pub n_prime: Option<[f64; 20]>,

    /// Phase D: 20-Bark 帯域別 PSB 比率（サブ3-A）。
    /// plugin_data/.../post/*.json `psb_snapshots[].psb` に直接書き込む値。
    /// 3 帯域集約 (low/mid/high, dB) は `psb_summary` 側。
    pub psb_bark: Option<[f64; 20]>,
}
