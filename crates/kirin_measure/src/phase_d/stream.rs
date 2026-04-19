//! Phase D ISO 532-1 streaming adapter.
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
use super::tables::*;
use super::{core_loudness, calc_slopes, sharpness, spectral_balance};

// ── Result ──────────────────────────────────────────────────────

/// Per-frame Phase D output at 2 kHz rate.
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
}

// ── Streaming Processor ─────────────────────────────────────────

/// Streaming Phase D processor.
///
/// Accepts mono 48 kHz chunks of arbitrary size. Returns per-frame results
/// at 2 kHz rate (one frame per 24 input samples).
///
/// Call `reset()` on SS-8 Active transition to clear all filter state.
pub struct PhaseDStream {
    field_type: FieldType,
    fb: FbState,
    decay: DecayState,
    tw: TwState,
}

impl PhaseDStream {
    pub fn new(field_type: FieldType) -> Self {
        Self {
            field_type,
            fb: FbState::new(),
            decay: DecayState::new(),
            tw: TwState::new(),
        }
    }

    /// Feed mono 48 kHz samples. Returns Phase D results for each output frame.
    ///
    /// Output count = number of decimation boundaries crossed in this chunk.
    /// May return empty Vec if fewer than DEC_FACTOR samples accumulated.
    pub fn push(&mut self, mono_48k: &[f64]) -> Vec<PhaseDResult> {
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

        // 6. Sharpness + PSB (stateless per frame)
        let n = filtered.len();
        let sharp = sharpness::compute(&slopes.n_specific[..n], &filtered);
        let psb = spectral_balance::compute(&slopes.n_specific[..n], &filtered);

        (0..n)
            .map(|i| PhaseDResult {
                loudness: filtered[i],
                sharpness: sharp[i],
                n_specific: slopes.n_specific[i],
                psb: psb[i],
            })
            .collect()
    }

    /// Clear all internal state (SS-8: non-Active → Active transition).
    pub fn reset(&mut self) {
        self.fb.reset();
        self.decay.reset();
        self.tw.reset();
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

        // Streaming pipeline (single push = same data, same deltas)
        let mut stream = PhaseDStream::new(FieldType::Free);
        let sr = stream.push(&signal);

        assert_eq!(sr.len(), filtered.len());
        for i in 0..sr.len() {
            let ld = (sr[i].loudness - filtered[i]).abs();
            assert!(ld < 1e-10, "Loudness frame {i}: diff={ld}");
            let sd = (sr[i].sharpness - sharp_b[i]).abs();
            assert!(sd < 1e-10, "Sharpness frame {i}: diff={sd}");
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
}
