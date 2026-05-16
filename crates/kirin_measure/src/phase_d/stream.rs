//!  ISO 532-1 streaming adapter.
//!
//! Wraps the batch modules with persistent IIR state for continuous
//! chunk-by-chunk input from the Measure Thread.
//!
//! State preserved across push():
//! - filter_bank: 28-band SOS + LP filter states + decimation counter
//! - nonlinear_decay: per-band uo/u2 decay states
//! - temporal_weighting: dual LP IIR y_prev
//!
//! Kirin Hypha T-2: ストリーミングアダプタ。

use super::filter_bank::{sos_process, lp_process, SosState, LpState};
use super::stft::{StftProcessor, STFT_FFT_SIZE};
use super::tables::*;
use super::{core_loudness, calc_slopes, sharpness, spectral_balance};

///  stream が前提とする mono サンプリングレート (Hz)。
/// STFT のビン→周波数変換で使用。
const PHASE_D_SAMPLE_RATE: f64 = 48000.0;

// ── Result ──────────────────────────────────────────────────────

/// Per-frame  output at 2 kHz rate.
#[derive(Clone)]
pub struct PhaseDResult {
    /// Filtered total loudness N(t) in sone
    pub loudness: f64,
    /// DIN 45692 Widmann sharpness S(t) in acum
    pub sharpness: f64,
    /// 240-bin specific loudness N'(z) at 0.1 Bark resolution (sone/Bark)
    pub n_specific: [f64; N_SPEC_BINS],
    /// 20-band Perceptual Spectral Balance
    pub psb: [f64; N_BARK],
    /// 20-Bark aggregated specific loudness N'(t) in sone/Bark (サブ3-A-1).
    /// Mean of 12 bins per Bark band; used for Frame.n_prime[20] persistence.
    pub n_prime: [f64; N_BARK],
    ///  C-2: Bark 21-24 (5800-15500 Hz) FFT エネルギー (linear power)。
    /// ISO 532-1 specific loudness (N_BARK=20) とは独立経路で、
    /// STFT の最新完了フレームから取得。STFT 未完了時は `[0.0; 4]`。
    /// **単位が異なる** ため `psb` や `n_prime` と素朴に加算・比較してはならない。
    pub psb_bark21_24: [f64; 4],
    ///  C-2: 15.5k-20kHz FFT 補完帯域のエネルギー (linear power)。
    /// Bark 24 upper (15500 Hz) より上の余剰帯域。同じく STFT 最新フレーム由来。
    pub psb_high_ext_15_5k_20k: f64,
}

// ── Streaming Processor ─────────────────────────────────────────

/// Streaming  processor.
///
/// Accepts mono 48 kHz chunks of arbitrary size. Returns per-frame results
/// at 2 kHz rate (one frame per 24 input samples).
///
/// Call `reset()` on SS-8 Active transition to clear all filter state.
///
///  C-2: ISO 532-1 パイプラインと並列に STFT 経路を保持し、
/// Bark 21-24 + 15.5k-20kHz 補完帯域のエネルギーを `PhaseDResult` に
/// 添付する。両経路は相互独立で、STFT 側は同じ 48 kHz mono 入力を
/// 受けるが ISO 532-1 の数値には一切影響しない。
pub struct PhaseDStream {
    field_type: FieldType,
    fb: FbState,
    decay: DecayState,
    tw: TwState,
    stft: StftProcessor,
    /// STFT 最新完了フレームから算出した Bark 21-24 エネルギー。
    /// 2 kHz 出力フレーム vs STFT hop (~93.75 Hz) のレート差を吸収するため、
    /// 新しい STFT フレームが到着するまで前回値を保持する (ratchet & hold)。
    latest_psb_bark21_24: [f64; 4],
    latest_psb_high_ext: f64,
}

impl PhaseDStream {
    pub fn new(field_type: FieldType) -> Self {
        Self {
            field_type,
            fb: FbState::new(),
            decay: DecayState::new(),
            tw: TwState::new(),
            stft: StftProcessor::new(),
            latest_psb_bark21_24: [0.0; 4],
            latest_psb_high_ext: 0.0,
        }
    }

