mod editor;

use kirin_measure::{
    daw_session_id, ensure_legacy_cleanup_done, load_installation_id_safe, load_license_safe,
    process_project_hash, spawn_io_thread_post, spawn_measure_thread, spawn_watchdog,
    store_signal_state, DeltaResult, License, MeasureResult, RecordStateMachine, SignalState,
    WatchdogParams, N_CHANNELS, RING_BUFFER_SECONDS,
};
use nih_plug::prelude::*;
use nih_plug_egui::EguiState;
use std::sync::atomic::{AtomicBool, AtomicI64, AtomicU32, AtomicU8, Ordering};
use std::sync::{Arc, Mutex, RwLock};
use std::thread::JoinHandle;
use uuid::Uuid;

/// Kirin Hypha POST — マスタリングチェイン後段計測プラグイン。
///
/// # 4層隔離
/// ```text
/// Audio Thread    — process(): バッファコピーのみ（R-12）。絶対に止まらない
/// Measure Thread  — 4項目計測（ebur128 + スライディングウィンドウ）
/// IO Thread       — PRE ファイル読み + Δ算出 + post.json アトミック書き込み
/// Watchdog Thread — Measure / IO の is_finished() 監視・自動再起動（T-8）
/// ```
///
/// # A-3 修正後の識別子モデル
/// - `params.instance_id`: 永続 UUID（`#[persist]`）。project save に同梱され、再オープンで復元
/// - `project_hash`       : DAW プロセス起動時に 1 度だけ生成（OnceLock 経由で PRE/POST 共有）
/// - `daw_session_id`     : DAW プロセス UUID（cross-process 防壁。record_signal content に同梱）
pub struct HyphaPost {
    params: Arc<HyphaPostParams>,
    editor_state: Arc<EguiState>,

    /// プロセス単位 `project_hash`（plugin_data path のルートセグメント）。
    project_hash: String,
    /// プロセス単位 `daw_session_id`（record_signal content の cross-process 防壁）。
    daw_session_id: String,

    ring_producer: Option<rtrb::Producer<f32>>,
    measure_result: Arc<Mutex<MeasureResult>>,
    delta_result: Arc<Mutex<DeltaResult>>,

    measure_shutdown: Arc<AtomicBool>,
    io_shutdown: Arc<AtomicBool>,

    // ── T-8: Watchdog ────────────────────────────────────────────────
    watchdog_shutdown: Arc<AtomicBool>,
    watchdog_handle: Option<JoinHandle<()>>,
    pending_producer: Arc<Mutex<Option<rtrb::Producer<f32>>>>,
    measure_alive: Arc<AtomicBool>,
    process_counter: u32,

    // ── SignalState（SS-1）────────────────────────────────────────────
    signal_state: Arc<AtomicU8>,

    // ── Heartbeat（SS-3 代替: process() 停止検出）────────────────────
    heartbeat: Arc<AtomicU32>,

    // ── Record モード ─────────────────────────────────────────────────
    record_sm: Arc<RecordStateMachine>,
    record_acknowledged: Arc<AtomicBool>,
    /// pair_label（POST GUI に表示。Record 中は "pair: PRE_xxxxxxxx"、Watch 中は空文字）
    pair_label: Arc<Mutex<String>>,
    /// trigger_keep が選定した PRE instance_id（v1.2 (a) cross-instance pair 復元キー）。
    /// Watch 中は None、Keep 成功直後に Some、Stop / 失敗で None。POST IO Thread が
    /// Record 開始時に読み出して plugin_data の `paired_pre_instance_id` に書き込む。
    paired_pre_target: Arc<Mutex<Option<String>>>,
    license: Arc<License>,

    /// preset/*.json が 1 件以上存在するか。
    preset_available: Arc<AtomicBool>,

    // ── T-E/T-F 追加（guardian_77 v3 §9 / §10）────────────────────────
    installation_id: Arc<String>,
    playback_pos_samples: Arc<AtomicI64>,
    playback_sample_rate: Arc<AtomicU32>,
}

