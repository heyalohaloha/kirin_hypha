//! Deterministic log-frequency bank and definition hashing for fixed-scale SuperFlux.

use std::f32::consts::PI;

use sha2::{Digest, Sha256};

use super::{
    SuperFluxBand, SuperFluxConfig, SuperFluxRuntimeVerification, SUPERFLUX_ALGORITHM_VERSION,
    SUPERFLUX_DEFINITION_VERSION, SUPERFLUX_MAX_HZ, SUPERFLUX_MAX_MILLIHZ, SUPERFLUX_MIN_HZ,
    SUPERFLUX_MIN_MILLIHZ, SUPERFLUX_REFERENCE_HOP,
};

pub(super) fn build_filterbank(
    sample_rate: u32,
    fft_size: usize,
    bands_per_octave: u32,
) -> Result<Vec<SuperFluxBand>, &'static str> {
    let center_hz = |q: i32| 440.0 * 2.0_f64.powf(q as f64 / bands_per_octave as f64);
    let mut first_q = 0;
    while center_hz(first_q) >= SUPERFLUX_MIN_HZ {
        first_q -= 1;
    }
    let mut last_q = 0;
    while center_hz(last_q) <= SUPERFLUX_MAX_HZ {
        last_q += 1;
    }
    let nyquist_bin = fft_size / 2;
    let mut unique = Vec::<(f64, usize)>::new();
    for q in first_q..=last_q {
        let hz = center_hz(q);
        let raw_bin = hz * fft_size as f64 / sample_rate as f64;
        let bin = (raw_bin.round() as usize).clamp(1, nyquist_bin - 1);
        if unique.last().is_none_or(|previous| previous.1 != bin) {
            unique.push((hz, bin));
        }
    }
    let mut bands = Vec::new();
    for points in unique.windows(3) {
        if !(SUPERFLUX_MIN_HZ..=SUPERFLUX_MAX_HZ).contains(&points[1].0) {
            continue;
        }
        let bins = [points[0].1, points[1].1, points[2].1];
        if !(bins[0] < bins[1] && bins[1] < bins[2] && bins[2] < nyquist_bin) {
            return Err("invalid SuperFlux filterbank triplet");
        }
        let mut weights = Vec::with_capacity(bins[2] - bins[0] + 1);
        for bin in bins[0]..=bins[2] {
            let weight = if bin <= bins[1] {
                (bin - bins[0]) as f32 / (bins[1] - bins[0]) as f32
            } else {
                (bins[2] - bin) as f32 / (bins[2] - bins[1]) as f32
            };
            weights.push(weight);
        }
        bands.push(SuperFluxBand { bins, weights });
    }
    if bands.is_empty() {
        return Err("empty SuperFlux filterbank");
    }
    Ok(bands)
}

pub(super) fn definition_hash(
    sample_rate: u32,
    window_samples: usize,
    hop_samples: usize,
    fft_size: usize,
    spectral_lag_frames: usize,
    config: SuperFluxConfig,
    bands: &[SuperFluxBand],
) -> [u8; 32] {
    // This is a semantic identity, not a fingerprint of one platform's libm output. Runtime Hann
    // and triangle f32 coefficients are verified separately below; only their algorithm contract
    // and the realized integer bank topology belong in the cross-platform definition.
    let mut hasher = Sha256::new();
    for field in [
        SUPERFLUX_ALGORITHM_VERSION,
        SUPERFLUX_DEFINITION_VERSION,
        "periodic-hann",
        "one-sided-power-per-channel-over-window-energy",
        "lr-power-mean-before-square-root",
        "mid-side-waveform-half-sum-difference",
        "coherent-sine-full-scale-gain",
        "a4-440-equal-log-centers",
        "nearest-bin-ties-away-low-first-dedup",
        "dc-nyquist-excluded-clamp-to-interior",
        "hz-triangles-0-1-0-no-area-normalization",
        "log10-one-plus-amplitude-over-fixed-reference",
        "past-frame-frequency-neighbor-maximum-half-wave",
        "arithmetic-band-mean",
    ] {
        hasher.update(field.as_bytes());
        hasher.update([0]);
    }
    hasher.update(config.channel_mode.as_str().as_bytes());
    hasher.update((config.channel_count as u64).to_le_bytes());
    hasher.update(sample_rate.to_le_bytes());
    hasher.update(config.reference_window_samples.to_le_bytes());
    hasher.update(SUPERFLUX_REFERENCE_HOP.to_le_bytes());
    for value in [window_samples, hop_samples, fft_size, spectral_lag_frames] {
        hasher.update((value as u64).to_le_bytes());
    }
    hasher.update(config.bands_per_octave.to_le_bytes());
    hasher.update((config.maximum_filter_radius as u64).to_le_bytes());
    hasher.update(config.reference_dbfs.to_le_bytes());
    hasher.update(SUPERFLUX_MIN_MILLIHZ.to_le_bytes());
    hasher.update(SUPERFLUX_MAX_MILLIHZ.to_le_bytes());
    hasher.update((bands.len() as u64).to_le_bytes());
    for band in bands {
        for bin in band.bins {
            hasher.update((bin as u64).to_le_bytes());
        }
        hasher.update((band.weights.len() as u64).to_le_bytes());
    }
    hasher.finalize().into()
}

