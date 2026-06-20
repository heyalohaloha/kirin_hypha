//! ISO 532-1 Core Loudness Calculation
//!
//! 28 third-octave band SPL → 20 critical bands (Bark) → core loudness (sone/Bark)
//! Plus 1 zero-padded band = 21 total.
//!
//! Kirin Hypha 移植版（Lens `native/src/psychoacoustic/core_loudness.rs` からアルゴリズム同一移植）。
//! Reference:  loudness_zwst/_main_loudness.py

use super::tables::*;

/// Compute core loudness from SPL matrix.
///
/// # Arguments
/// * `spl_frames` - SPL values per frame [frame][28 bands] (dB)
/// * `field_type` - Free or Diffuse sound field
///
/// # Returns
/// Core loudness matrix [frame][21 bands] (sone/Bark)
pub fn compute(spl_frames: &[[f64; N_BANDS]], field_type: FieldType) -> Vec<[f64; N_CORE]> {
    let n_frames = spl_frames.len();
    let mut result = vec![[0.0f64; N_CORE]; n_frames];

    for t in 0..n_frames {
        let spec = &spl_frames[t];
        result[t] = compute_frame(spec, field_type);
    }

    result
}

/// Compute core loudness for a single time frame.
fn compute_frame(spec: &[f64; N_BANDS], field_type: FieldType) -> [f64; N_CORE] {
    // Step 1: Low-frequency correction (bands 0-10, i.e. 25-250 Hz)
    // Apply equal loudness contour corrections
    let mut xp = [0.0f64; 11]; // corrected levels for bands 0-10
    for band in 0..11 {
        let level = spec[band];
        //  vectorized logic:
        // Find highest range index where spec > (rap - dll), use that dll value
        let mut correction = DLL[0][band]; // default
        for r in 0..7 {
            if level > RAP[r] - DLL[r][band] {
                correction = DLL[r + 1][band];
            }
        }
        if level > RAP[7] - DLL[7][band] {
            correction = 0.0;
        }

        xp[band] = correction + level;
    }

    // Convert corrected low-freq levels to intensity
    let mut ti = [0.0f64; 11];
    for i in 0..11 {
        ti[i] = 10.0f64.powf(xp[i] / 10.0);
    }

    // Step 2: Critical band integration for first 3 bands
    // LCB[0] = sum(ti[0..6]), LCB[1] = sum(ti[6..9]), LCB[2] = sum(ti[9..11])
    let mut lcb = [0.0f64; 3];
    let gi0: f64 = ti[0..6].iter().sum();
    let gi1: f64 = ti[6..9].iter().sum();
    let gi2: f64 = ti[9..11].iter().sum();
    if gi0 > 0.0 {
        lcb[0] = 10.0 * gi0.log10();
    }
    if gi1 > 0.0 {
        lcb[1] = 10.0 * gi1.log10();
    }
    if gi2 > 0.0 {
        lcb[2] = 10.0 * gi2.log10();
    }

    // Step 3: Build 20-band level array
    // le[0..3] = lcb[0..3], le[3..20] = spec[11..28]
    let mut le = [0.0f64; N_BARK];
    le[0] = lcb[0];
    le[1] = lcb[1];
    le[2] = lcb[2];
    le[3..N_BARK].copy_from_slice(&spec[11..N_BANDS]);

    // Step 4: Apply ear transmission correction (A0)
    for i in 0..N_BARK {
        le[i] -= A0[i];
    }

    // Step 5: Apply free/diffuse correction (DDF)
    if field_type == FieldType::Diffuse {
        for i in 0..N_BARK {
            le[i] += DDF[i];
        }
    }

    // Step 6: Compute core loudness
    let s = LOUDNESS_S; // 0.25
    let mut nm = [0.0f64; N_CORE]; // 21 bands (20 + 1 zero padding)

    for i in 0..N_BARK {
        if le[i] > LTQ[i] {
            le[i] -= DCB[i]; // critical band adaptation (only above threshold)

            let mp1 = 0.0635 * 10.0f64.powf(0.025 * LTQ[i]);
            let mp2 = (1.0 - s + s * 10.0f64.powf(0.1 * (le[i] - LTQ[i]))).powf(0.25) - 1.0;
            nm[i] = mp1 * mp2;
            if nm[i] < 0.0 {
                nm[i] = 0.0;
            }
        }
        // else: nm[i] remains 0.0
    }
    // nm[20] = 0.0 (zero padding, already initialized)

    // Step 7: Lowest critical band correction
    let korry = 0.4 + 0.32 * nm[0].powf(0.2);
    if korry <= 1.0 {
        nm[0] *= korry;
    }

    nm
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_silence_zero_loudness() {
        // Very low SPL should produce near-zero core loudness
        let frame = [-20.0f64; N_BANDS]; // -20 dB SPL (below threshold)
        let nm = compute_frame(&frame, FieldType::Free);
        for v in nm.iter().take(N_CORE) {
            assert!(*v >= 0.0, "Core loudness must be non-negative");
            assert!(*v < 0.01, "Low SPL should give near-zero loudness");
        }
    }

    #[test]
    fn test_high_level_positive_loudness() {
        // 94 dB in band 16 (1kHz) should give positive core loudness
        let mut frame = [-100.0f64; N_BANDS];
        frame[16] = 94.0; // 1kHz at 94 dB
        let nm = compute_frame(&frame, FieldType::Free);
        // Band 16 maps to critical band ~8-9 (after offset: spec[16] = le[8])
        // There should be some positive loudness
        let total: f64 = nm.iter().sum();
        assert!(total > 0.0, "94 dB signal should produce positive loudness");
    }
}
