mod editor;

use kirin_measure::{
    daw_session_id, ensure_legacy_cleanup_done, load_license_safe, process_project_hash,
    set_daw_session_id, set_project_uuid, spawn_io_thread_pre, spawn_measure_thread,
    spawn_watchdog, store_signal_state, License, MeasureResult, RecordStateMachine, SignalState,
    WatchdogParams, N_CHANNELS, RING_BUFFER_SECONDS,
};
use nih_plug::prelude::*;
use nih_plug_egui::EguiState;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::thread::JoinHandle;
use uuid::Uuid;

/// Kirin Hypha PRE — マスタリングチェイン前段計測プラグイン。
///
/// # 4層隔離
/// ```text
/// Audio Thread    — process(): バッファコピーのみ（R-12）。絶対に止まらない
/// Measure Thread  — 4項目計測（ebur128 + スライディングウィンドウ）
/// IO Thread       — $TMPDIR/kirin/{project_hash}/{instance_id}/pre.json にアトミック書込
/// Watchdog Thread — Measure / IO の is_finished() 監視・自動再起動（T-8）
/// ```
///
/// # B-020 / γ-3 後の識別子モデル
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

    measure_shutdown: Arc<AtomicBool>,
    io_shutdown: Arc<AtomicBool>,

    watchdog_shutdown: Arc<AtomicBool>,
    watchdog_handle: Option<JoinHandle<()>>,
    pending_producer: Arc<Mutex<Option<rtrb::Producer<f32>>>>,
    measure_alive: Arc<AtomicBool>,
    process_counter: u32,

    signal_state: Arc<AtomicU8>,
    heartbeat: Arc<AtomicU32>,

    record_sm: Arc<RecordStateMachine>,
    recording: Arc<AtomicBool>,
    record_acknowledged: Arc<AtomicBool>,
    license: Arc<License>,

    preset_available: Arc<AtomicBool>,
}

#[derive(Params)]
struct HyphaPreParams {
    #[id = "bypass"]
    pub bypass: BoolParam,

    /// プロジェクト保存時に永続化される instance UUID（A-3 修正後）。
    /// POST が pending を立てる際の `target_pre_instance_id` 識別子として使われる。
    ///
    /// B-022 段階 1: chunk-restore タイミングで host が `set_state` を発行した
    /// 直後の最新値を io_thread が lazy-read で拾えるよう `Arc<RwLock<String>>`
    /// にする。`#[persist]` は `Arc<RwLock<String>>` も RwLock<String> 同様に
    /// 扱う（nih-plug `params/persist.rs` `impl_persistent_arc!` 経由 / chunk
    /// JSON 形式は変わらない / 既存プロジェクト互換）。
    #[persist = "instance_id"]
    pub instance_id: Arc<RwLock<String>>,

    /// プロジェクト chunk に永続化される project UUID（B-020 / γ-3）。
    /// 同一プロジェクト内の PRE/POST instances 全てが同じ値を共有する。
    /// plugin_data path の `{project_uuid}/` セグメントとして使う。
    #[persist = "project_uuid"]
    pub project_uuid: RwLock<String>,

    /// プロジェクト chunk に永続化される daw session UUID（B-020 / γ-3）。
    /// `record_signal.json` の content に同梱され、別 DAW プロセス起源の
    /// signal を PRE が誤って ack することを防ぐ cross-process 防壁。
    #[persist = "daw_session_uuid"]
    pub daw_session_uuid: RwLock<String>,
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
        }
    }
}

impl Default for HyphaPre {
    fn default() -> Self {
        ensure_legacy_cleanup_done();

        Self {
            params: Arc::new(HyphaPreParams::default()),
            editor_state: EguiState::from_size(300, 200),
            project_hash: process_project_hash(),
            daw_session_id: daw_session_id(),
            ring_producer: None,
            measure_result: Arc::new(Mutex::new(MeasureResult::default())),
            measure_shutdown: Arc::new(AtomicBool::new(false)),
            io_shutdown: Arc::new(AtomicBool::new(false)),
            watchdog_shutdown: Arc::new(AtomicBool::new(false)),
            watchdog_handle: None,
            pending_producer: Arc::new(Mutex::new(None)),
            measure_alive: Arc::new(AtomicBool::new(true)),
            process_counter: 0,
            signal_state: Arc::new(AtomicU8::new(SignalState::Inactive as u8)),
            heartbeat: Arc::new(AtomicU32::new(0)),
            record_sm: Arc::new(RecordStateMachine::new()),
            recording: Arc::new(AtomicBool::new(false)),
            record_acknowledged: Arc::new(AtomicBool::new(false)),
            license: Arc::new(load_license_safe()),
            preset_available: Arc::new(AtomicBool::new(false)),
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
            Arc::clone(&self.heartbeat),
        );

        // B-022 段階 1: io_thread には Arc<RwLock<String>> 共有 → tick ごと lazy-read。
        // 旧 String snapshot は initialize() 時点で凍結されるため、setState 経由の
        // 後続復元・再 initialize で値が変わったケースで instance_id が陳腐化していた。
        let sample_rate = buffer_config.sample_rate as u32;
        let instance_id_arc = Arc::clone(&self.params.instance_id);
        let project_hash = self.project_hash.clone();
        let daw_session_id = self.daw_session_id.clone();
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
                )
            }
        };

        self.watchdog_handle = Some(spawn_watchdog(WatchdogParams {
            sample_rate: buffer_config.sample_rate as u32,
            ring_capacity: capacity,
            measure_result: Arc::clone(&self.measure_result),
            signal_state: Arc::clone(&self.signal_state),
            heartbeat: Arc::clone(&self.heartbeat),
            measure_shutdown: Arc::clone(&self.measure_shutdown),
            measure_alive: Arc::clone(&self.measure_alive),
            pending_producer: Arc::clone(&self.pending_producer),
            measure_handle,
            io_shutdown: Arc::clone(&self.io_shutdown),
            io_handle,
            restart_io: Box::new(restart_io),
            watchdog_shutdown: Arc::clone(&self.watchdog_shutdown),
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
                        let _ = producer.push(*sample);
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

fn buffer_is_silent(buffer: &mut Buffer) -> bool {
    for channel in buffer.as_slice() {
        for &sample in channel.iter() {
            if sample != 0.0 {
                return false;
            }
        }
    }
    true
}

nih_export_vst3!(HyphaPre);
