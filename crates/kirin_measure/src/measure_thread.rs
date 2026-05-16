//! Measure Thread 起動モジュール。
//!
//!  3層隔離:
//! - Audio Thread は ring buffer Producer に書くだけ（このファイルに触れない）
//! - Measure Thread（このファイル）はクラッシュしても Audio Thread を止めない
//! - IO Thread（T-4/T-5）は measure_result を読むだけ

use crate::phase_d::stream::PhaseDStream;
use crate::phase_d::tables::FieldType;
use crate::record::RecordStateMachine;
use crate::resampler::ResamplerTo48k;
use crate::{
    engine::SessionSummary, load_signal_state, store_signal_state, MeasureEngine, MeasureResult,
    PsbSummary, SignalState, N_CHANNELS,
};

///  v2:  / EBU R128 を回す内部処理 SR は常に 48 kHz。
/// 入力 SR が 48000 でない場合は Measure Thread 入口で `ResamplerTo48k` を介して
/// 48 kHz に変換してから engine / phase_d に渡す。
const ENGINE_SR: u32 = 48_000;
use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
use std::sync::{Arc, Mutex};
use std::thread::{self, JoinHandle};
use std::time::Duration;

/// Measure Thread のループ間隔（ 推奨 100ms）。
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
///
/// # B-043 (LUFS-I / LRA / PLR)
/// - `record_sm`       : Record mode 中の SS-8 reset 抑止判定に使う（案I-a）。
///   Watch→Record 遷移時は engine.reset() を明示実行してセッション開始時点で
///   ebur128 内部状態をクリアする。Record 中は SS-8 reset をスキップすることで
///   transport 停止/再開を跨いだ LUFS-I / LRA の通算性を確保する。
/// - `session_summary` : Record 中の各ループで `engine.finalize()` の最新値を
///   注入する共有スロット。IO Thread が Record→Watch 遷移時に読み出して
///   `PluginDataWriter::set_session_aggregates()` 経由で JSON に焼き込む。
#[allow(clippy::too_many_arguments)]
pub fn spawn_measure_thread(
    mut consumer: rtrb::Consumer<f32>,
    sample_rate: u32,
    result: Arc<Mutex<MeasureResult>>,
    signal_state: Arc<AtomicU8>,
    shutdown: Arc<AtomicBool>,
    heartbeat: Arc<AtomicU32>,
    record_sm: Arc<RecordStateMachine>,
    session_summary: Arc<Mutex<Option<SessionSummary>>>,
) -> JoinHandle<()> {
    thread::spawn(move || {
        //  v2: 内部処理は 48 kHz 固定。入力 SR が異なる場合のみ
        // ResamplerTo48k で変換して engine / phase_d に渡す。
        let mut engine = match MeasureEngine::new(ENGINE_SR, N_CHANNELS) {
            Ok(e) => e,
            Err(e) => {
                log::error!("[MeasureThread] MeasureEngine::new failed: {}", e);
                return;
            }
        };

        // 入力 SR が 48 kHz の場合はバイパス（ゼロオーバーヘッド経路を維持）。
        // 異なる場合のみ rubato Fft リサンプラを構築する。失敗時は Measure Thread のみ終了。
        let mut resampler: Option<ResamplerTo48k> = if sample_rate != ENGINE_SR {
            match ResamplerTo48k::new(sample_rate, N_CHANNELS) {
                Ok(r) => {
                    log::info!(
                        "[MeasureThread] Resampler {}->{} Hz constructed",
                        sample_rate, ENGINE_SR
                    );
                    Some(r)
                }
                Err(e) => {
                    log::error!(
                        "[MeasureThread] ResamplerTo48k::new({}) failed: {:?}",
                        sample_rate, e
                    );
                    return;
                }
            }
        } else {
            None
        };

        //  streaming processor（ v2: 全 SR 対応のため常に Some 相当）。
        let mut phase_d = PhaseDStream::new(FieldType::Free);
        //  stereo→mono 変換バッファ（48 kHz 1 秒分の余裕で確保）。
        let mut mono_buf: Vec<f64> = Vec::with_capacity(ENGINE_SR as usize / N_CHANNELS);
        //  最新結果（ループをまたいで保持。engine が結果を返した時にマージ）
        let mut latest_pd: Option<crate::phase_d::stream::PhaseDResult> = None;

        // f32 → f64 変換バッファ（ループをまたいで再利用。再アロケーションを避ける）
        let mut chunk_f64: Vec<f64> = Vec::with_capacity(sample_rate as usize);
        // リサンプル後 48kHz interleaved バッファ（resampler が Some のときのみ使う）
        let mut resampled_buf: Vec<f64> =
            Vec::with_capacity(ENGINE_SR as usize * N_CHANNELS / 4);

        // 前回ループの SignalState を保持し、非Active→Active 遷移を検出する（SS-8）。
        let mut prev_active = false;

        // B-043: Record mode 遷移を検出し、Watch→Record 開始時に engine をリセットする。
        // Record 中の SS-8 reset 抑止と組み合わせて、LUFS-I / LRA のセッション通算性を確保する。
        let mut prev_recording = false;

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

            // ── B-043: Record mode 遷移ハンドリング ──────────────────
            // Watch→Record: engine を明示リセットして新セッション開始。
            //   - 前 Watch 期間で accumulate された LUFS-I / LRA / TP の running max
            //     を捨て、セッション通算値を 0 から積み直す。
            // Record→Watch: 共有 session_summary は IO Thread が読んだ後にクリア
            //   される（次の Watch→Record でここで上書き None する）。
            let is_recording = record_sm.is_recording();
            if !prev_recording && is_recording {
                engine.reset();
                phase_d.reset();
                if let Some(rs) = &mut resampler {
                    rs.reset();
                }
                latest_pd = None;
                if let Ok(mut g) = session_summary.lock() {
                    *g = None;
                }
                log::info!("[MeasureThread] engine reset on Watch→Record transition");
            }
            prev_recording = is_recording;

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
            // 前セッションの ebur128 FIR 遅延ライン / tp_window / window_400ms /
            // / リサンプラ FFT overlap / pending 入力 をすべてクリアして、新セッション
            // 最初のチャンクが汚染されるのを防ぐ。
            //
            // B-043 (案I-a): Record mode 中は engine.reset() をスキップする。
            // transport 停止/再開を跨いだ LUFS-I / LRA / TP running max のセッション
            // 通算性を確保するため。Watch 中は従来通り reset する。
            // phase_d / resampler / latest_pd は Record 中でも reset してよい
            // （セッション集計に影響しないため）。
            if !prev_active {
                if !is_recording {
                    engine.reset();
                } else {
                    log::info!("[MeasureThread] SS-8 reset suppressed in Record mode (B-043)");
                }
                phase_d.reset();
                if let Some(rs) = &mut resampler {
                    rs.reset();
                }
                latest_pd = None;
                prev_active = true;
                log::info!(
                    "[MeasureThread] Active transition handled (is_recording={})",
                    is_recording
                );
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

                //  v2: 入力 SR が 48 kHz でない場合のみリサンプリング。
                // resampler 経由では 48 kHz interleaved f64 が `resampled_buf` に追記される。
                // 端数フレームは ResamplerTo48k 内部の pending に保持され次回呼出で消費。
                let chunk_48k: &[f64] = if let Some(rs) = resampler.as_mut() {
                    resampled_buf.clear();
                    if let Err(e) = rs.process(&chunk_f64, &mut resampled_buf) {
                        log::warn!(
                            "[MeasureThread] Resampler error ({}->48000): {:?}, dropping chunk",
                            sample_rate, e
                        );
                        thread::sleep(LOOP_SLEEP);
                        continue;
                    }
                    &resampled_buf
                } else {
                    &chunk_f64
                };

                // : stereo→mono 変換 + push（常に 48 kHz データに対して実行）
                mono_buf.clear();
                for ch in chunk_48k.chunks_exact(N_CHANNELS) {
                    mono_buf.push((ch[0] + ch[1]) * 0.5);
                }
                let pd_results = phase_d.push(&mono_buf);
                if let Some(last) = pd_results.last() {
                    latest_pd = Some(last.clone());
                }

                // 100ms チャンク単位で計測し、揃ったら結果を共有領域に書き込む
                if let Some(mut new_result) = engine.push(chunk_48k) {
                    //  結果をマージ（初期化中は None のまま）
                    if let Some(ref pd_r) = latest_pd {
                        new_result.n_prime_total = Some(pd_r.loudness);
                        new_result.sharpness = Some(pd_r.sharpness);
                        new_result.psb_summary = Some(compute_psb_summary(
                            &pd_r.psb,
                            &pd_r.psb_bark21_24,
                            pd_r.psb_high_ext_15_5k_20k,
                        ));
                        new_result.n_prime = Some(pd_r.n_prime);
                        new_result.psb_bark = Some(pd_r.psb);
                    }
                    match result.lock() {
                        Ok(mut guard) => *guard = new_result,
                        Err(e) => {
                            log::warn!("[MeasureThread] result Mutex poisoned: {}", e);
                        }
                    }
                }

                // B-043: Record 中は session_summary に毎ループの最新 finalize() を反映。
                // IO Thread が Record→Watch 遷移時に直近の値を読み出して JSON に焼く。
                // engine.push() 後に呼ぶことで最新チャンク反映後の値を取れる。
                if is_recording {
                    let summary = engine.finalize();
                    if let Ok(mut g) = session_summary.lock() {
                        *g = Some(summary);
                    }
                }
            }

            // 100ms スリープ（ 推奨間隔）
            thread::sleep(LOOP_SLEEP);
        }

        log::info!("[MeasureThread] terminated");
    })
}

