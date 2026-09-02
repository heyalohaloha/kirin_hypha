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

use super::filter_bank::{lp_process, sos_process, LpState, SosState};
use super::stft::{StftProcessor, STFT_FFT_SIZE};
use super::tables::*;
use super::{calc_slopes, core_loudness, sharpness, spectral_balance};

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

        let Some((slopes, filtered)) = self.push_iso_core(mono_48k) else {
            return vec![];
        };

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

    /// Feed one display-only Sharpness aperture without running the independent STFT/PSB branch.
    ///
    /// This is reserved for a dedicated Perceptual Delta stream. Do not alternate it with
    /// [`Self::push`] on one instance: the ISO 532-1 state advances here, while the independent
    /// high-band STFT state intentionally does not.
    pub(crate) fn push_sharpness_only(&mut self, mono_48k: &[f64]) -> Option<f64> {
        let (slopes, filtered) = self.push_iso_core(mono_48k)?;
        sharpness::compute(&slopes.n_specific[..filtered.len()], &filtered)
            .last()
            .copied()
    }

    fn push_iso_core(&mut self, mono_48k: &[f64]) -> Option<(calc_slopes::SlopesResult, Vec<f64>)> {
        let spl_frames = self.fb.process(mono_48k);
        if spl_frames.is_empty() {
            return None;
        }
        let core = core_loudness::compute(&spl_frames, self.field_type);
        let decayed = self.decay.process(&core);
        if decayed.is_empty() {
            return None;
        }
        let slopes = calc_slopes::compute(&decayed);
        let filtered = self.tw.process(&slopes.n_total);
        Some((slopes, filtered))
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
                ((NL_T_VAR * lambda_2 + 1.0) * e1 - (NL_T_VAR * lambda_1 + 1.0) * e2) / den,
                ((NL_T_VAR * lambda_1 + 1.0) * e1 - (NL_T_VAR * lambda_2 + 1.0) * e2) / den,
                (NL_T_VAR * lambda_1 + 1.0) * (NL_T_VAR * lambda_2 + 1.0) * (e1 - e2) / den,
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
            loudness,
            self.a1_short,
            self.b0_short,
            &mut self.y_prev_short,
        );
        let lp_l = lowpass_stream(loudness, self.a1_long, self.b0_long, &mut self.y_prev_long);
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
#[path = "stream_tests.rs"]
mod tests;
