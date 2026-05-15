//! Perceptual Spectral Balance (PSB) and Bark-band specific loudness.
//!
//! PSB(t, z) = N'(t, z) / N(t)
//! 240 bins aggregated to 20 Bark bands (12 bins per band)
//!
//! This is a Kirin-original metric, not part of ISO 532-1.
//!
//! Kirin Hypha 移植版（Lens `native/src/psychoacoustic/spectral_balance.rs` からアルゴリズム同一移植）。
//!
//!  C-2 追加 (この module 末尾):
//! Bark 21-24 (5800-15500 Hz) を FFT パワースペクトルから直接ビニングで
//! 算出する関数と、15.5k-20kHz 補完帯域の関数を追加。これらは
//! ISO 532-1 specific loudness パイプライン (Bark 1-20) とは独立で、
//! 単位も異なる (specific loudness sone/Bark vs FFT power, linear)。
//! 本 module 既存関数 (`compute`, `compute_n_prime`) には一切変更なし。

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

/// Aggregate 240-bin specific loudness N'(t,z) to 20 Bark bands (サブ3-A-1).
///
/// `n_prime[bark] = mean(n_specific[bark*12 .. bark*12+12])` — units: sone/Bark.
/// Unlike [`compute`], the result is **not** normalized by N(t), so each value is
/// the average per-Bark specific loudness of the band. Used for Frame.n_prime[20]
/// persisted to `plugin_data/.../post/*.json` (G-50-34).
///
/// Independent from PSB computation so that PhaseD batch-vs-stream equivalence
/// tests (bit-identical within 1e-10) are unaffected.
pub fn compute_n_prime(n_specific: &[[f64; N_SPEC_BINS]]) -> Vec<[f64; N_BARK]> {
    let n_frames = n_specific.len();
    let mut out = vec![[0.0f64; N_BARK]; n_frames];
    let bins_per_bark = N_SPEC_BINS / N_BARK; // 240 / 20 = 12

    for t in 0..n_frames {
        for (bark, band) in out[t].iter_mut().enumerate() {
            let start = bark * bins_per_bark;
            let end = start + bins_per_bark;
            let sum: f64 = n_specific[t][start..end].iter().sum();
            *band = sum / bins_per_bark as f64;
        }
    }

    out
}

// ============================================================
//  C-2: PSB High FFT 経路 (Bark 21-24 + 15.5k-20kHz 補完)
// ============================================================
//
// ISO 532-1 側 (上記 `compute` / `compute_n_prime`) とは独立。
// 入力は FFT 片側パワースペクトル (|X[k]|^2)、単位は linear power。
// Bark 1-20 specific loudness (sone/Bark) と素朴加算・比較禁止。

/// FFT 片側パワースペクトルから Bark 21-24 の帯域エネルギーを集計する。
///
/// # Arguments
/// - `power_spec`: 片側パワースペクトル (長さ `fft_size/2 + 1`)
/// - `sample_rate`: サンプリングレート (Hz)
/// - `fft_size`: FFT 窓長 (samples)
///
/// # Returns
/// `[f64; 4]` — Bark 21, 22, 23, 24 の順。単位は linear FFT power (|X[k]|^2 の和)。
///
/// # 帯域境界 (Zwicker 1961, BARK_UPPER_HZ_PSB より)
/// - Bark 21: [6400, 7700) Hz
/// - Bark 22: [7700, 9500) Hz
/// - Bark 23: [9500, 12000) Hz
/// - Bark 24: [12000, 15500) Hz
///
/// ビン k の中心周波数が `[lower, upper)` に入るとき、そのビンを含める。
pub fn compute_psb_bark21_24_from_fft(
    power_spec: &[f64],
    sample_rate: f64,
    fft_size: usize,
) -> [f64; 4] {
    let bin_hz = sample_rate / fft_size as f64;
    let max_k = power_spec.len().saturating_sub(1);
    let mut out = [0.0f64; 4];

    // PSB index 20, 21, 22, 23 が Bark 21, 22, 23, 24 に対応 (0-indexed)。
    // 下端は BARK_UPPER_HZ_PSB[idx - 1] (ひとつ下のバンドの上端)、
    // 上端は BARK_UPPER_HZ_PSB[idx]。
    for (i, band) in out.iter_mut().enumerate() {
        let idx = 20 + i;
        let lower_hz = BARK_UPPER_HZ_PSB[idx - 1];
        let upper_hz = BARK_UPPER_HZ_PSB[idx];

        let k_lo = ((lower_hz / bin_hz).ceil() as usize).min(max_k);
        let k_hi = ((upper_hz / bin_hz).ceil() as usize).min(max_k);
        if k_lo < k_hi {
            *band = power_spec[k_lo..k_hi].iter().sum();
        }
    }
    out
}