/// PSB low / mid / high を集約して dB 表現にする。
///
///  C-3 (Daisuke 判断 経路A、破壊的変更):
/// - low : Bark 1–8   ISO 532-1 specific loudness (sone/Bark)         → dB
/// - mid : Bark 9–16  ISO 532-1 specific loudness (sone/Bark)         → dB
/// - high: Bark 21–24 + 15.5k–20kHz FFT energy (linear power)         → dB
///
/// 旧 high (Bark 17–20 specific loudness) は完全廃止し並存させない。
/// ISO 532-1 由来の psb[16..20] は計算に使わない（n_prime[20] / psb_bark[20]
/// 側では引き続き露出するため、外部から参照したい場合はそちらを使う）。
fn compute_psb_summary(
    psb: &[f64; 20],
    psb_bark21_24: &[f64; 4],
    psb_high_ext_15_5k_20k: f64,
) -> PsbSummary {
    let low: f64 = psb[0..8].iter().sum();
    let mid: f64 = psb[8..16].iter().sum();
    //  C-3: Bark 21–24 FFT power + 15.5k–20kHz 補完。
    // FFT 経路は別単位 (linear power) のため log10 で dB 化するのは
    // low/mid と同じ「対数スケール」に揃えるためのみで、絶対値の
    // 比較可能性を保証するものではない（PsbSummary doc 参照）。
    let high_lin: f64 = psb_bark21_24.iter().sum::<f64>() + psb_high_ext_15_5k_20k;
    let tiny = 1e-12;
    PsbSummary {
        low: 10.0 * (low + tiny).log10(),
        mid: 10.0 * (mid + tiny).log10(),
        high: 10.0 * (high_lin + tiny).log10(),
    }
}

