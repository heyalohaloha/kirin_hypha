mod editor;

use kirin_measure::{
    daw_session_id, ensure_legacy_cleanup_done, identity_instance_attach, identity_instance_detach,
    live_window, load_license_safe, process_project_hash,
    set_daw_session_id, set_project_uuid, spawn_io_thread_pre, spawn_measure_thread,
    spawn_watchdog, store_signal_state, License, LivenessEvaluator, MeasureResult, RecordStateMachine,
    SessionSummary, SignalState, WatchdogIo, WatchdogParams, N_CHANNELS, RING_BUFFER_SECONDS,
};
use nih_plug::prelude::*;
use nih_plug_egui::EguiState;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU64, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::thread::JoinHandle;
use uuid::Uuid;

/// Kirin Hypha PRE — upstream measurement plugin.
///
/// # 4層隔離
/// ```text
/// Audio Thread    — process(): buffer copy only. Never blocks.
/// Measure Thread  — 4項目計測（ebur128 + スライディングウィンドウ）
/// IO Thread       — $TMPDIR/kirin/{project_hash}/{instance_id}/pre.json にアトミック書込
/// Watchdog Thread — Measure / IO thread health monitoring with auto-restart
/// ```
///
/// #  /  後の識別子モデル
/// - `params.instance_id`     : 永続 UUID（`#[persist]`）。plugin instance ごと
/// - `params.project_uuid`    : 永続 UUID（`#[persist]`）。プロジェクト chunk 共有
/// - `params.daw_session_uuid`: 永続 UUID（`#[persist]`）。プロジェクト chunk 共有
/// - `project_hash` field     : `process_project_hash()` 経由で cell から読む chunk-persistent 値
/// - `daw_session_id` field   : `daw_session_id()` 経由で cell から読む chunk-persistent 値
pub struct HyphaPre {
    params: Arc<HyphaPreParams>,
    editor_state: Arc<EguiState>,

    /// プロセス単位 `project_hash`（plugin_data path のルートセグメント）。
    project_hash: String,
    /// プロセス単位 `daw_session_id`（POST signal の cross-process 防壁 filter）。
    daw_session_id: String,

    ring_producer: Option<rtrb::Producer<f32>>,
    measure_result: Arc<Mutex<MeasureResult>>,
    /// B-043: Record セッション集計値共有スロット（Measure → IO Thread）。
    session_summary: Arc<Mutex<Option<SessionSummary>>>,
    /// B-076: ring 満杯で測定 ring に push できなかった累積サンプル数。Audio Thread が
    /// 計数し、io_thread が per-Record dropped_samples を .kirin に焼き込む。
    overflow: Arc<AtomicU64>,

    measure_shutdown: Arc<AtomicBool>,
    io_shutdown: Arc<AtomicBool>,

    watchdog_shutdown: Arc<AtomicBool>,
    watchdog_handle: Option<JoinHandle<()>>,
    pending_producer: Arc<Mutex<Option<rtrb::Producer<f32>>>>,
    measure_alive: Arc<AtomicBool>,
    process_counter: u32,

    signal_state: Arc<AtomicU8>,
    heartbeat: Arc<AtomicU32>,
    /// B-118: 単一鮮度評価器。PRE は pair lock を持たないため editor では消費しないが、
    /// spawn_measure_thread / watchdog 再起動に渡す（signature 統一）。
    liveness: Arc<LivenessEvaluator>,

    record_sm: Arc<RecordStateMachine>,
    recording: Arc<AtomicBool>,
    record_acknowledged: Arc<AtomicBool>,
    license: Arc<License>,

    preset_available: Arc<AtomicBool>,

    /// B-025 Group B-2/B-3 / Gap-19/20: io_thread → editor のステータス行通知。
    /// `None` = 通常 / `Some("Record stopped: ...")` = 連続失敗閾値到達後の文言。
    record_error_message: Arc<RwLock<Option<String>>>,
}

#[derive(Params)]
struct HyphaPreParams {
    #[id = "bypass"]
    pub bypass: BoolParam,

    /// プロジェクト保存時に永続化される instance UUID。
    /// POST が pending を立てる際の `target_pre_instance_id` 識別子として使われる。
    ///
    /// B-022 段階 1: chunk-restore タイミングで host が `set_state` を発行した
    /// 直後の最新値を io_thread が lazy-read で拾えるよう `Arc<RwLock<String>>`
    /// にする。`#[persist]` は `Arc<RwLock<String>>` も RwLock<String> 同様に
    /// 扱う（nih-plug `params/persist.rs` `impl_persistent_arc!` 経由 / chunk
    /// JSON 形式は変わらない / 既存プロジェクト互換）。
    #[persist = "instance_id"]
    pub instance_id: Arc<RwLock<String>>,