    /// Feed mono 48 kHz samples. Returns  results for each output frame.
    ///
    /// Output count = number of decimation boundaries crossed in this chunk.
    /// May return empty Vec if fewer than DEC_FACTOR samples accumulated.
    pub fn push(&mut self, mono_48k: &[f64]) -> Vec<PhaseDResult> {
        //  C-2: STFT 経路を先に回す。ISO 532-1 パイプラインとは
        // 独立で、同じ mono サンプルを入力とするが結果は別フィールドに格納。
        // STFT は hop=512 で発火するため、複数フレーム or 0 フレームが返る。
        // 最新完了フレームを保持して後続の 2 kHz 出力に添付する。
        let stft_frames = self.stft.push(mono_48k);
        if let Some(last_spec) = stft_frames.last() {
            self.latest_psb_bark21_24 = spectral_balance::compute_psb_bark21_24_from_fft(
                last_spec.as_slice(),
                PHASE_D_SAMPLE_RATE,
                STFT_FFT_SIZE,
            );
            self.latest_psb_high_ext = spectral_balance::compute_psb_high_ext_15_5k_20k(
                last_spec.as_slice(),
                PHASE_D_SAMPLE_RATE,
                STFT_FFT_SIZE,
            );
            // PSB High bug fix: normalize FFT band powers by total spectral energy.
            // Without this, high uses raw |X[k]|² sums (~35 for pink noise)
            // while low/mid use PSB ratios (~0.3). The 20 dB mismatch makes high
            // physically meaningless. After normalization, high represents the
            // fraction of total energy in the band — same concept as low/mid.
            let total_power: f64 = last_spec.iter().sum();
            if total_power > 1e-12 {
                for v in &mut self.latest_psb_bark21_24 {
                    *v /= total_power;
                }
                self.latest_psb_high_ext /= total_power;
            }
        }

        // 1. Filter bank → SPL frames at 2 kHz
        let spl_frames = self.fb.process(mono_48k);
        if spl_frames.is_empty() {
            return vec![];
        }

        // 2. Core loudness (stateless per frame)
        let core = core_loudness::compute(&spl_frames, self.field_type);

        // 3. Nonlinear decay (stateful: uo/u2 per band)
        let decayed = self.decay.process(&core);
        if decayed.is_empty() {
            return vec![];
        }

        // 4. Calc slopes (stateless per frame)
        let slopes = calc_slopes::compute(&decayed);

        // 5. Temporal weighting (stateful: dual LP IIR)
        let filtered = self.tw.process(&slopes.n_total);

        // 6. Sharpness + PSB + n_prime (stateless per frame)
        let n = filtered.len();
        let sharp = sharpness::compute(&slopes.n_specific[..n], &filtered);
        let psb = spectral_balance::compute(&slopes.n_specific[..n], &filtered);
        let n_prime = spectral_balance::compute_n_prime(&slopes.n_specific[..n]);

        (0..n)
            .map(|i| PhaseDResult {
                loudness: filtered[i],
                sharpness: sharp[i],
                n_specific: slopes.n_specific[i],
                psb: psb[i],
                n_prime: n_prime[i],
                psb_bark21_24: self.latest_psb_bark21_24,
                psb_high_ext_15_5k_20k: self.latest_psb_high_ext,
            })
            .collect()
    }

    /// Clear all internal state (SS-8: non-Active → Active transition).
    pub fn reset(&mut self) {
        self.fb.reset();
        self.decay.reset();
        self.tw.reset();
        //  C-2: STFT 経路も同時にリセット。
        self.stft.reset();
        self.latest_psb_bark21_24 = [0.0; 4];
        self.latest_psb_high_ext = 0.0;
    }
}

// ── Filter Bank Streaming State ─────────────────────────────────

struct FbState {
    sos: [[SosState; 3]; N_BANDS],
    lp: [[LpState; 3]; N_BANDS],
    coeff: [[[f64; 6]; 3]; N_BANDS],
    gain: [f64; N_BANDS],
    b0_lp: [f64; N_BANDS],
    a1_lp: [f64; N_BANDS],
    dec_count: usize,
}

