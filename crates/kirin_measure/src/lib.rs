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
pub mod record;
pub mod record_signal;
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
pub use record::{RecordState, RecordStateMachine, TransitionError};
pub use record_signal::{
    delete_signal, is_timed_out, mark_acknowledged, mark_released, pick_closest_pre, read_signal,
    scan_pre_candidates, signal_path, write_pending, write_signal, PostMetrics, PreCandidate,
    RecordSignal, SignalError, SignalStatus, ACK_TIMEOUT_SECONDS, SIGNAL_FILENAME,
};
pub use storage::{
    load_or_recover, read_identity, write_both, write_identity_atomic, IdentityCache,
    LoadStatus, LoadedIdentity, StorageError, StoragePaths,
};
pub use watchdog::{spawn_watchdog, WatchdogParams};

use std::sync::atomic::{AtomicU8, Ordering};

// ── 共有定数 ────────────────────────────────────────────────────────────────

/// Audio Thread → Measure Thread リングバッファの保持長（秒）。
/// 2 秒: Measure Thread 再起動時の空白を吸収できる余裕（guardian_53 T-2）。
pub const RING_BUFFER_SECONDS: usize = 2;

/// 対応チャンネル数（ステレオ固定）。
pub const N_CHANNELS: usize = 2;

// ── Phase 1.0 固定パラメータ（U-3 未検証のため DAW プロジェクトパス取得は保留）──

/// Phase 1.0 の project_hash 固定値。
/// U-3「nih-plug から DAW プロジェクトパスが取得できるか」が未検証のため、
/// 全インスタンス共通の `"default"` を使う。将来検証成功後に動的化。
pub const PROJECT_HASH_PHASE1: &str = "default";

/// Phase 1.0 の bus 名固定値（MIX bus 前提）。
pub const BUS_PHASE1: &str = "MIX";

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
/// Phase D (ISO 532-1) の 20-Bark PSB を 3 帯域に集約し dB 表現したもの。
/// Kirin-original metric。
#[derive(Debug, Clone, Default)]
pub struct PsbSummary {
    /// Bark 1–8 (low frequency) [dB]
    pub low: f64,
    /// Bark 9–16 (mid frequency) [dB]
    pub mid: f64,
    /// Bark 17–20 (high frequency) [dB]
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
}