#[derive(Params)]
struct HyphaPostParams {
    /// DAW バイパスパラメータ（SS-3: nih-plug `kIsBypass` フラグ）。
    #[id = "bypass"]
    pub bypass: BoolParam,

    /// プロジェクト保存時に永続化される instance UUID（A-3 修正後）。
    /// 初回挿入時に Default::default() で生成、project 再オープン時には
    /// nih-plug の persist 機構が同じ値を復元する。Watch / Record / plugin_data
    /// の path 構築と record_signal の target_pre_instance_id 識別に使う。
    #[persist = "instance_id"]
    pub instance_id: RwLock<String>,
}

impl Default for HyphaPostParams {
    fn default() -> Self {
        Self {
            bypass: BoolParam::new("Bypass", false)
                .make_bypass()
                .with_value_to_string(formatters::v2s_bool_bypass())
                .with_string_to_value(formatters::s2v_bool_bypass()),
            instance_id: RwLock::new(Uuid::new_v4().to_string()),
        }
    }
}

impl Default for HyphaPost {
    fn default() -> Self {
        // 起動時 1 回限りの旧構造 cleanup（OnceLock 内側で flag-guarded）。
        ensure_legacy_cleanup_done();

        Self {
            params: Arc::new(HyphaPostParams::default()),
            editor_state: EguiState::from_size(300, 200),
            project_hash: process_project_hash(),
            daw_session_id: daw_session_id(),
            ring_producer: None,
            measure_result: Arc::new(Mutex::new(MeasureResult::default())),
            delta_result: Arc::new(Mutex::new(DeltaResult::default())),
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
            record_acknowledged: Arc::new(AtomicBool::new(false)),
            pair_label: Arc::new(Mutex::new(String::new())),
            paired_pre_target: Arc::new(Mutex::new(None)),
            license: Arc::new(load_license_safe()),
            preset_available: Arc::new(AtomicBool::new(false)),
            installation_id: Arc::new(load_installation_id_or_empty()),
            playback_pos_samples: Arc::new(AtomicI64::new(i64::MIN)),
            playback_sample_rate: Arc::new(AtomicU32::new(0)),
        }
    }
}

/// Best-effort read of the installation_id for T-E proposals filtering.
fn load_installation_id_or_empty() -> String {
    load_installation_id_safe().unwrap_or_default()
}

/// `params.instance_id` から現在の文字列を読み取る（panic-safe）。
fn read_instance_id(params: &HyphaPostParams) -> String {
    params
        .instance_id
        .read()
        .ok()
        .map(|g| g.clone())
        .unwrap_or_default()
}

impl Drop for HyphaPost {
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

impl Plugin for HyphaPost {
    const NAME: &'static str = "Kirin Hypha POST";
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
        editor::create_post_editor(editor::PostEditorArgs {
            egui_state: Arc::clone(&self.editor_state),
            instance_id: read_instance_id(&self.params),
            project_hash: self.project_hash.clone(),
            daw_session_id: self.daw_session_id.clone(),
            measure: Arc::clone(&self.measure_result),
            delta: Arc::clone(&self.delta_result),
            measure_alive: Arc::clone(&self.measure_alive),
            signal_state: Arc::clone(&self.signal_state),
            record_sm: Arc::clone(&self.record_sm),
            record_acknowledged: Arc::clone(&self.record_acknowledged),
            pair_label: Arc::clone(&self.pair_label),
            paired_pre_target: Arc::clone(&self.paired_pre_target),
            license: Arc::clone(&self.license),
            preset_available: Arc::clone(&self.preset_available),
            installation_id: Arc::clone(&self.installation_id),
            playback_pos_samples: Arc::clone(&self.playback_pos_samples),
            playback_sample_rate: Arc::clone(&self.playback_sample_rate),
        })
    }