impl FbState {
    fn new() -> Self {
        let mut coeff = [[[0.0f64; 6]; 3]; N_BANDS];
        let mut gain = [0.0f64; N_BANDS];
        let mut b0_lp = [0.0f64; N_BANDS];
        let mut a1_lp = [0.0f64; N_BANDS];

        for band in 0..N_BANDS {
            for s in 0..3 {
                for c in 0..6 {
                    coeff[band][s][c] = FILTER_REF[s][c] - FILTER_DIFF[band][s][c];
                }
            }
            gain[band] = FILTER_GAIN[band];

            let center_freq = 10.0f64.powf((band as f64 - 16.0) / 10.0) * 1000.0;
            let tau = if center_freq <= 1000.0 {
                2.0 / (3.0 * center_freq)
            } else {
                2.0 / 3000.0
            };
            a1_lp[band] = (-1.0 / (REQUIRED_FS as f64 * tau)).exp();
            b0_lp[band] = 1.0 - a1_lp[band];
        }

        FbState {
            sos: [[SosState::default(); 3]; N_BANDS],
            lp: [[LpState::default(); 3]; N_BANDS],
            coeff,
            gain,
            b0_lp,
            a1_lp,
            dec_count: 0,
        }
    }

    /// Process mono 48 kHz samples through 28-band SOS IIR + LP + decimation.
    /// Returns SPL frames at 2 kHz. Filter state persists across calls.
    fn process(&mut self, signal: &[f64]) -> Vec<[f64; N_BANDS]> {
        let mut frames = Vec::with_capacity(signal.len() / DEC_FACTOR + 1);

        for &x in signal {
            let is_dec = self.dec_count == 0;
            let mut spl = [0.0f64; N_BANDS];

            for (band, spl_val) in spl.iter_mut().enumerate() {
                let mut y = self.gain[band] * x;
                for (sos, cf) in self.sos[band].iter_mut().zip(self.coeff[band].iter()) {
                    y = sos_process(sos, cf, y);
                }
                y *= y;
                for lp in &mut self.lp[band] {
                    y = lp_process(lp, self.b0_lp[band], self.a1_lp[band], y);
                }
                if is_dec {
                    *spl_val = 10.0 * ((y + TINY) / I_REF).log10();
                }
            }

            if is_dec {
                frames.push(spl);
            }
            self.dec_count = (self.dec_count + 1) % DEC_FACTOR;
        }

        frames
    }

    fn reset(&mut self) {
        self.sos = [[SosState::default(); 3]; N_BANDS];
        self.lp = [[LpState::default(); 3]; N_BANDS];
        self.dec_count = 0;
    }
}

// ── Nonlinear Decay Streaming State ─────────────────────────────

struct DecayState {
    b: [f64; 6],
    uo_prev: [f64; N_CORE],
    u2_prev: [f64; N_CORE],
    initialized: bool,
}

impl DecayState {
    fn new() -> Self {
        let sample_rate = INTERNAL_FS as f64;
        let delta_t = 1.0 / (sample_rate * NL_ITER as f64);
        let p = (NL_T_VAR + NL_T_LONG) / (NL_T_VAR * NL_T_SHORT);
        let q = 1.0 / (NL_T_SHORT * NL_T_VAR);
        let disc = (p * p / 4.0 - q).sqrt();
        let lambda_1 = -p / 2.0 + disc;
        let lambda_2 = -p / 2.0 - disc;
        let den = NL_T_VAR * (lambda_1 - lambda_2);
        let e1 = (lambda_1 * delta_t).exp();
        let e2 = (lambda_2 * delta_t).exp();

        DecayState {
            b: [
                (e1 - e2) / den,
                ((NL_T_VAR * lambda_2 + 1.0) * e1
                    - (NL_T_VAR * lambda_1 + 1.0) * e2)
                    / den,
                ((NL_T_VAR * lambda_1 + 1.0) * e1
                    - (NL_T_VAR * lambda_2 + 1.0) * e2)
                    / den,
                (NL_T_VAR * lambda_1 + 1.0)
                    * (NL_T_VAR * lambda_2 + 1.0)
                    * (e1 - e2)
                    / den,
                (-delta_t / NL_T_LONG).exp(),
                (-delta_t / NL_T_VAR).exp(),
            ],
            uo_prev: [0.0; N_CORE],
            u2_prev: [0.0; N_CORE],
            initialized: false,
        }
    }