    /// プロジェクト chunk に永続化される project UUID（ / ）。
    /// 同一プロジェクト内の PRE/POST instances 全てが同じ値を共有する。
    /// plugin_data path の `{project_uuid}/` セグメントとして使う。
    #[persist = "project_uuid"]
    pub project_uuid: RwLock<String>,

    /// プロジェクト chunk に永続化される daw session UUID（ / ）。
    /// `record_signal.json` の content に同梱され、別 DAW プロセス起源の
    /// signal を PRE が誤って ack することを防ぐ cross-process 防壁。
    #[persist = "daw_session_uuid"]
    pub daw_session_uuid: RwLock<String>,

    /// ユーザー定義 Name (B-023 / G-115-40 案 A-3)。
    /// ASCII 0x20-0x7E のみ / 16 文字以内 / 空文字許容 (UUID 短縮 fallback)。
    /// 既存 instance_id / project_uuid と同じ Arc<RwLock<String>> パターンで
    /// editor / io_thread と共有 (B-022 段階 1 lazy-read 経路に乗せる)。
    #[persist = "name"]
    pub name: Arc<RwLock<String>>,
}

impl Default for HyphaPreParams {
    fn default() -> Self {
        Self {
            bypass: BoolParam::new("Bypass", false)
                .make_bypass()
                .with_value_to_string(formatters::v2s_bool_bypass())
                .with_string_to_value(formatters::s2v_bool_bypass()),
            instance_id: Arc::new(RwLock::new(Uuid::new_v4().to_string())),
            project_uuid: RwLock::new(Uuid::new_v4().to_string()),
            daw_session_uuid: RwLock::new(Uuid::new_v4().to_string()),
            name: Arc::new(RwLock::new(String::new())),
        }
    }
}

/// B-027 段階 2: `kirin_measure::sanitize_name` への re-export (DRY 統合)。
///
/// 旧実装 (B-023 段階 1) は kirin_measure crate に移動し hypha_pre / hypha_post の
/// 両 cdylib から共通参照される単一情報源となった。本 re-export は既存の
/// `editor.rs` / `name_persist_tests` / 外部参照を破壊しないために維持する。
pub use kirin_measure::sanitize_name;

impl Default for HyphaPre {
    fn default() -> Self {
        // B-110: live インスタンス refcount +1（破棄は Drop で −1）。下の project_hash /
        // daw_session_id seed より前に置き、seed 時点で refcount>0 を保証する。
        identity_instance_attach();
        ensure_legacy_cleanup_done();

        // B-118: heartbeat を先に作り、同一 Arc を観測する単一鮮度評価器を構築する。
        let heartbeat = Arc::new(AtomicU32::new(0));
        let liveness = Arc::new(LivenessEvaluator::new(Arc::clone(&heartbeat), live_window()));

        Self {
            params: Arc::new(HyphaPreParams::default()),
            editor_state: EguiState::from_size(300, 200),
            project_hash: process_project_hash(),
            daw_session_id: daw_session_id(),
            ring_producer: None,
            measure_result: Arc::new(Mutex::new(MeasureResult::default())),
            session_summary: Arc::new(Mutex::new(None)),
            overflow: Arc::new(AtomicU64::new(0)),
            measure_shutdown: Arc::new(AtomicBool::new(false)),
            io_shutdown: Arc::new(AtomicBool::new(false)),
            watchdog_shutdown: Arc::new(AtomicBool::new(false)),
            watchdog_handle: None,
            pending_producer: Arc::new(Mutex::new(None)),
            measure_alive: Arc::new(AtomicBool::new(true)),
            process_counter: 0,
            signal_state: Arc::new(AtomicU8::new(SignalState::Inactive as u8)),
            heartbeat,
            liveness,
            record_sm: Arc::new(RecordStateMachine::new()),
            recording: Arc::new(AtomicBool::new(false)),
            record_acknowledged: Arc::new(AtomicBool::new(false)),
            license: Arc::new(load_license_safe()),
            preset_available: Arc::new(AtomicBool::new(false)),
            record_error_message: Arc::new(RwLock::new(None)),
        }
    }
}