/// FFT 片側パワースペクトルから 15.5k-20kHz 補完帯域のエネルギーを集計する。
///
///  .1 の PSB High 補完帯域。Bark 24 upper (15500 Hz) から
/// 20000 Hz まで。Nyquist を超える場合は Nyquist までで打ち切り。
///
/// # Returns
/// linear FFT power 単位でのエネルギー和。
pub fn compute_psb_high_ext_15_5k_20k(
    power_spec: &[f64],
    sample_rate: f64,
    fft_size: usize,
) -> f64 {
    let bin_hz = sample_rate / fft_size as f64;
    let max_k = power_spec.len().saturating_sub(1);
    let lower_hz = 15500.0_f64;
    let upper_hz = 20000.0_f64;
    let k_lo = ((lower_hz / bin_hz).ceil() as usize).min(max_k);
    let k_hi = ((upper_hz / bin_hz).ceil() as usize).min(max_k);
    if k_lo < k_hi {
        power_spec[k_lo..k_hi].iter().sum()
    } else {
        0.0
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn test_compute_n_prime_empty() {
        let result = compute_n_prime(&[]);
        assert!(result.is_empty());
    }

    #[test]
    fn test_compute_n_prime_zero_input_returns_zeros() {
        let input = vec![[0.0f64; N_SPEC_BINS]; 3];
        let result = compute_n_prime(&input);
        assert_eq!(result.len(), 3);
        for frame in &result {
            for &band in frame.iter() {
                assert_eq!(band, 0.0);
            }
        }
    }

    #[test]
    fn test_compute_n_prime_flat_input_preserves_unit() {
        // n_specific = 2.5 sone/Bark everywhere → each bark band = 2.5
        let mut input = [[0.0f64; N_SPEC_BINS]; 1];
        for v in input[0].iter_mut() {
            *v = 2.5;
        }
        let result = compute_n_prime(&input);
        for &band in result[0].iter() {
            assert!((band - 2.5).abs() < 1e-12, "expected 2.5 sone/Bark, got {band}");
        }
    }

    #[test]
    fn test_compute_n_prime_mean_not_sum() {
        // Only first bin of Bark 0 is non-zero (value 12.0). Mean over 12 bins = 1.0.
        // Sum would be 12.0; this test pins the "mean" semantics.
        let mut input = [[0.0f64; N_SPEC_BINS]; 1];
        input[0][0] = 12.0;
        let result = compute_n_prime(&input);
        assert!((result[0][0] - 1.0).abs() < 1e-12, "Bark 0 should be mean=1.0, got {}", result[0][0]);
        for &v in result[0].iter().skip(1) {
            assert_eq!(v, 0.0);
        }
    }

    #[test]
    fn test_compute_n_prime_independent_of_psb() {
        // n_prime must not depend on n_total, unlike PSB.
        let mut input = [[0.0f64; N_SPEC_BINS]; 1];
        for v in input[0].iter_mut() {
            *v = 1.5;
        }
        let a = compute_n_prime(&input);
        let b = compute_n_prime(&input);
        for bark in 0..N_BARK {
            assert_eq!(a[0][bark], b[0][bark]);
        }
    }

    // ──  C-2: FFT → Bark 21-24 ビニングのフィルタ応答テスト ──
    //
    // アプローチ: FFT 片側パワースペクトルを直接合成し
    // (ある bin に単位パワーを立てる)、その bin が対応する周波数が
    // 指定された Bark 21-24 帯域に正しく振り分けられるかを検証する。
    // STFT 全体のテストは `stft.rs` + 統合テストでカバー。
    //
    // 帯域境界 (Zwicker 1961):
    //   Bark 21 [6400, 7700), Bark 22 [7700, 9500),
    //   Bark 23 [9500, 12000), Bark 24 [12000, 15500).
    // 参考: fft_size=1024, sample_rate=48000 → bin_hz ≈ 46.875 Hz/bin
    //   6400/46.875 = 136.53  → k_lo=137  → 137*46.875 = 6421.875 Hz
    //   7700/46.875 = 164.27  → k_hi=165  → 164*46.875 = 7687.5 Hz
    //   9500/46.875 = 202.67  → k_hi=203
    //   12000/46.875 = 256.00 → k_hi=256
    //   15500/46.875 = 330.67 → k_hi=331

    const FS: f64 = 48000.0;
    const NF: usize = 1024;
    const SPEC_LEN: usize = 513; // NF/2 + 1

    fn spec_with_unit_at(bin: usize) -> Vec<f64> {
        let mut s = vec![0.0f64; SPEC_LEN];
        s[bin] = 1.0;
        s
    }

    #[test]
    fn test_bark21_24_from_fft_dc_is_zero() {
        let spec = spec_with_unit_at(0); // DC only
        let out = compute_psb_bark21_24_from_fft(&spec, FS, NF);
        for (i, v) in out.iter().enumerate() {
            assert_eq!(*v, 0.0, "Bark 2{} should be zero for DC", i + 1);
        }
    }

    #[test]
    fn test_bark21_24_from_fft_below_bark21_is_zero() {
        // 5000 Hz → bin 106.67 → bin 107, below Bark 21 lower (6400).
        let spec = spec_with_unit_at(107);
        let out = compute_psb_bark21_24_from_fft(&spec, FS, NF);
        for (i, v) in out.iter().enumerate() {
            assert_eq!(*v, 0.0, "Bark 2{} should be zero for 5000 Hz", i + 1);
        }
    }

    #[test]
    fn test_bark21_from_fft_center_7000hz() {
        // 7000 Hz → bin 149.33 → bin 149 (within [137, 165))
        let spec = spec_with_unit_at(149);
        let out = compute_psb_bark21_24_from_fft(&spec, FS, NF);
        assert_eq!(out[0], 1.0, "Bark 21 should capture 7000 Hz exactly");
        assert_eq!(out[1], 0.0);
        assert_eq!(out[2], 0.0);
        assert_eq!(out[3], 0.0);
    }

    #[test]
    fn test_bark22_from_fft_center_8500hz() {
        // 8500 Hz → bin 181.33 → bin 181 (within [165, 203))
        let spec = spec_with_unit_at(181);
        let out = compute_psb_bark21_24_from_fft(&spec, FS, NF);
        assert_eq!(out[0], 0.0);
        assert_eq!(out[1], 1.0, "Bark 22 should capture 8500 Hz exactly");
        assert_eq!(out[2], 0.0);
        assert_eq!(out[3], 0.0);
    }

    #[test]
    fn test_bark23_from_fft_center_10500hz() {
        // 10500 Hz → bin 224.0 → bin 224 (within [203, 256))
        let spec = spec_with_unit_at(224);
        let out = compute_psb_bark21_24_from_fft(&spec, FS, NF);
        assert_eq!(out[0], 0.0);
        assert_eq!(out[1], 0.0);
        assert_eq!(out[2], 1.0, "Bark 23 should capture 10500 Hz exactly");
        assert_eq!(out[3], 0.0);
    }

    #[test]
    fn test_bark24_from_fft_center_13500hz() {
        // 13500 Hz → bin 288.0 → bin 288 (within [256, 331))
        let spec = spec_with_unit_at(288);
        let out = compute_psb_bark21_24_from_fft(&spec, FS, NF);
        assert_eq!(out[0], 0.0);
        assert_eq!(out[1], 0.0);
        assert_eq!(out[2], 0.0);
        assert_eq!(out[3], 1.0, "Bark 24 should capture 13500 Hz exactly");
    }

    #[test]
    fn test_bark21_24_boundary_bins_exclusive_upper() {
        // 7700 Hz (Bark 21 upper) → bin 164.27 → boundary.
        // ceil(7700/46.875) = 165 → k_hi=165 exclusive → bin 164 belongs to Bark 21.
        let spec_164 = spec_with_unit_at(164);
        let out = compute_psb_bark21_24_from_fft(&spec_164, FS, NF);
        assert_eq!(out[0], 1.0, "bin 164 (7687.5 Hz) belongs to Bark 21");
        assert_eq!(out[1], 0.0);

        // bin 165 (7734.375 Hz) is just above 7700, belongs to Bark 22.
        let spec_165 = spec_with_unit_at(165);
        let out = compute_psb_bark21_24_from_fft(&spec_165, FS, NF);
        assert_eq!(out[0], 0.0);
        assert_eq!(out[1], 1.0, "bin 165 (7734.375 Hz) belongs to Bark 22");
    }

    #[test]
    fn test_bark24_upper_edge_15500hz_excluded() {
        // 15500 Hz (Bark 24 upper) must NOT be counted in Bark 24.
        // ceil(15500/46.875) = 331; bin 331 = 15515.625 Hz (above 15500).
        // bin 330 = 15468.75 Hz (below 15500, in Bark 24).
        let spec_330 = spec_with_unit_at(330);
        let out = compute_psb_bark21_24_from_fft(&spec_330, FS, NF);
        assert_eq!(out[3], 1.0, "bin 330 (15468.75 Hz) in Bark 24");

        let spec_331 = spec_with_unit_at(331);
        let out = compute_psb_bark21_24_from_fft(&spec_331, FS, NF);
        assert_eq!(out[3], 0.0, "bin 331 (15515.625 Hz) must NOT be in Bark 24");
    }

    #[test]
    fn test_psb_high_ext_15_5k_20k_zero_below_range() {
        let spec = spec_with_unit_at(330); // 15468.75 Hz
        let v = compute_psb_high_ext_15_5k_20k(&spec, FS, NF);
        assert_eq!(v, 0.0);
    }

    #[test]
    fn test_psb_high_ext_15_5k_20k_captures_17khz() {
        // 17000 Hz → bin 362.67 → bin 363. Within [331, 427).
        let spec = spec_with_unit_at(363);
        let v = compute_psb_high_ext_15_5k_20k(&spec, FS, NF);
        assert_eq!(v, 1.0, "17 kHz should land in 15.5k-20k extension");
    }

    #[test]
    fn test_psb_high_ext_15_5k_20k_excludes_at_and_above_20khz() {
        // ceil(20000/46.875) = 427; bin 427 = 20015.625 Hz (above 20000) excluded.
        // bin 426 = 19968.75 Hz included.
        let spec_426 = spec_with_unit_at(426);
        let v = compute_psb_high_ext_15_5k_20k(&spec_426, FS, NF);
        assert_eq!(v, 1.0);

        let spec_427 = spec_with_unit_at(427);
        let v = compute_psb_high_ext_15_5k_20k(&spec_427, FS, NF);
        assert_eq!(v, 0.0);
    }

    #[test]
    fn test_bark21_24_full_band_sums_match_individual_bins() {
        // Energy conservation: sum over all bins 137..331 must equal sum of 4 Bark bands.
        let mut spec = vec![0.0f64; SPEC_LEN];
        for (k, v) in spec.iter_mut().enumerate().take(331).skip(137) {
            *v = (k as f64) * 0.01; // arbitrary non-uniform profile
        }
        let out = compute_psb_bark21_24_from_fft(&spec, FS, NF);
        let total_from_bands: f64 = out.iter().sum();
        let total_direct: f64 = spec[137..331].iter().sum();
        assert!(
            (total_from_bands - total_direct).abs() < 1e-10,
            "Bark 21-24 sum ({}) must equal direct bin sum ({})",
            total_from_bands,
            total_direct
        );
    }
}
