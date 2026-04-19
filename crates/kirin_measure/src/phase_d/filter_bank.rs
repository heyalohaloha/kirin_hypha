//! ISO 532-1 Section 6.3: 1/3 octave band filter bank
//!
//! 28-band SOS filtering → squaring → 3-stage LP smoothing → SPL → decimation
//! Output: SPL matrix [28 bands × Ntime] at 2 kHz temporal resolution
//!
//! Kirin Hypha 移植版（Lens `native/src/psychoacoustic/filter_bank.rs` からアルゴリズム同一移植）。
//! Reference: MoSQITo loudness_zwtv/_third_octave_levels.py + _square_and_smooth.py

use super::tables::*;

/// Result of filter bank processing
pub struct FilterBankResult {
    /// SPL values per band per time frame [band][frame]
    /// Shape: N_BANDS × n_time_frames
    pub spl: Vec<[f64; N_BANDS]>,
    /// Number of output time frames
    pub n_frames: usize,
}

/// SOS (Second-Order Section) filter state for one section
#[derive(Clone, Copy, Default)]
pub(super) struct SosState {
    w1: f64,
    w2: f64,
}

/// Process a single sample through one SOS section (Direct Form II Transposed)
///
/// coeff: [b0, b1, b2, a0, a1, a2] where a0 is always 1.0
#[inline]
pub(super) fn sos_process(state: &mut SosState, coeff: &[f64; 6], x: f64) -> f64 {
    let b0 = coeff[0];
    let b1 = coeff[1];
    let b2 = coeff[2];
    // a0 = coeff[3] = 1.0 (always, per ISO 532-1)
    let a1 = coeff[4];
    let a2 = coeff[5];

    let y = b0 * x + state.w1;
    state.w1 = b1 * x - a1 * y + state.w2;
    state.w2 = b2 * x - a2 * y;
    y
}

/// 1st order IIR lowpass filter state
#[derive(Clone, Copy, Default)]
pub(super) struct LpState {
    y1: f64,
}

/// Process a single sample through 1st order IIR LP: y = b0*x + a1*y_prev
#[inline]
pub(super) fn lp_process(state: &mut LpState, b0: f64, a1: f64, x: f64) -> f64 {
    let y = b0 * x + a1 * state.y1;
    state.y1 = y;
    y
}

/// Compute 1/3 octave band SPL matrix from mono 48kHz signal
///
/// # Arguments
/// * `signal` - Mono signal in Pa (0dBFS = 1.0 Pa ≈ 94 dB SPL)
///
/// # Returns
/// SPL matrix [n_frames][28 bands] at 2 kHz resolution
///
/// # Panics
/// If signal length < DEC_FACTOR (24 samples = 0.5ms)
pub fn compute(signal: &[f64]) -> FilterBankResult {
    let n_samples = signal.len();
    assert!(n_samples >= DEC_FACTOR, "Signal too short for filter bank");

    let n_frames = n_samples / DEC_FACTOR;

    // Allocate output: time-major for cache efficiency during later stages
    let mut spl = vec![[0.0f64; N_BANDS]; n_frames];

    // Process each band independently
    for band in 0..N_BANDS {
        // Compute actual SOS coefficients: ref - diff
        let mut coeff = [[0.0f64; 6]; 3];
        for s in 0..3 {
            for c in 0..6 {
                coeff[s][c] = FILTER_REF[s][c] - FILTER_DIFF[band][s][c];
            }
        }
        let gain = FILTER_GAIN[band];

        // Compute smoothing time constant
        let center_freq = 10.0f64.powf((band as f64 - 16.0) / 10.0) * 1000.0;
        let tau = if center_freq <= 1000.0 {
            2.0 / (3.0 * center_freq)
        } else {
            2.0 / 3000.0
        };
        let a1_lp = (-1.0 / (REQUIRED_FS as f64 * tau)).exp();
        let b0_lp = 1.0 - a1_lp;

        // Initialize filter states
        let mut sos_states = [SosState::default(), SosState::default(), SosState::default()];
        let mut lp_states = [LpState::default(), LpState::default(), LpState::default()];

        // Process signal sample by sample
        let mut frame_idx = 0usize;

        for (i, &x) in signal.iter().enumerate() {
            // 3-section SOS cascade with gain
            let mut y = gain * x;
            for s in 0..3 {
                y = sos_process(&mut sos_states[s], &coeff[s], y);
            }

            // Square
            y = y * y;

            // 3-stage LP smoothing cascade
            for lp in &mut lp_states {
                y = lp_process(lp, b0_lp, a1_lp, y);
            }

            // Decimation: take sample at i=0, DEC_FACTOR, 2*DEC_FACTOR, ...
            // Matches MoSQITo's sig[::dec_factor] indexing
            if i % DEC_FACTOR == 0 && frame_idx < n_frames {
                // SPL conversion: 10 * log10((y + tiny) / I_ref)
                spl[frame_idx][band] = 10.0 * ((y + TINY) / I_REF).log10();
                frame_idx += 1;
            }
        }
    }

    FilterBankResult { spl, n_frames }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    /// Generate 1kHz sine at 94 dB SPL (1.0 Pa RMS) for 1 second at 48kHz
    fn gen_1khz_94db(duration_s: f64) -> Vec<f64> {
        let n = (REQUIRED_FS as f64 * duration_s) as usize;
        let rms = 1.0; // 1.0 Pa = 94 dB SPL
        let peak = rms * 2.0f64.sqrt();
        (0..n)
            .map(|i| peak * (2.0 * PI * 1000.0 * i as f64 / REQUIRED_FS as f64).sin())
            .collect()
    }

    #[test]
    fn test_1khz_energy_concentration() {
        let signal = gen_1khz_94db(1.0);
        let result = compute(&signal);

        assert!(result.n_frames > 0);
        // Band 16 = 1000 Hz should have the highest mean SPL
        let n = result.n_frames;
        let mut band_means = [0.0f64; N_BANDS];
        for frame in 0..n {
            for band in 0..N_BANDS {
                band_means[band] += result.spl[frame][band];
            }
        }
        for m in band_means.iter_mut() {
            *m /= n as f64;
        }

        // Find band with max mean SPL
        let max_band = band_means
            .iter()
            .enumerate()
            .max_by(|a, b| a.1.partial_cmp(b.1).unwrap())
            .unwrap()
            .0;
        assert_eq!(max_band, 16, "1kHz should peak at band 16 (1000 Hz), got band {max_band}");
    }

    #[test]
    fn test_silence_low_spl() {
        let signal = vec![1e-20; REQUIRED_FS as usize]; // 1 second silence
        let result = compute(&signal);
        // All SPL values should be very negative (near floor)
        for frame in 0..result.n_frames {
            for band in 0..N_BANDS {
                assert!(
                    result.spl[frame][band] < 0.0,
                    "Silence SPL should be negative, got {} at band {band}",
                    result.spl[frame][band]
                );
            }
        }
    }

    #[test]
    fn test_frame_count() {
        // 1 second @ 48kHz → 48000/24 = 2000 frames
        let signal = gen_1khz_94db(1.0);
        let result = compute(&signal);
        assert_eq!(result.n_frames, 2000);
    }
}