    /// Process core loudness frames with persistent uo/u2 state per band.
    /// Uses delta=0 for the last frame (streaming: next chunk will continue).
    fn process(&mut self, core: &[[f64; N_CORE]]) -> Vec<[f64; N_CORE]> {
        let n = core.len();
        if n == 0 {
            return vec![];
        }

        let mut result = vec![[0.0f64; N_CORE]; n];

        for band in 0..N_CORE {
            let input: Vec<f64> = core.iter().map(|f| f[band]).collect();

            // Deltas for virtual upsampling (last = 0 for streaming continuity)
            let mut delta = vec![0.0f64; n];
            for i in 0..n.saturating_sub(1) {
                delta[i] = (input[i + 1] - input[i]) / NL_ITER as f64;
            }

            // Virtual upsample: NL_ITER sub-samples per frame
            let total_sub = n * NL_ITER;
            let mut ui = Vec::with_capacity(total_sub);
            for f in 0..n {
                for sub in 0..NL_ITER {
                    ui.push(input[f] + sub as f64 * delta[f]);
                }
            }

            let mut uo_p = self.uo_prev[band];
            let mut u2_p = self.u2_prev[band];

            // First-ever sub-sample: uo=ui, u2=initial (matches batch col=0)
            let start = if !self.initialized {
                uo_p = ui[0];
                u2_p = if input[0] >= 1e-5 {
                    input[0] * (1.0 - self.b[5])
                } else {
                    0.0
                };
                result[0][band] = ui[0];
                1
            } else {
                0
            };

            for col in start..total_sub {
                let ui_val = ui[col];

                // Default: uo = ui (attack / no change)
                let mut uo_cur = ui_val;

                let uo2_decay = uo_p * self.b[2] - u2_p * self.b[3];
                if uo_p > u2_p && uo2_decay >= ui_val {
                    uo_cur = uo2_decay;
                }
                let uo2_simple = uo_p * self.b[4];
                if uo_p <= u2_p && uo2_simple >= ui_val {
                    uo_cur = uo2_simple;
                }

                let mut u2_cur = uo_cur;
                let u22 = uo_p * self.b[0] - u2_p * self.b[1];
                if ui_val < uo_p && uo_p > u2_p && u22 <= uo_cur {
                    u2_cur = u22;
                }
                let u2_attack = (u2_p - ui_val) * self.b[5] + ui_val;
                let near_zero = (ui_val - uo_p).abs() < 1e-5;
                if ui_val >= uo_p && !(near_zero && uo_cur <= u2_p) {
                    u2_cur = u2_attack;
                }

                // Record output at first sub-sample of each frame
                if col % NL_ITER == 0 {
                    result[col / NL_ITER][band] = uo_cur;
                }

                uo_p = uo_cur;
                u2_p = u2_cur;
            }

            self.uo_prev[band] = uo_p;
            self.u2_prev[band] = u2_p;
        }

        if !self.initialized {
            self.initialized = true;
        }
        result
    }

    fn reset(&mut self) {
        self.uo_prev = [0.0; N_CORE];
        self.u2_prev = [0.0; N_CORE];
        self.initialized = false;
    }
}

// ── Temporal Weighting Streaming State ──────────────────────────

struct TwState {
    a1_short: f64,
    b0_short: f64,
    a1_long: f64,
    b0_long: f64,
    y_prev_short: f64,
    y_prev_long: f64,
}

impl TwState {
    fn new() -> Self {
        let sr = INTERNAL_FS as f64;
        let li = TW_LP_ITER as f64;
        let a1_s = (-1.0 / (sr * li * TW_TAU_SHORT)).exp();
        let a1_l = (-1.0 / (sr * li * TW_TAU_LONG)).exp();
        TwState {
            a1_short: a1_s,
            b0_short: 1.0 - a1_s,
            a1_long: a1_l,
            b0_long: 1.0 - a1_l,
            y_prev_short: 0.0,
            y_prev_long: 0.0,
        }
    }

    /// Apply dual LP temporal weighting with persistent IIR state.
    fn process(&mut self, loudness: &[f64]) -> Vec<f64> {
        if loudness.is_empty() {
            return vec![];
        }
        let lp_s = lowpass_stream(
            loudness, self.a1_short, self.b0_short, &mut self.y_prev_short,
        );
        let lp_l = lowpass_stream(
            loudness, self.a1_long, self.b0_long, &mut self.y_prev_long,
        );
        lp_s.iter()
            .zip(lp_l.iter())
            .map(|(&s, &l)| TW_WEIGHT_SHORT * s + TW_WEIGHT_LONG * l)
            .collect()
    }