/// `RwLock<String>` 永続フィールドから現在値を読む（panic-safe）。
///
/// B-022 段階 1: `instance_id` は `Arc<RwLock<String>>` 化されたが、project_uuid
/// / daw_session_uuid の `RwLock<String>` 経路（chunk-restored 値の cell 反映）
/// では引き続き本ヘルパを使う。`instance_id` の lazy-read は io_thread_post の
/// `read_instance_id_arc` を再利用する。
fn read_persisted_string(field: &RwLock<String>) -> String {
    field.read().ok().map(|g| g.clone()).unwrap_or_default()
}

impl Drop for HyphaPre {
    fn drop(&mut self) {
        self.record_sm.exit_record();

        self.watchdog_shutdown.store(true, Ordering::Relaxed);
        self.measure_shutdown.store(true, Ordering::Relaxed);
        self.io_shutdown.store(true, Ordering::Relaxed);

        if let Some(h) = self.watchdog_handle.take() {
            let _ = h.join();
        }

        // B-110: live インスタンス refcount −1。0 到達で共有 identity セル（project_uuid /
        // daw_session_id）を clear し、次プロジェクトの再 seed を許す。egui 殻は role-scoped 追加
        // セルを持たないので clear_extra は no-op。PRE io_thread は spawn 時 snapshot
        // （`project_hash: String`）でセル非参照のため、未 join でも clear と非競合（A-3）。
        identity_instance_detach(|| {});
    }
}

impl Plugin for HyphaPre {
    const NAME: &'static str = "Kirin Hypha PRE";
    const VENDOR: &'static str = "Kirin";
    const URL: &'static str = "";
    const EMAIL: &'static str = "";
    const VERSION: &'static str = env!("CARGO_PKG_VERSION");

