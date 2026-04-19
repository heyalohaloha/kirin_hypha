mod editor;

use kirin_measure::{
    load_license_safe, spawn_io_thread_post, spawn_measure_thread, spawn_watchdog,
    store_signal_state, DeltaResult, License, MeasureResult, RecordStateMachine, SignalState,
    WatchdogParams, N_CHANNELS, RING_BUFFER_SECONDS,
};
use nih_plug::prelude::*;
use nih_plug_egui::EguiState;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use uuid::Uuid;

/// Kirin Hypha POST — マスタリングチェイン後段計測プラグイン。
///
/// # 4層隔離（guardian_53 T-8 追加後）
/// ```text
/// Audio Thread    — process(): バッファコピーのみ（R-12）。絶対に止まらない
/// Measure Thread  — 4項目計測（ebur128 + スライディングウィンドウ）
/// IO Thread       — PRE ファイル読み + Δ算出 + post_{id}.json アトミック書き込み
/// Watchdog Thread — Measure / IO の is_finished() 監視・自動再起動（T-8）
/// ```
pub struct HyphaPost {
    params: Arc<HyphaPostParams>,
    editor_state: Arc<EguiState>,
    instance_id: String,

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

    // ── Record モード（サブ2-B: RecordStateMachine と実配線済）─────
    /// Record 状態機械（Watch ↔ Record の遷移。license 二重 gate 付き）。
    record_sm: Arc<RecordStateMachine>,
    /// Record 信号 ACK 済か（false=Standby, true=Active。サブ3 で IO Thread が更新）
    record_acknowledged: Arc<AtomicBool>,
    /// ペアリング表示ラベル（例: "PRE abc123…"）。サブ3 で IO Thread が更新。
    pair_label: Arc<Mutex<String>>,
    /// Identity.json から読んだライセンス値（サブ2-A: GUI 分岐に使用）。
    /// 起動時に 1 回だけ読み込み、降格反映は Step 4 T-6 で別途実装。
    license: Arc<License>,
}

#[derive(Params)]
struct HyphaPostParams {
    /// DAW バイパスパラメータ（SS-3: nih-plug `kIsBypass` フラグ）。
    #[id = "bypass"]
    pub bypass: BoolParam,
}

impl Default for HyphaPostParams {
    fn default() -> Self {
        Self {
            bypass: BoolParam::new("Bypass", false)
                .make_bypass()
                .with_value_to_string(formatters::v2s_bool_bypass())
                .with_string_to_value(formatters::s2v_bool_bypass()),
        }
    }
}

impl Default for HyphaPost {
    fn default() -> Self {
        Self {
            params: Arc::new(HyphaPostParams::default()),
            editor_state: EguiState::from_size(300, 200),
            instance_id: Uuid::new_v4().to_string(),
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
            license: Arc::new(load_license_safe()),
        }
    }
}

impl Drop for HyphaPost {
    fn drop(&mut self) {
        // T-7 先取り: Record 中の場合は Watch へ戻す（plugin_data/ 書込停止）
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
        editor::create_post_editor(
            Arc::clone(&self.editor_state),
            self.instance_id.clone(),
            Arc::clone(&self.measure_result),
            Arc::clone(&self.delta_result),
            Arc::clone(&self.measure_alive),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.record_sm),
            Arc::clone(&self.record_acknowledged),
            Arc::clone(&self.pair_label),
            Arc::clone(&self.license),
        )
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
        let io_handle = spawn_io_thread_post(
            self.instance_id.clone(),
            Arc::clone(&self.measure_result),
            Arc::clone(&self.delta_result),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.io_shutdown),
        );

        // ── Watchdog Thread 起動（T-8） ──────────────────────────────
        let restart_io = {
            let instance_id = self.instance_id.clone();
            let measure_result = Arc::clone(&self.measure_result);
            let delta_result = Arc::clone(&self.delta_result);
            let signal_state = Arc::clone(&self.signal_state);
            move |new_shutdown: Arc<AtomicBool>| {
                spawn_io_thread_post(
                    instance_id.clone(),
                    Arc::clone(&measure_result),
                    Arc::clone(&delta_result),
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
        // ── Heartbeat: process() が呼ばれていることを Measure Thread に通知 ──
        self.heartbeat.fetch_add(1, Ordering::Relaxed);

        // ── SS-2/SS-3: SignalState 判定（process() 先頭）──────────────
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

        // R-12: バッファを変更しない（in-place 素通し）
        if state == SignalState::Active {
            if let Some(producer) = &mut self.ring_producer {
                for channel_samples in buffer.iter_samples() {
                    for sample in channel_samples {
                        let _ = producer.push(*sample);
                    }
                }
            }
        }

        // T-8: Watchdog が差し込んだ新 Producer を低頻度でスワップ
        self.process_counter = self.process_counter.wrapping_add(1);
        if self.process_counter & 0xFF == 0 {
            // ── SS-9 診断ログ（低頻度: ~5秒に1回）──────────────────
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

/// 入力バッファが全ゼロ（無音）かを判定する。
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