    fn initialize(
        &mut self,
        _audio_io_layout: &AudioIOLayout,
        buffer_config: &BufferConfig,
        context: &mut impl InitContext<Self>,
    ) -> bool {
        context.set_latency_samples(0);

        // ── 既存 Watchdog を停止 ─────────────────────────────────────
        self.watchdog_shutdown.store(true, Ordering::Relaxed);
        self.measure_shutdown.store(true, Ordering::Relaxed);
        self.io_shutdown.store(true, Ordering::Relaxed);
        if let Some(h) = self.watchdog_handle.take() {
            let _ = h.join();
        }

        // ── フラグリセット ───────────────────────────────────────────
        self.watchdog_shutdown.store(false, Ordering::Relaxed);
        self.measure_shutdown.store(false, Ordering::Relaxed);
        self.io_shutdown.store(false, Ordering::Relaxed);
        self.measure_alive.store(true, Ordering::Relaxed);
        if let Ok(mut slot) = self.pending_producer.lock() {
            *slot = None;
        }
        self.process_counter = 0;

        // ── リングバッファ再生成 ─────────────────────────────────────
        let capacity =
            (buffer_config.sample_rate as usize) * RING_BUFFER_SECONDS * N_CHANNELS;
        let (producer, consumer) = rtrb::RingBuffer::new(capacity);
        self.ring_producer = Some(producer);

        // ── Heartbeat リセット ────────────────────────────────────────
        self.heartbeat.store(0, Ordering::Relaxed);

        // ── T-F: sample_rate キャッシュ ──────────────────────────────
        self.playback_sample_rate
            .store(buffer_config.sample_rate as u32, Ordering::Relaxed);
        self.playback_pos_samples.store(i64::MIN, Ordering::Relaxed);

        // ── Measure Thread 起動 ──────────────────────────────────────
        let measure_handle = spawn_measure_thread(
            consumer,
            buffer_config.sample_rate as u32,
            Arc::clone(&self.measure_result),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.measure_shutdown),
            Arc::clone(&self.heartbeat),
        );

        // ── IO Thread 起動 ───────────────────────────────────────────
        let sample_rate = buffer_config.sample_rate as u32;
        let instance_id = read_instance_id(&self.params);
        let project_hash = self.project_hash.clone();
        let io_handle = spawn_io_thread_post(
            instance_id.clone(),
            project_hash.clone(),
            sample_rate,
            Arc::clone(&self.record_sm),
            Arc::clone(&self.measure_result),
            Arc::clone(&self.delta_result),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.preset_available),
            Arc::clone(&self.paired_pre_target),
            Arc::clone(&self.io_shutdown),
        );

        // ── Watchdog Thread 起動 ──────────────────────────────────────
        let restart_io = {
            let instance_id = instance_id.clone();
            let project_hash = project_hash.clone();
            let record_sm = Arc::clone(&self.record_sm);
            let measure_result = Arc::clone(&self.measure_result);
            let delta_result = Arc::clone(&self.delta_result);
            let signal_state = Arc::clone(&self.signal_state);
            let preset_available = Arc::clone(&self.preset_available);
            let paired_pre_target = Arc::clone(&self.paired_pre_target);
            move |new_shutdown: Arc<AtomicBool>| {
                spawn_io_thread_post(
                    instance_id.clone(),
                    project_hash.clone(),
                    sample_rate,
                    Arc::clone(&record_sm),
                    Arc::clone(&measure_result),
                    Arc::clone(&delta_result),
                    Arc::clone(&signal_state),
                    Arc::clone(&preset_available),
                    Arc::clone(&paired_pre_target),
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

        let pos = transport.pos_samples().unwrap_or(i64::MIN);
        self.playback_pos_samples.store(pos, Ordering::Relaxed);

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
                "[POST diag] state={:?} bypass={} playing={} silent={} buf_len={}",
                state, bypass_val, playing, silent,
                buffer.samples()
            );

            if let Ok(mut slot) = self.pending_producer.try_lock() {
                if let Some(new_producer) = slot.take() {
                    self.ring_producer = Some(new_producer);
                    log::info!("[POST process] ring producer swapped after Measure restart");
                }
            }
        }

        ProcessStatus::Normal
    }
}

impl Vst3Plugin for HyphaPost {
    const VST3_CLASS_ID: [u8; 16] = *b"KirinHyphaPOSTv1";
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

nih_export_vst3!(HyphaPost);