    const AUDIO_IO_LAYOUTS: &'static [AudioIOLayout] = &[AudioIOLayout {
        main_input_channels: NonZeroU32::new(2),
        main_output_channels: NonZeroU32::new(2),
        ..AudioIOLayout::const_default()
    }];

    const MIDI_INPUT: MidiConfig = MidiConfig::None;
    const MIDI_OUTPUT: MidiConfig = MidiConfig::None;
    const SAMPLE_ACCURATE_AUTOMATION: bool = false;

    type SysExMessage = ();
    type BackgroundTask = ();

    fn params(&self) -> Arc<dyn Params> {
        self.params.clone()
    }

    fn editor(&mut self, _async_executor: AsyncExecutor<Self>) -> Option<Box<dyn Editor>> {
        editor::create_pre_editor(
            Arc::clone(&self.editor_state),
            Arc::clone(&self.measure_result),
            Arc::clone(&self.measure_alive),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.recording),
            Arc::clone(&self.record_acknowledged),
            Arc::clone(&self.preset_available),
            Arc::clone(&self.params.name),
            Arc::clone(&self.params.instance_id),
            Arc::clone(&self.record_error_message),
        )
    }

    fn initialize(
        &mut self,
        _audio_io_layout: &AudioIOLayout,
        buffer_config: &BufferConfig,
        context: &mut impl InitContext<Self>,
    ) -> bool {
        context.set_latency_samples(0);

        // chunk-persist 値（params.project_uuid / params.daw_session_uuid）を
        // プロセス cell に反映。PRE は authority として無条件に上書きする。
        // params 側は nih-plug の persist 機構が initialize() より前に
        // chunk から復元済み（新規プロジェクトの場合は Default::default() の
        // 値がそのまま残る）。
        let persisted_project_uuid = read_persisted_string(&self.params.project_uuid);
        let persisted_session_uuid = read_persisted_string(&self.params.daw_session_uuid);
        if !persisted_project_uuid.is_empty() {
            set_project_uuid(persisted_project_uuid);
        }
        if !persisted_session_uuid.is_empty() {
            set_daw_session_id(persisted_session_uuid);
        }
        // セル更新後に struct field を再読込（IO/Editor が参照する値）。
        self.project_hash = process_project_hash();
        self.daw_session_id = daw_session_id();

        // B-023 段階 1: chunk-restored Name の正規化補助 (経路 1)。
        // 主の正規化は読み取り側 lazy-read で `sanitize_name` を毎回かける
        // 経路 2 が担う。本ブロックは「永続化されている値自体を正規化済に
        // 保つ」念のための片付けで、initialize() 後に 1 度だけ書き戻す。
        // R-28 機能的沈黙: 違反値の検出で UI エラーを出さない。
        let restored_name = self
            .params
            .name
            .read()
            .ok()
            .map(|g| g.clone())
            .unwrap_or_default();
        let sanitized = sanitize_name(&restored_name);
        if sanitized != restored_name {
            if let Ok(mut g) = self.params.name.write() {
                *g = sanitized;
            }
        }

        self.watchdog_shutdown.store(true, Ordering::Relaxed);
        self.measure_shutdown.store(true, Ordering::Relaxed);
        self.io_shutdown.store(true, Ordering::Relaxed);
        if let Some(h) = self.watchdog_handle.take() {
            let _ = h.join();
        }

        self.watchdog_shutdown.store(false, Ordering::Relaxed);
        self.measure_shutdown.store(false, Ordering::Relaxed);
        self.io_shutdown.store(false, Ordering::Relaxed);
        self.measure_alive.store(true, Ordering::Relaxed);
        if let Ok(mut slot) = self.pending_producer.lock() {
            *slot = None;
        }
        self.process_counter = 0;

        let capacity =
            (buffer_config.sample_rate as usize) * RING_BUFFER_SECONDS * N_CHANNELS;
        let (producer, consumer) = rtrb::RingBuffer::new(capacity);
        self.ring_producer = Some(producer);

        self.heartbeat.store(0, Ordering::Relaxed);

        let measure_handle = spawn_measure_thread(
            consumer,
            buffer_config.sample_rate as u32,
            Arc::clone(&self.measure_result),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.measure_shutdown),
            Arc::clone(&self.liveness),
            Arc::clone(&self.record_sm),
            Arc::clone(&self.session_summary),
        );

        // B-022 段階 1: io_thread には Arc<RwLock<String>> 共有 → tick ごと lazy-read。
        // 旧 String snapshot は initialize() 時点で凍結されるため、setState 経由の
        // 後続復元・再 initialize で値が変わったケースで instance_id が陳腐化していた。
        let sample_rate = buffer_config.sample_rate as u32;
        let instance_id_arc = Arc::clone(&self.params.instance_id);
        let project_hash = self.project_hash.clone();
        let daw_session_id = self.daw_session_id.clone();
        // B-023 段階 3: PRE Name を IO Thread に共有 (Arc<RwLock<String>>)。
        // ack 時に signal.paired_pre_name に書く。chunk-restore 後の最新値を
        // tick ごと lazy-read。
        let name_arc = Arc::clone(&self.params.name);
        let io_handle = spawn_io_thread_pre(
            Arc::clone(&instance_id_arc),
            project_hash.clone(),
            daw_session_id.clone(),
            sample_rate,
            Arc::clone(&self.record_sm),
            Arc::clone(&self.recording),
            Arc::clone(&self.record_acknowledged),
            Arc::clone(&self.license),
            Arc::clone(&self.measure_result),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.io_shutdown),
            Arc::clone(&name_arc),
            Arc::clone(&self.record_error_message),
            Arc::clone(&self.session_summary),
            Arc::clone(&self.overflow), // B-076: per-Record dropped_samples
        );

        let restart_io = {
            let instance_id_arc = Arc::clone(&instance_id_arc);
            let project_hash = project_hash.clone();
            let daw_session_id = daw_session_id.clone();
            let record_sm = Arc::clone(&self.record_sm);
            let recording = Arc::clone(&self.recording);
            let record_acknowledged = Arc::clone(&self.record_acknowledged);
            let license = Arc::clone(&self.license);
            let measure_result = Arc::clone(&self.measure_result);
            let signal_state = Arc::clone(&self.signal_state);
            let name_arc = Arc::clone(&name_arc);
            let record_error_message = Arc::clone(&self.record_error_message);
            let session_summary = Arc::clone(&self.session_summary);
            let overflow = Arc::clone(&self.overflow); // B-076
            move |new_shutdown: Arc<AtomicBool>| {
                spawn_io_thread_pre(
                    Arc::clone(&instance_id_arc),
                    project_hash.clone(),
                    daw_session_id.clone(),
                    sample_rate,
                    Arc::clone(&record_sm),
                    Arc::clone(&recording),
                    Arc::clone(&record_acknowledged),
                    Arc::clone(&license),
                    Arc::clone(&measure_result),
                    Arc::clone(&signal_state),
                    new_shutdown,
                    Arc::clone(&name_arc),
                    Arc::clone(&record_error_message),
                    Arc::clone(&session_summary),
                    Arc::clone(&overflow), // B-076: per-Record dropped_samples
                )
            }
        };

        self.watchdog_handle = Some(spawn_watchdog(WatchdogParams {
            sample_rate: buffer_config.sample_rate as u32,
            ring_capacity: capacity,
            measure_result: Arc::clone(&self.measure_result),
            signal_state: Arc::clone(&self.signal_state),
            evaluator: Arc::clone(&self.liveness),
            measure_shutdown: Arc::clone(&self.measure_shutdown),
            measure_alive: Arc::clone(&self.measure_alive),
            pending_producer: Arc::clone(&self.pending_producer),
            measure_handle,
            // B-118: egui は io を eager spawn 済み handle で渡す（既存挙動）。
            io: WatchdogIo::Eager {
                io_shutdown: Arc::clone(&self.io_shutdown),
                io_handle,
                restart_io: Box::new(restart_io),
            },
            watchdog_shutdown: Arc::clone(&self.watchdog_shutdown),
            join_on_shutdown: false, // egui=detach（既存挙動・default 不変）
            record_sm: Arc::clone(&self.record_sm),
            session_summary: Arc::clone(&self.session_summary),
        }));

        true
    }

    fn reset(&mut self) {}

    fn process(
        &mut self,
        buffer: &mut Buffer,
        _aux: &mut AuxiliaryBuffers,
        context: &mut impl ProcessContext<Self>,
    ) -> ProcessStatus {
        self.heartbeat.fetch_add(1, Ordering::Relaxed);

        let bypass_val = self.params.bypass.value();
        let transport = context.transport();
        let playing = transport.playing;
        let silent = buffer_is_silent(buffer);

        let state = if bypass_val {
            SignalState::Bypassed
        } else if !playing || silent {
            SignalState::Inactive
        } else {
            SignalState::Active
        };
        store_signal_state(&self.signal_state, state);

        if state == SignalState::Active {
            if let Some(producer) = &mut self.ring_producer {
                for channel_samples in buffer.iter_samples() {
                    for sample in channel_samples {
                        // B-076: ring 満杯時は無言で捨てず累積計数（per-Record で .kirin に露出）。
                        if producer.push(*sample).is_err() {
                            self.overflow.fetch_add(1, Ordering::Relaxed);
                        }
                    }
                }
            }
        }

        self.process_counter = self.process_counter.wrapping_add(1);
        if self.process_counter & 0xFF == 0 {
            log::info!(
                "[PRE diag] state={:?} bypass={} playing={} silent={} buf_len={}",
                state, bypass_val, playing, silent,
                buffer.samples()
            );

            if let Ok(mut slot) = self.pending_producer.try_lock() {
                if let Some(new_producer) = slot.take() {
                    self.ring_producer = Some(new_producer);
                    log::info!("[PRE process] ring producer swapped after Measure restart");
                }
            }
        }

        ProcessStatus::Normal
    }
}