    fn reset(&mut self) {
        self.y_prev_short = 0.0;
        self.y_prev_long = 0.0;
    }
}

/// Streaming 1st-order LP with 24x virtual upsampling.
/// y_prev persists across calls for temporal continuity.
fn lowpass_stream(loudness: &[f64], a1: f64, b0: f64, y_prev: &mut f64) -> Vec<f64> {
    let n = loudness.len();
    let lp_iter = TW_LP_ITER;

    // Deltas for linear interpolation (last = 0 for streaming)
    let mut delta = vec![0.0f64; n];
    for i in 0..n.saturating_sub(1) {
        delta[i] = (loudness[i + 1] - loudness[i]) / lp_iter as f64;
    }

    let mut result = vec![0.0f64; n];
    for f in 0..n {
        let mut ui_val = loudness[f];
        for sub in 0..lp_iter {
            if sub > 0 {
                ui_val += delta[f];
            }
            let y = b0 * ui_val + a1 * *y_prev;
            *y_prev = y;
            if sub == 0 {
                result[f] = y;
            }
        }
    }
    result
}

// ── Tests ───────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    fn gen_1khz_94db(duration_s: f64) -> Vec<f64> {
        let n = (REQUIRED_FS as f64 * duration_s) as usize;
        let peak = 2.0f64.sqrt();
        (0..n)
            .map(|i| peak * (2.0 * PI * 1000.0 * i as f64 / REQUIRED_FS as f64).sin())
            .collect()
    }

    #[test]
    fn test_streaming_produces_output() {
        let signal = gen_1khz_94db(0.1); // 100ms = 4800 samples
        let mut stream = PhaseDStream::new(FieldType::Free);
        let results = stream.push(&signal);
        assert_eq!(results.len(), 200, "4800/24 = 200 frames");
        for r in &results[10..] {
            assert!(r.loudness > 0.0, "94dB tone should produce positive loudness");
        }
    }

    #[test]
    fn test_empty_input() {
        let mut stream = PhaseDStream::new(FieldType::Free);
        assert!(stream.push(&[]).is_empty());
    }

    #[test]
    fn test_sub_frame_no_output() {
        // Fresh stream: dec_count=0, so first sample IS a decimation point.
        // Need 0 samples for 0 output.
        let mut stream = PhaseDStream::new(FieldType::Free);
        assert!(stream.push(&[]).is_empty());
    }

    #[test]
    fn test_frame_count() {
        let mut stream = PhaseDStream::new(FieldType::Free);
        // dec_count=0: samples 0,24 → 2 frames. After: dec_count=0
        assert_eq!(stream.push(&[0.0; 48]).len(), 2);
        // dec_count=0: sample 0 → 1 frame. After: dec_count=0
        assert_eq!(stream.push(&[0.0; 24]).len(), 1);
        // dec_count=0: sample 0 → 1 frame, then 22 more. After: dec_count=23
        assert_eq!(stream.push(&[0.0; 23]).len(), 1);
        // dec_count=23: 1 sample → dec_count wraps to 0. No frame emitted.
        assert_eq!(stream.push(&[0.0; 1]).len(), 0);
        // dec_count=0: sample 0 → 1 frame
        assert_eq!(stream.push(&[0.0; 1]).len(), 1);
    }

    #[test]
    fn test_batch_vs_stream_equivalence() {
        use super::super::{
            filter_bank, nonlinear_decay, temporal_weighting,
        };

        let signal = gen_1khz_94db(0.5);

        // Batch pipeline
        let fb = filter_bank::compute(&signal);
        let core = core_loudness::compute(&fb.spl, FieldType::Free);
        let decayed = nonlinear_decay::compute(&core);
        let slopes = calc_slopes::compute(&decayed);
        let filtered = temporal_weighting::compute(&slopes.n_total);
        let sharp_b = sharpness::compute(&slopes.n_specific, &filtered);
        let n_prime_b = spectral_balance::compute_n_prime(&slopes.n_specific);

        // Streaming pipeline (single push = same data, same deltas)
        let mut stream = PhaseDStream::new(FieldType::Free);
        let sr = stream.push(&signal);

        assert_eq!(sr.len(), filtered.len());
        for i in 0..sr.len() {
            let ld = (sr[i].loudness - filtered[i]).abs();
            assert!(ld < 1e-10, "Loudness frame {i}: diff={ld}");
            let sd = (sr[i].sharpness - sharp_b[i]).abs();
            assert!(sd < 1e-10, "Sharpness frame {i}: diff={sd}");
            for (bark, &expected) in n_prime_b[i].iter().enumerate() {
                let nd = (sr[i].n_prime[bark] - expected).abs();
                assert!(nd < 1e-10, "n_prime frame {i} bark {bark}: diff={nd}");
            }
        }
    }

    #[test]
    fn test_reset_restores_initial_state() {
        let signal = gen_1khz_94db(0.1);
        let mut stream = PhaseDStream::new(FieldType::Free);
        let first = stream.push(&signal);
        stream.reset();
        let second = stream.push(&signal);
        assert_eq!(first.len(), second.len());
        for i in 0..first.len() {
            assert!(
                (first[i].loudness - second[i].loudness).abs() < 1e-10,
                "Frame {i} loudness mismatch after reset"
            );
        }
    }

    // ──  C-2: STFT 経路の end-to-end テスト ──

    /// 48 kHz の正弦波信号を生成 (ピーク値 1.0)。
    fn gen_sine(freq_hz: f64, duration_s: f64) -> Vec<f64> {
        let n = (REQUIRED_FS as f64 * duration_s) as usize;
        (0..n)
            .map(|i| (2.0 * PI * freq_hz * i as f64 / REQUIRED_FS as f64).sin())
            .collect()
    }

    /// 指定帯域に集中しているかを検証するヘルパ。
    /// `target_idx` は 0=Bark21, 1=Bark22, 2=Bark23, 3=Bark24。
    fn assert_dominated_by(psb_bark21_24: [f64; 4], target_idx: usize, min_ratio: f64) {
        let total: f64 = psb_bark21_24.iter().sum();
        assert!(total > 0.0, "total energy must be positive: {psb_bark21_24:?}");
        let ratio = psb_bark21_24[target_idx] / total;
        assert!(
            ratio >= min_ratio,
            "band {target_idx} should dominate ({ratio} < {min_ratio}): {psb_bark21_24:?}"
        );
    }

    #[test]
    fn test_stft_bark21_24_zero_before_first_frame() {
        // STFT は 1024 サンプル (21.3 ms) 蓄積するまで発火しない。
        // ISO 532-1 側 (2 kHz 出力) は 24 サンプルで発火する。
        // 最初の 1023 サンプル以下を入力すると、PhaseDResult.psb_bark21_24 は
        // 初期値 [0.0; 4] のまま返る。
        let mut stream = PhaseDStream::new(FieldType::Free);
        let results = stream.push(&vec![0.5; 1000]);
        // 1000/24 = 41 frames from ISO 532-1 side.
        assert!(!results.is_empty());
        for r in &results {
            assert_eq!(r.psb_bark21_24, [0.0; 4], "STFT not yet fired");
            assert_eq!(r.psb_high_ext_15_5k_20k, 0.0);
        }
    }

    #[test]
    fn test_stft_bark21_dominated_by_7000hz() {
        // 7000 Hz = Bark 21 center。200 ms で十分な STFT フレームが蓄積する。
        let signal = gen_sine(7000.0, 0.2);
        let mut stream = PhaseDStream::new(FieldType::Free);
        let results = stream.push(&signal);
        let last = results.last().expect("non-empty results");
        assert_dominated_by(last.psb_bark21_24, 0, 0.95);
    }

    #[test]
    fn test_stft_bark22_dominated_by_8500hz() {
        let signal = gen_sine(8500.0, 0.2);
        let mut stream = PhaseDStream::new(FieldType::Free);
        let results = stream.push(&signal);
        let last = results.last().expect("non-empty results");
        assert_dominated_by(last.psb_bark21_24, 1, 0.95);
    }

    #[test]
    fn test_stft_bark23_dominated_by_10500hz() {
        let signal = gen_sine(10500.0, 0.2);
        let mut stream = PhaseDStream::new(FieldType::Free);
        let results = stream.push(&signal);
        let last = results.last().expect("non-empty results");
        assert_dominated_by(last.psb_bark21_24, 2, 0.95);
    }

    #[test]
    fn test_stft_bark24_dominated_by_13500hz() {
        let signal = gen_sine(13500.0, 0.2);
        let mut stream = PhaseDStream::new(FieldType::Free);
        let results = stream.push(&signal);
        let last = results.last().expect("non-empty results");
        assert_dominated_by(last.psb_bark21_24, 3, 0.95);
    }

    #[test]
    fn test_stft_high_ext_captures_17khz() {
        // 17 kHz は Bark 24 より上 (15.5k-20k 補完帯域)。
        // Bark 21-24 にほとんど落ちず、psb_high_ext_15_5k_20k に集中する。
        let signal = gen_sine(17000.0, 0.2);
        let mut stream = PhaseDStream::new(FieldType::Free);
        let results = stream.push(&signal);
        let last = results.last().expect("non-empty results");

        let bark_sum: f64 = last.psb_bark21_24.iter().sum();
        let ext = last.psb_high_ext_15_5k_20k;
        assert!(ext > 0.0, "15.5k-20k ext should capture 17 kHz");
        assert!(
            ext / (ext + bark_sum) >= 0.95,
            "17 kHz should concentrate in ext (ext={ext}, bark21_24={bark_sum})"
        );
    }

    #[test]
    fn test_stft_5khz_not_in_bark21_24() {
        // 5 kHz は Bark 21 (6400 Hz lower) より下。Bark 21-24 にほぼ落ちない。
        let signal = gen_sine(5000.0, 0.2);
        let mut stream = PhaseDStream::new(FieldType::Free);
        let results = stream.push(&signal);
        let last = results.last().expect("non-empty results");
        let bark_sum: f64 = last.psb_bark21_24.iter().sum();
        assert!(
            bark_sum < 1e-3,
            "5 kHz should NOT leak into Bark 21-24: got {bark_sum}"
        );
    }

    #[test]
    fn test_stft_reset_clears_held_values() {
        // Reset 後、最新 Bark 21-24 値が初期値に戻る。
        let signal = gen_sine(7000.0, 0.2);
        let mut stream = PhaseDStream::new(FieldType::Free);
        let _ = stream.push(&signal);
        stream.reset();
        // 小チャンク (STFT 発火前) を入れて確認
        let results = stream.push(&vec![0.0; 100]);
        for r in &results {
            assert_eq!(r.psb_bark21_24, [0.0; 4]);
            assert_eq!(r.psb_high_ext_15_5k_20k, 0.0);
        }
    }

    // ──  C-2 回帰保証: ISO 532-1 側に影響がないこと ──

    #[test]
    fn test_iso_532_1_fields_unchanged_by_stft_integration() {
        // STFT 経路追加後も、ISO 532-1 由来のフィールド
        // (loudness / sharpness / n_specific / psb / n_prime) は batch と bit-identical。
        // これは既存の test_batch_vs_stream_equivalence と同等だが、
        //  C-2 のガードとして明示的に再確認する。
        use super::super::{filter_bank, nonlinear_decay, temporal_weighting};
        let signal = gen_1khz_94db(0.5);
        let fb = filter_bank::compute(&signal);
        let core = core_loudness::compute(&fb.spl, FieldType::Free);
        let decayed = nonlinear_decay::compute(&core);
        let slopes = calc_slopes::compute(&decayed);
        let filtered = temporal_weighting::compute(&slopes.n_total);
        let sharp_b = sharpness::compute(&slopes.n_specific, &filtered);
        let psb_b = spectral_balance::compute(&slopes.n_specific, &filtered);
        let n_prime_b = spectral_balance::compute_n_prime(&slopes.n_specific);

        let mut stream = PhaseDStream::new(FieldType::Free);
        let sr = stream.push(&signal);
        assert_eq!(sr.len(), filtered.len());
        for i in 0..sr.len() {
            assert!((sr[i].loudness - filtered[i]).abs() < 1e-10);
            assert!((sr[i].sharpness - sharp_b[i]).abs() < 1e-10);
            for (bark, &expected) in psb_b[i].iter().enumerate() {
                assert!(
                    (sr[i].psb[bark] - expected).abs() < 1e-10,
                    "psb[{bark}] diff at frame {i}"
                );
            }
            for (bark, &expected) in n_prime_b[i].iter().enumerate() {
                assert!(
                    (sr[i].n_prime[bark] - expected).abs() < 1e-10,
                    "n_prime[{bark}] diff at frame {i}"
                );
            }
        }
    }

    #[test]
    fn test_multi_push_continuity() {
        let signal = gen_1khz_94db(0.2);
        let half = signal.len() / 2;

        // Single push
        let mut s1 = PhaseDStream::new(FieldType::Free);
        let single = s1.push(&signal);

        // Two pushes
        let mut s2 = PhaseDStream::new(FieldType::Free);
        let mut split = s2.push(&signal[..half]);
        split.extend(s2.push(&signal[half..]));

        assert_eq!(single.len(), split.len());
        // Interior frames should be very close (boundary delta=0 effect is small)
        for i in 10..single.len().saturating_sub(10) {
            let diff = (single[i].loudness - split[i].loudness).abs();
            assert!(
                diff < 0.5,
                "Frame {i}: single={:.4}, split={:.4}, diff={diff:.6}",
                single[i].loudness, split[i].loudness,
            );
        }
    }

    // ── PSB High bug fix: pink noise regression test ──

    /// Generate synthetic pink noise (1/f spectrum, -3 dB/octave).
    /// Uses Paul Kellet's pinking filter (well-known approximation to -3 dB/oct).
    fn gen_pink_noise(n_samples: usize, seed: u64) -> Vec<f64> {
        // Simple LCG for reproducible pseudo-random white noise
        let mut rng_state = seed;
        let mut pink = Vec::with_capacity(n_samples);

        let mut b0 = 0.0f64;
        let mut b1 = 0.0f64;
        let mut b2 = 0.0f64;
        let mut b3 = 0.0f64;
        let mut b4 = 0.0f64;
        let mut b5 = 0.0f64;
        let mut b6 = 0.0f64;

        for _ in 0..n_samples {
            rng_state = rng_state.wrapping_mul(6364136223846793005).wrapping_add(1442695040888963407);
            let white = (rng_state >> 33) as f64 / (1u64 << 31) as f64 - 1.0;

            // Paul Kellet's pinking filter (-3.01 dB/octave, accurate 40 Hz–20 kHz)
            b0 = 0.99886 * b0 + white * 0.0555179;
            b1 = 0.99332 * b1 + white * 0.0750759;
            b2 = 0.96900 * b2 + white * 0.1538520;
            b3 = 0.86650 * b3 + white * 0.3104856;
            b4 = 0.55000 * b4 + white * 0.5329522;
            b5 = -0.7616 * b5 - white * 0.0168980;
            let sample = b0 + b1 + b2 + b3 + b4 + b5 + b6 + white * 0.5362;
            b6 = white * 0.115926;

            pink.push(sample * 0.05); // scale to reasonable amplitude
        }
        pink
    }

    /// PSB High bug fix regression test: for pink noise (1/f spectrum),
    /// PSB summary must satisfy high < mid and high < low.
    /// Before fix: high ≈ +15 dB (raw FFT power, unnormalized).
    /// After fix: high ≈ -7 to -10 dB (normalized fraction).
    #[test]
    fn test_psb_high_pink_noise_monotone_decreasing() {
        use crate::measure_thread::tests::compute_psb_summary_pub;

        let signal = gen_pink_noise(48000 * 2, 42); // 2 seconds
        let mut stream = PhaseDStream::new(FieldType::Free);
        let results = stream.push(&signal);
        assert!(!results.is_empty(), "should produce results");

        // Use last result (steady-state after STFT has fired multiple times)
        let last = results.last().unwrap();
        let psb_summary = compute_psb_summary_pub(
            &last.psb,
            &last.psb_bark21_24,
            last.psb_high_ext_15_5k_20k,
        );

        // For pink noise: low/mid should be > high
        // (1/f means less energy at higher frequencies)
        assert!(
            psb_summary.high < psb_summary.low,
            "Pink noise: high ({:.3}) must be < low ({:.3})",
            psb_summary.high, psb_summary.low
        );
        assert!(
            psb_summary.high < psb_summary.mid,
            "Pink noise: high ({:.3}) must be < mid ({:.3})",
            psb_summary.high, psb_summary.mid
        );
        // Sanity: high should be negative dB (fraction < 1.0)
        assert!(
            psb_summary.high < 0.0,
            "Pink noise: high ({:.3}) should be negative dB (normalized fraction)",
            psb_summary.high
        );
    }
}
