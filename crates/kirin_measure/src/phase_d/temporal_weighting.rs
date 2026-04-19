//! ISO 532-1 Temporal Weighting
//!
//! Two parallel 1st-order low-pass filters (3.5ms + 70ms)
//! with 24x virtual upsampling, applied to total loudness N(t) only.
//!
//! N_filtered = 0.47 * LP1(N) + 0.53 * LP2(N)
//!
//! Kirin Hypha 移植版（Lens `native/src/psychoacoustic/temporal_weighting.rs` からアルゴリズム同一移植）。
//! Reference: MoSQITo loudness_zwtv/_temporal_weighting.py + _lowpass_intp.py

use super::tables::*;

/// Apply temporal weighting to total loudness.
///
/// # Arguments
/// * `loudness` - Total loudness N(t) at 2kHz resolution
///
/// # Returns
/// Filtered loudness N(t)
pub fn compute(loudness: &[f64]) -> Vec<f64> {
    let lp1 = lowpass_interp(loudness, TW_TAU_SHORT);
    let lp2 = lowpass_interp(loudness, TW_TAU_LONG);

    lp1.iter()
        .zip(lp2.iter())
        .map(|(&v1, &v2)| TW_WEIGHT_SHORT * v1 + TW_WEIGHT_LONG * v2)
        .collect()
}

/// 1st order low-pass filter with linear interpolation for increased precision.
///
/// Uses 24x virtual upsampling internally.
fn lowpass_interp(loudness: &[f64], tau: f64) -> Vec<f64> {
    let n = loudness.len();
    if n == 0 {
        return vec![];
    }

    let sample_rate = INTERNAL_FS as f64; // 2000 Hz
    let lp_iter = TW_LP_ITER; // 24

    // Filter coefficients
    let a1 = (-1.0 / (sample_rate * lp_iter as f64 * tau)).exp();
    let b0 = 1.0 - a1;

    // Compute deltas (linear interpolation between frames)
    let mut delta = vec![0.0f64; n];
    for i in 0..n - 1 {
        delta[i] = (loudness[i + 1] - loudness[i]) / lp_iter as f64;
    }
    // Last delta: (0 - last) / lp_iter (MoSQITo rolls and sets last to 0)
    delta[n - 1] = -loudness[n - 1] / lp_iter as f64;

    // Virtual upsample + filter
    let total = n * lp_iter;
    let mut ui = vec![0.0f64; total];
    for f in 0..n {
        ui[f * lp_iter] = loudness[f];
        for s in 1..lp_iter {
            ui[f * lp_iter + s] = ui[f * lp_iter + s - 1] + delta[f];
        }
    }

    // Apply 1st order IIR: y[n] = b0*x[n] + a1*y[n-1]
    // Using scipy.signal.lfilter equivalent
    let mut y_prev = 0.0f64;
    for sample in ui.iter_mut() {
        let y = b0 * *sample + a1 * y_prev;
        *sample = y;
        y_prev = y;
    }

    // Extract first sub-sample of each frame
    let mut result = vec![0.0f64; n];
    for f in 0..n {
        result[f] = ui[f * lp_iter];
    }

    result
}
