//! DIN 45692:2009 Sharpness (Widmann method)
//!
//! S(t) = 0.11 * sum[N'(z) * g(z) * z * 0.1] / N(t)
//!
//! Kirin Hypha 移植版（Lens `native/src/psychoacoustic/sharpness.rs` からアルゴリズム同一移植）。
//! Reference:  sharpness_din/sharpness_din_from_loudness.py

use super::tables::*;

/// Compute sharpness from specific loudness and total loudness.
///
/// # Arguments
/// * `n_specific` - N'(t,z) [frame][240 bins]
/// * `n_total` - Filtered N(t) [frame]
///
/// # Returns
/// Sharpness S(t) in acum [frame]
pub fn compute(n_specific: &[[f64; N_SPEC_BINS]], n_total: &[f64]) -> Vec<f64> {
    let n_frames = n_specific.len();
    let mut sharpness = vec![0.0f64; n_frames];

    for t in 0..n_frames {
        let n = n_total[t];
        if n < 0.1 {
            sharpness[t] = 0.0;
            continue;
        }

        let mut numerator = 0.0f64;
        for (bin, &ns) in n_specific[t].iter().enumerate() {
            let z = (bin as f64 + 1.0) * 0.1; // 0.1 to 24.0 Bark
            let g = sharpness_weight_din(z);
            numerator += ns * g * z * 0.1;
        }

        sharpness[t] = SHARPNESS_C * numerator / n;
    }

    sharpness
}
