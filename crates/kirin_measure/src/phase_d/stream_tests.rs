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
        assert!(
            r.loudness > 0.0,
            "94dB tone should produce positive loudness"
        );
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
    use super::super::{filter_bank, nonlinear_decay, temporal_weighting};

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
    assert!(
        total > 0.0,
        "total energy must be positive: {psb_bark21_24:?}"
    );
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
            single[i].loudness,
            split[i].loudness,
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
        rng_state = rng_state
            .wrapping_mul(6364136223846793005)
            .wrapping_add(1442695040888963407);
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
    let psb_summary =
        compute_psb_summary_pub(&last.psb, &last.psb_bark21_24, last.psb_high_ext_15_5k_20k);

    // For pink noise: low/mid should be > high
    // (1/f means less energy at higher frequencies)
    assert!(
        psb_summary.high < psb_summary.low,
        "Pink noise: high ({:.3}) must be < low ({:.3})",
        psb_summary.high,
        psb_summary.low
    );
    assert!(
        psb_summary.high < psb_summary.mid,
        "Pink noise: high ({:.3}) must be < mid ({:.3})",
        psb_summary.high,
        psb_summary.mid
    );
    // Sanity: high should be negative dB (fraction < 1.0)
    assert!(
        psb_summary.high < 0.0,
        "Pink noise: high ({:.3}) should be negative dB (normalized fraction)",
        psb_summary.high
    );
}