impl Vst3Plugin for HyphaPre {
    const VST3_CLASS_ID: [u8; 16] = *b"KirinHyphaPREv01";
    const VST3_SUBCATEGORIES: &'static [Vst3SubCategory] =
        &[Vst3SubCategory::Fx, Vst3SubCategory::Analyzer];
}

/// B-107: 無音床のピーク線形しきい値 = -140 dBFS = 10^(-140/20) = 1e-7。
/// Audio Thread で log10 を避けるため線形で比較する（RT 安全 / R-12 精緻化と整合）。
const SILENCE_PEAK_LINEAR: f32 = 1e-7;

/// B-107: 1 サンプルが無音床（|s| < -140 dBFS）か（旧: 厳密 0 比較）。純粋・境界テスト可能。
#[inline]
fn sample_is_silent(sample: f32) -> bool {
    sample.abs() < SILENCE_PEAK_LINEAR
}

fn buffer_is_silent(buffer: &mut Buffer) -> bool {
    for channel in buffer.as_slice() {
        for &sample in channel.iter() {
            if !sample_is_silent(sample) {
                return false;
            }
        }
    }
    true
}

nih_export_vst3!(HyphaPre);

// ── B-023 段階 1: Name フィールド chunk persist + 正規化テスト ─────────────
//
// hypha_pre crate は cdylib のみ (Cargo.toml:7) のため外部 tests/ から
// 直接 import 不可。kirin_measure と同パターンで `#[cfg(test)] mod` 内部配置
// で 5 件の必須テストを揃える。
//
// テスト方針:
// - chunk roundtrip (1-2): nih-plug の `params/persist.rs:5-9` で
//   `serialize_field = serde_json::to_string` / `deserialize_field =
//   serde_json::from_str` を pub re-export している。本テストはその re-export
//   経由 (`nih_plug::params::persist::{serialize_field, deserialize_field}`) で
//   `RwLock<String>` の値を roundtrip し、chunk persist と同経路で値が保たれる
//   ことを構造的に固定する
// - sanitize_name 正規化 (3-5): truncate / 非 ASCII strip / 制御文字 strip
#[cfg(test)]
mod b107_silence_tests {
    use super::{sample_is_silent, SILENCE_PEAK_LINEAR};

