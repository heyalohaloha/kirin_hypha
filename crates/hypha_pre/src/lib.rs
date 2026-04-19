mod editor;

use kirin_measure::{
    spawn_io_thread_pre, spawn_measure_thread, spawn_watchdog, store_signal_state, MeasureResult,
    SignalState, WatchdogParams, N_CHANNELS, RING_BUFFER_SECONDS,
};
use nih_plug::prelude::*;
use nih_plug_egui::EguiState;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::JoinHandle;
use uuid::Uuid;

/// Kirin Hypha PRE — マスタリングチェイン前段計測プラグイン。
///
/// # 4層隔離（guardian_53 T-8 追加後）
/// ```text
/// Audio Thread    — process(): バッファコピーのみ（R-12）。絶対に止まらない
/// Measure Thread  — 4項目計測（ebur128 + スライディングウィンドウ）
/// IO Thread       — $TMPDIR/kirin/default/MIX/pre_{id}.json にアトミック書き込み
/// Watchdog Thread — Measure / IO の is_finished() 監視・自動再起動（T-8）
/// ```
pub struct HyphaPre {
    params: Arc<HyphaPreParams>,
    editor_state: Arc<EguiState>,
    instance_id: String,

    /// Audio Thread → Measure Thread へのロックフリーリングバッファ（Producer 側）
    ring_producer: Option<rtrb::Producer<f32>>,
    measure_result: Arc<Mutex<MeasureResult>>,

    /// Measure Thread 停止フラグ（Watchdog が再起動時に reset → true 再利用）
    measure_shutdown: Arc<AtomicBool>,
    /// IO Thread 停止フラグ
    io_shutdown: Arc<AtomicBool>,

    // ── T-8: Watchdog ────────────────────────────────────────────────
    /// Watchdog Thread 停止フラグ
    watchdog_shutdown: Arc<AtomicBool>,
    /// Watchdog Thread JoinHandle。Watchdog が Measure / IO Handle も所有する。
    watchdog_handle: Option<JoinHandle<()>>,
    /// Watchdog → Audio Thread: 再起動後の新 Producer を渡すスロット
    pending_producer: Arc<Mutex<Option<rtrb::Producer<f32>>>>,
    /// Measure Thread 生存フラグ（false=停止中→LED 黄、true=稼働中→LED 青）
    measure_alive: Arc<AtomicBool>,
    /// process() 間引きカウンタ（pending_producer チェック）
    process_counter: u32,

    // ── SignalState（SS-1）────────────────────────────────────────────
    /// Audio Thread → Measure/IO Thread 共有。毎 process() で書き込み。
    signal_state: Arc<AtomicU8>,

    // ── Heartbeat（SS-3 代替: process() 停止検出）────────────────────
    /// Audio Thread が毎 process() でインクリメント。Measure Thread が監視し、
    /// 200ms 以上変化なしなら process() 停止と判定して signal_state を Inactive に上書き。
    /// DAW がバイパス時に process() を停止するケース（Studio One 等）に対応。
    heartbeat: Arc<AtomicU32>,

    // ── Record モード（サブ1-C: GUI プレースホルダ。サブ2/3 で実配線） ──
    /// Record モード中か（サブ2 でボタン操作から設定）
    recording: Arc<AtomicBool>,
    /// Record 信号 ACK 済か（PRE は常時 true と見なす初期値。サブ3 で調整可）
    record_acknowledged: Arc<AtomicBool>,
}

#[derive(Params)]
struct HyphaPreParams {
    /// DAW バイパスパラメータ（SS-3: nih-plug `kIsBypass` フラグ）。
    /// ホストがバイパスボタンを操作するとこの値が自動同期される。
    #[id = "bypass"]
    pub bypass: BoolParam,
}

impl Default for HyphaPreParams {
    fn default() -> Self {
        Self {
            bypass: BoolParam::new("Bypass", false)
                .make_bypass()
                .with_value_to_string(formatters::v2s_bool_bypass())
                .with_string_to_value(formatters::s2v_bool_bypass()),
        }
    }
}

impl Default for HyphaPre {
    fn default() -> Self {
        Self {
            params: Arc::new(HyphaPreParams::default()),
            editor_state: EguiState::from_size(300, 200),
            instance_id: Uuid::new_v4().to_string(),
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
            recording: Arc::new(AtomicBool::new(false)),
            record_acknowledged: Arc::new(AtomicBool::new(false)),
        }
    }
}

impl Drop for HyphaPre {
    fn drop(&mut self) {
        // 全スレッドに停止信号（Watchdog が 50ms 以内に終了。Measure / IO も終了する）
        self.watchdog_shutdown.store(true, Ordering::Relaxed);
        self.measure_shutdown.store(true, Ordering::Relaxed);
        self.io_shutdown.store(true, Ordering::Relaxed);

        // Watchdog を join（measure / io の handle は watchdog が所有している）
        if let Some(h) = self.watchdog_handle.take() {
            let _ = h.join();
        }
        // Measure / IO Thread は shutdown フラグで終了し /tmp/ ファイルを削除する。
        // Watchdog join 後 ~100ms 以内に完了する（join は行わない設計）。
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
        )
    }

    fn initialize(
        &mut self,
        _audio_io_layout: &AudioIOLayout,
        buffer_config: &BufferConfig,
        context: &mut impl InitContext<Self>,
    ) -> bool {
        context.set_latency_samples(0);

        // ── 既存 Watchdog を停止（最大 50ms で応答） ─────────────────
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

        // ── Measure Thread 起動（handle は Watchdog に渡す） ─────────
        let measure_handle = spawn_measure_thread(
            consumer,
            buffer_config.sample_rate as u32,
            Arc::clone(&self.measure_result),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.measure_shutdown),
            Arc::clone(&self.heartbeat),
        );

        // ── IO Thread 起動（handle は Watchdog に渡す） ───────────────
        let io_handle = spawn_io_thread_pre(
            self.instance_id.clone(),
            Arc::clone(&self.measure_result),
            Arc::clone(&self.signal_state),
            Arc::clone(&self.io_shutdown),
        );

        // ── Watchdog Thread 起動（T-8） ──────────────────────────────
        let restart_io = {
            let instance_id = self.instance_id.clone();
            let measure_result = Arc::clone(&self.measure_result);
            let signal_state = Arc::clone(&self.signal_state);
            move |new_shutdown: Arc<AtomicBool>| {
                spawn_io_thread_pre(
                    instance_id.clone(),
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
        // Active 時のみリングバッファに push（Bypassed/Inactive 時は計測不要）
        if state == SignalState::Active {
            if let Some(producer) = &mut self.ring_producer {
                for channel_samples in buffer.iter_samples() {
                    for sample in channel_samples {
                        let _ = producer.push(*sample);
                    }
                }
            }
        }

        // T-8: Watchdog が差し込んだ新 Producer を低頻度でスワップ。
        // try_lock() で非ブロッキング。失敗時は次の機会に再試行（~40ms 後）。
        self.process_counter = self.process_counter.wrapping_add(1);
        if self.process_counter & 0xFF == 0 {
            // ── SS-9 診断ログ（低頻度: ~5秒に1回）──────────────────
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

/// 入力バッファが全ゼロ（無音）かを判定する。
///
/// nih-plug の `Buffer` はチャンネル × サンプル。全サンプルが 0.0 なら true。
/// Audio Thread 内で毎 process() 呼ばれるため、early return で高速化。
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
