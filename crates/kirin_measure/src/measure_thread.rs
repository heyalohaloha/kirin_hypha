//! Measure Thread 起動モジュール。
//!
//! guardian_53 3層隔離:
//! - Audio Thread は ring buffer Producer に書くだけ（このファイルに触れない）
//! - Measure Thread（このファイル）はクラッシュしても Audio Thread を止めない
//! - IO Thread（T-4/T-5）は measure_result を読むだけ

use crate::phase_d::stream::PhaseDStream;
use crate::phase_d::tables::FieldType;
use crate::{load_signal_state, store_signal_state, MeasureEngine, MeasureResult, PsbSummary, SignalState, N_CHANNELS};
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

/// Measure Thread のループ間隔（guardian_53: 推奨 100ms）。
const LOOP_SLEEP: Duration = Duration::from_millis(100);

/// heartbeat が何回連続で変化しなければ process() 停止と判定するか。
/// 2 回 × 100ms = 200ms。48kHz/512 で ~93 回/100ms の process() が呼ばれるため、
/// 200ms 無変化は確実に停止。
const HEARTBEAT_STALE_THRESHOLD: u32 = 2;

/// Measure Thread を起動し、JoinHandle を返す。
///
/// # 引数
/// - `consumer`    : Audio Thread からサンプルを受け取る rtrb Consumer（所有権を移動）
/// - `sample_rate` : 現在のサンプルレート（Hz）
/// - `result`      : IO Thread / GUI と共有する計測結果（Arc<Mutex<MeasureResult>>）
/// - `signal_state`: Audio Thread が書き込む信号状態
/// - `shutdown`    : `true` に設定されたらループを終了するフラグ
/// - `heartbeat`   : Audio Thread が毎 process() でインクリメントするカウンタ。
///   200ms 以上変化なし → process() 停止と判定し signal_state を Inactive に上書き。
///   DAW がバイパス時に process() を停止するケース（Studio One 等）に対応。
///
/// # 3層隔離保証
/// このスレッドが panic しても Audio Thread は継続する。
/// panic → JoinHandle::is_finished() で検出 → T-8 で自動再起動する。
pub fn spawn_measure_thread(
    mut consumer: rtrb::Consumer<f32>,
    sample_rate: u32,
    result: Arc<Mutex<MeasureResult>>,
    signal_state: Arc<AtomicU8>,
    shutdown: Arc<AtomicBool>,
    heartbeat: Arc<AtomicU32>,
) -> JoinHandle<()> {
    thread::spawn(move || {
        // MeasureEngine 初期化。失敗したらこのスレッドのみ終了（Audio Thread は継続）。
        let mut engine = match MeasureEngine::new(sample_rate, N_CHANNELS) {
            Ok(e) => e,
            Err(e) => {
                log::error!("[MeasureThread] MeasureEngine::new failed: {}", e);
                return;
            }
        };

        // Phase D streaming processor（48 kHz 限定。他レートでは None）
        let mut phase_d: Option<PhaseDStream> = if sample_rate == 48000 {
            Some(PhaseDStream::new(FieldType::Free))
        } else {
            None
        };
        // Phase D stereo→mono 変換バッファ（再アロケーション回避）
        let mut mono_buf: Vec<f64> = if phase_d.is_some() {
            Vec::with_capacity(sample_rate as usize / N_CHANNELS)
        } else {
            Vec::new()
        };
        // Phase D 最新結果（ループをまたいで保持。engine が結果を返した時にマージ）
        let mut latest_pd: Option<crate::phase_d::stream::PhaseDResult> = None;

        // f32 → f64 変換バッファ（ループをまたいで再利用。再アロケーションを避ける）
        let mut chunk_f64: Vec<f64> = Vec::with_capacity(sample_rate as usize);

        // 前回ループの SignalState を保持し、非Active→Active 遷移を検出する（SS-8）。
        let mut prev_active = false;

        // heartbeat stall detection: process() が停止したことを検出する。
        // Studio One 等、バイパス時に process() を呼ばなくなる DAW に対応。
        let mut last_heartbeat: u32 = heartbeat.load(Ordering::Relaxed);
        let mut hb_stale_count: u32 = 0;

        log::info!("[MeasureThread] started (sample_rate={})", sample_rate);

        loop {
            // シャットダウン確認（initialize() が呼ばれたか、プラグインが Drop されたか）
            if shutdown.load(Ordering::Relaxed) {
                break;
            }

            // ── Heartbeat stall detection ──────────────────────────
            // process() が 200ms 以上呼ばれていなければ Inactive に上書きする。
            // process() 再開時に Audio Thread が即座に正しい state を書き戻す。
            let current_hb = heartbeat.load(Ordering::Relaxed);
            if current_hb == last_heartbeat {
                hb_stale_count += 1;
                if hb_stale_count == HEARTBEAT_STALE_THRESHOLD {
                    log::info!(
                        "[MeasureThread] heartbeat stale ({}x{}ms) — process() stopped, overriding to Inactive",
                        HEARTBEAT_STALE_THRESHOLD, LOOP_SLEEP.as_millis()
                    );
                    store_signal_state(&signal_state, SignalState::Inactive);
                }
            } else {
                if hb_stale_count >= HEARTBEAT_STALE_THRESHOLD {
                    log::info!("[MeasureThread] heartbeat resumed — process() restarted");
                }
                hb_stale_count = 0;
                last_heartbeat = current_hb;
            }

            // ── SS-4: SignalState チェック ──────────────────────────
            let state = load_signal_state(&signal_state);
            if state != SignalState::Active {
                prev_active = false;
                // Bypassed / Inactive → compute() スキップ。
                // リングバッファに残っているサンプルは破棄する
                // （Active に戻ったとき古いデータで計測しないため）。
                let stale = consumer.slots();
                for _ in 0..stale {
                    let _ = consumer.pop();
                }
                // 計測結果をクリア（GUI が即座に `---` 表示できるようにする）
                match result.lock() {
                    Ok(mut guard) => *guard = MeasureResult::default(),
                    Err(e) => log::warn!("[MeasureThread] result Mutex poisoned: {}", e),
                }
                thread::sleep(LOOP_SLEEP);
                continue;
            }

            // ── SS-8: 非Active→Active 遷移時にエンジンリセット ──────
            // 前セッションの ebur128 FIR 遅延ライン / tp_window / window_400ms を
            // クリアして、新セッション最初のチャンクが汚染されるのを防ぐ。
            if !prev_active {
                engine.reset();
                if let Some(pd) = &mut phase_d {
                    pd.reset();
                }
                latest_pd = None;
                prev_active = true;
                log::info!("[MeasureThread] engine reset on Active transition");
            }

            // ── Active: リングバッファから全サンプルを取得して計測 ────
            let available = consumer.slots();
            if available > 0 {
                chunk_f64.clear();
                for _ in 0..available {
                    match consumer.pop() {
                        Ok(s) => chunk_f64.push(s as f64), // f32 → f64
                        Err(_) => break,                   // Consumer が空になった
                    }
                }

                // Phase D: stereo→mono 変換 + push（48 kHz のみ）
                if let Some(pd) = &mut phase_d {
                    mono_buf.clear();
                    for ch in chunk_f64.chunks_exact(N_CHANNELS) {
                        mono_buf.push((ch[0] + ch[1]) * 0.5);
                    }
                    let pd_results = pd.push(&mono_buf);
                    if let Some(last) = pd_results.last() {
                        latest_pd = Some(last.clone());
                    }
                }

                // 100ms チャンク単位で計測し、揃ったら結果を共有領域に書き込む
                if let Some(mut new_result) = engine.push(&chunk_f64) {
                    // Phase D 結果をマージ（初期化中は None のまま）
                    if let Some(ref pd_r) = latest_pd {
                        new_result.n_prime_total = Some(pd_r.loudness);
                        new_result.sharpness = Some(pd_r.sharpness);
                        new_result.psb_summary = Some(compute_psb_summary(&pd_r.psb));
                    }
                    match result.lock() {
                        Ok(mut guard) => *guard = new_result,
                        Err(e) => {
                            log::warn!("[MeasureThread] result Mutex poisoned: {}", e);
                        }
                    }
                }
            }

            // 100ms スリープ（guardian_53 推奨間隔）
            thread::sleep(LOOP_SLEEP);
        }

        log::info!("[MeasureThread] terminated");
    })
}

/// 20-Bark PSB を 3 帯域に集約して dB 表現にする。
///
/// - low:  Bark 1–8  (indices 0–7)
/// - mid:  Bark 9–16 (indices 8–15)
/// - high: Bark 17–20 (indices 16–19)
fn compute_psb_summary(psb: &[f64; 20]) -> PsbSummary {
    let low: f64 = psb[0..8].iter().sum();
    let mid: f64 = psb[8..16].iter().sum();
    let high: f64 = psb[16..20].iter().sum();
    let tiny = 1e-12;
    PsbSummary {
        low: 10.0 * (low + tiny).log10(),
        mid: 10.0 * (mid + tiny).log10(),
        high: 10.0 * (high + tiny).log10(),
    }
}