    /// B-107: 無音判定の境界（厳密0 / -141dBFS / -139dBFS）。silent iff peak < -140 dBFS。
    #[test]
    fn silence_threshold_boundary() {
        let lin = |dbfs: f32| 10f32.powf(dbfs / 20.0);
        assert!(sample_is_silent(0.0), "厳密0 は無音");
        assert!(sample_is_silent(lin(-141.0)), "-141 dBFS は無音 (< -140)");
        assert!(sample_is_silent(-lin(-141.0)), "-141 dBFS 負符号も無音 (abs)");
        assert!(!sample_is_silent(lin(-139.0)), "-139 dBFS は非無音 (> -140)");
        assert!(!sample_is_silent(-lin(-139.0)), "-139 dBFS 負符号も非無音 (abs)");
        // ちょうど -140 dBFS = SILENCE_PEAK_LINEAR は >= しきい値 → 非無音（peak < -140 のみ無音）。
        assert!(!sample_is_silent(SILENCE_PEAK_LINEAR), "ちょうど -140 dBFS は非無音（境界）");
    }
}

#[cfg(test)]
mod name_persist_tests {
    use super::sanitize_name;
    use nih_plug::params::persist::{deserialize_field, serialize_field};
    use std::sync::RwLock;

    /// Test 1: 空文字保存・復元。Name 未設定の Default::default() 状態 (空文字)
    /// が nih-plug chunk persist と同経路 (serialize_field / deserialize_field)
    /// を経ても空文字のまま戻ることを構造的に固定する。
    #[test]
    fn name_roundtrip_empty() {
        let rw: RwLock<String> = RwLock::new(String::new());
        let json = serialize_field(&*rw.read().unwrap()).expect("serialize empty");
        assert_eq!(json, r#""""#);
        let restored: String = deserialize_field(&json).expect("deserialize empty");
        let rw2: RwLock<String> = RwLock::new(restored);
        assert_eq!(*rw2.read().unwrap(), "");
    }

    /// Test 2: ASCII 16 文字保存・復元。論点 5 (c) 上限 16 文字の境界値が
    /// chunk persist と同経路を経ても完全一致で戻ることを構造的に固定する。
    #[test]
    fn name_roundtrip_ascii_16() {
        let original = "AbcDefGhi1234567"; // 16 文字 (上限)
        assert_eq!(original.len(), 16);
        let rw: RwLock<String> = RwLock::new(original.to_string());
        let json =
            serialize_field(&*rw.read().unwrap()).expect("serialize ascii_16");
        let restored: String =
            deserialize_field(&json).expect("deserialize ascii_16");
        let rw2: RwLock<String> = RwLock::new(restored);
        assert_eq!(*rw2.read().unwrap(), original);
    }

    /// Test 3: sanitize_name が 17 文字目以降を truncate する。
    /// 入力 "a" * 20 → "a" * 16。
    #[test]
    fn name_sanitize_truncates_over_16() {
        let raw = "a".repeat(20);
        let sanitized = sanitize_name(&raw);
        assert_eq!(sanitized.len(), 16);
        assert_eq!(sanitized, "a".repeat(16));
    }

    /// B-077 Test 4: sanitize_name が非 ASCII（日本語）を **保持**する（旧: strip）。
    /// 入力 "日本語Snare" → "日本語Snare"（日本語を残す）。
    #[test]
    fn name_sanitize_keeps_non_ascii() {
        let raw = "日本語Snare";
        let sanitized = sanitize_name(raw);
        assert_eq!(sanitized, "日本語Snare");
    }

    /// Test 5: sanitize_name が制御文字 (0x00-0x1F / 0x7F) を strip する。
    /// 入力 "\x01Snare\x7f" → "Snare" (SOH と DEL を除去)。
    #[test]
    fn name_sanitize_strips_control_chars() {
        let raw = "\x01Snare\x7f";
        let sanitized = sanitize_name(raw);
        assert_eq!(sanitized, "Snare");
    }
}