#[cfg(test)]
pub mod tests {
    use super::compute_psb_summary;

    /// Test-only public wrapper for compute_psb_summary.
    /// Used by stream.rs pink noise test.
    pub fn compute_psb_summary_pub(
        psb: &[f64; 20],
        psb_bark21_24: &[f64; 4],
        psb_high_ext_15_5k_20k: f64,
    ) -> crate::PsbSummary {
        compute_psb_summary(psb, psb_bark21_24, psb_high_ext_15_5k_20k)
    }

    ///  C-3 ガード: PsbSummary.low / .mid は ISO 532-1 由来の
    /// psb[0..16] のみを使い、Bark 21–24 / 15.5k–20k FFT 値の影響を一切
    /// 受けないこと。
    #[test]
    fn psb_summary_low_mid_independent_of_fft_inputs() {
        let psb = [
            0.10, 0.12, 0.11, 0.13, 0.09, 0.10, 0.10, 0.10, // low (Bark 1-8)
            0.05, 0.06, 0.05, 0.04, 0.05, 0.05, 0.06, 0.05, // mid (Bark 9-16)
            0.20, 0.25, 0.22, 0.18,                         // 旧 high 帯域（C-3 で未使用）
        ];
        let s_a = compute_psb_summary(&psb, &[0.0; 4], 0.0);
        let s_b = compute_psb_summary(&psb, &[1.0e3, 2.0e3, 3.0e3, 4.0e3], 5.0e3);
        assert_eq!(s_a.low, s_b.low, "low must not depend on FFT inputs");
        assert_eq!(s_a.mid, s_b.mid, "mid must not depend on FFT inputs");
    }

    /// PsbSummary.high は Bark 21–24 + 15.5k–20k の合計 (linear power) を
    /// 10·log10 で dB 化した値であり、psb[16..20] の値には依存しない。
    #[test]
    fn psb_summary_high_uses_only_fft_inputs() {
        let psb_a = [0.0; 20];
        let mut psb_b = [0.0; 20];
        for v in psb_b[16..20].iter_mut() {
            *v = 0.5; // 旧 high 帯域に大きい値を入れても結果は変わらないこと
        }
        let bark21_24 = [10.0, 20.0, 30.0, 40.0];
        let ext = 50.0;
        let s_a = compute_psb_summary(&psb_a, &bark21_24, ext);
        let s_b = compute_psb_summary(&psb_b, &bark21_24, ext);
        assert_eq!(s_a.high, s_b.high, "high must NOT depend on psb[16..20]");

        // 期待値: 10·log10(10+20+30+40+50 + 1e-12) = 10·log10(150)
        let expected = 10.0 * (150.0_f64).log10();
        assert!(
            (s_a.high - expected).abs() < 1e-9,
            "high = {} expected ≈ {}",
            s_a.high, expected
        );
    }

    /// FFT 入力が全 0 のとき high は ≈ 10·log10(tiny) = -120 dB に張り付く。
    /// (STFT 未発火フレームが PsbSummary を出すケースの挙動定義)
    #[test]
    fn psb_summary_high_floor_when_no_fft_energy() {
        let psb = [0.0; 20];
        let s = compute_psb_summary(&psb, &[0.0; 4], 0.0);
        assert!(s.high < -100.0, "high should floor near -120 dB, got {}", s.high);
    }
}