pub(super) fn verify_runtime_coefficients(
    window: &[f32],
    window_samples: usize,
    fft_size: usize,
    stored_window_energy: f32,
    stored_full_scale_gain: f32,
    bands: &[SuperFluxBand],
) -> Result<SuperFluxRuntimeVerification, &'static str> {
    if window.len() != window_samples
        || window.first() != Some(&0.0)
        || window
            .iter()
            .any(|value| !value.is_finite() || !(-1.0e-6..=1.000_001).contains(value))
    {
        return Err("invalid SuperFlux runtime Hann coefficients");
    }
    for (index, actual) in window.iter().enumerate() {
        let expected = 0.5 - 0.5 * (2.0 * PI * index as f32 / window_samples as f32).cos();
        if actual.to_bits() != expected.to_bits() {
            return Err("SuperFlux runtime Hann formula mismatch");
        }
    }
    let window_sum = window.iter().map(|&value| value as f64).sum::<f64>() as f32;
    let window_energy = window
        .iter()
        .map(|&value| f64::from(value) * f64::from(value))
        .sum::<f64>() as f32;
    let full_scale_gain = window_sum / (2.0 * window_energy).sqrt();
    if window_energy.to_bits() != stored_window_energy.to_bits()
        || full_scale_gain.to_bits() != stored_full_scale_gain.to_bits()
    {
        return Err("SuperFlux runtime Hann compensation mismatch");
    }

    let mut band_weight_count = 0_usize;
    for band in bands {
        let [start, center, end] = band.bins;
        let expected_len = end
            .checked_sub(start)
            .and_then(|span| span.checked_add(1))
            .ok_or("invalid SuperFlux runtime band topology")?;
        let center_offset = center
            .checked_sub(start)
            .ok_or("invalid SuperFlux runtime band center")?;
        if !(start < center && center < end && end < fft_size / 2)
            || band.weights.len() != expected_len
            || band.weights.first() != Some(&0.0)
            || band.weights.get(center_offset) != Some(&1.0)
            || band.weights.last() != Some(&0.0)
            || band
                .weights
                .iter()
                .any(|weight| !weight.is_finite() || !(0.0..=1.0).contains(weight))
        {
            return Err("invalid SuperFlux runtime triangle coefficients");
        }
        for (offset, actual) in band.weights.iter().enumerate() {
            let bin = start
                .checked_add(offset)
                .ok_or("SuperFlux runtime triangle index overflow")?;
            let expected = if bin <= center {
                (bin - start) as f32 / (center - start) as f32
            } else {
                (end - bin) as f32 / (end - center) as f32
            };
            if actual.to_bits() != expected.to_bits() {
                return Err("SuperFlux runtime triangle formula mismatch");
            }
        }
        band_weight_count = band_weight_count
            .checked_add(band.weights.len())
            .ok_or("SuperFlux runtime coefficient count overflow")?;
    }
    if bands.is_empty() {
        return Err("empty SuperFlux runtime filterbank");
    }
    Ok(SuperFluxRuntimeVerification {
        window_sum,
        window_energy,
        full_scale_gain,
        band_weight_count,
    })
}
