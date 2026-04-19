//! Perceptual Spectral Balance (PSB)
//!
//! PSB(t, z) = N'(t, z) / N(t)
//! 240 bins aggregated to 20 Bark bands (12 bins per band)
//!
//! This is a Kirin-original metric, not part of ISO 532-1.
//!
//! Kirin Hypha 移植版（Lens `native/src/psychoacoustic/spectral_balance.rs` からアルゴリズム同一移植）。

use super::tables::*;

/// Compute Perceptual Spectral Balance.
///
/// # Arguments
/// * `n_specific` - N'(t,z) [frame][240 bins]
/// * `n_total` - Filtered N(t) [frame]
///
/// # Returns
/// PSB [frame][20 Bark bands] (ratios summing to ~1.0 per frame)
pub fn compute(n_specific: &[[f64; N_SPEC_BINS]], n_total: &[f64]) -> Vec<[f64; N_BARK]> {
    let n_frames = n_specific.len();
    let mut psb = vec![[0.0f64; N_BARK]; n_frames];
    let bins_per_bark = N_SPEC_BINS / N_BARK; // 240 / 20 = 12

    for t in 0..n_frames {
        let n = n_total[t].max(0.1); // Avoid division by zero

        for (bark, psb_band) in psb[t].iter_mut().enumerate() {
            let start = bark * bins_per_bark;
            let end = start + bins_per_bark;
            let sum: f64 = n_specific[t][start..end].iter().sum();
            *psb_band = sum / (bins_per_bark as f64 * n);
        }
    }

    psb
}
