//! テスト信号 S-1〜S-5 生成＋理論値計測（guardian_53 §8）。
//!
//! 実行方法:
//!   cargo test -p kirin_measure gen_test_signals -- --nocapture
//!
//! 生成先: test_signals/

use ebur128::{EbuR128, Mode};
use hound::{SampleFormat, WavSpec, WavWriter};
use rand::Rng;
use std::f64::consts::PI;
use std::path::Path;

const SAMPLE_RATE: u32 = 48000;
const OUT_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../test_signals");

// ── WAV 書き出し ────────────────────────────────────────────────────────────

fn write_wav_f32_stereo(path: &str, samples_lr: &[(f32, f32)], sample_rate: u32) {
    let spec = WavSpec {
        channels: 2,
        sample_rate,
        bits_per_sample: 32,
        sample_format: SampleFormat::Float,
    };
    let mut writer = WavWriter::create(path, spec).expect("create wav");
    for &(l, r) in samples_lr {
        writer.write_sample(l).unwrap();
        writer.write_sample(r).unwrap();
    }
    writer.finalize().unwrap();
}

// ── 信号生成 ────────────────────────────────────────────────────────────────

/// 1kHz sine stereo (L=R) at given dBFS amplitude
fn gen_sine_stereo(freq: f64, db_fs: f64, n_frames: usize) -> Vec<(f32, f32)> {
    let amp = 10.0_f64.powf(db_fs / 20.0);
    let sr = SAMPLE_RATE as f64;
    (0..n_frames)
        .map(|i| {
            let s = (amp * (2.0 * PI * freq * i as f64 / sr).sin()) as f32;
            (s, s)
        })
        .collect()
}

/// ピンクノイズ生成（Voss-McCartney アルゴリズム）
///
/// 16 段の白色ノイズ源を異なる更新レートで合算し 1/f 特性に近似する。
fn gen_pink_noise_stereo(n_frames: usize) -> Vec<(f32, f32)> {
    let mut rng = rand::thread_rng();
    let n_rows = 16;
    let mut rows_l = vec![0.0_f64; n_rows];
    let mut rows_r = vec![0.0_f64; n_rows];
    let mut running_sum_l = 0.0_f64;
    let mut running_sum_r = 0.0_f64;

    // 初期化
    for i in 0..n_rows {
        rows_l[i] = rng.gen_range(-1.0..1.0);
        rows_r[i] = rng.gen_range(-1.0..1.0);
        running_sum_l += rows_l[i];
        running_sum_r += rows_r[i];
    }

    let norm = 1.0 / (n_rows as f64 + 1.0);
    let mut out = Vec::with_capacity(n_frames);

    for i in 0..n_frames {
        // CTZ (count trailing zeros) で更新行を決定
        let mut idx = 0;
        let mut k = i + 1;
        while k & 1 == 0 && idx < n_rows - 1 {
            k >>= 1;
            idx += 1;
        }

        running_sum_l -= rows_l[idx];
        rows_l[idx] = rng.gen_range(-1.0..1.0);
        running_sum_l += rows_l[idx];

        running_sum_r -= rows_r[idx];
        rows_r[idx] = rng.gen_range(-1.0..1.0);
        running_sum_r += rows_r[idx];

        let white_l: f64 = rng.gen_range(-1.0..1.0);
        let white_r: f64 = rng.gen_range(-1.0..1.0);

        let l = ((running_sum_l + white_l) * norm) as f32;
        let r = ((running_sum_r + white_r) * norm) as f32;
        out.push((l, r));
    }
    out
}

// ── LUFS 計測 + 正規化 ─────────────────────────────────────────────────────

/// ステレオ信号の LUFS-I（Integrated Loudness）を計測する。
fn measure_lufs_integrated(samples: &[(f32, f32)], sample_rate: u32) -> f64 {
    let mode = Mode::I;
    let mut ebu = EbuR128::new(2, sample_rate, mode).unwrap();
    let chunk_size = sample_rate as usize / 10; // 100ms
    let mut interleaved = Vec::with_capacity(chunk_size * 2);

    for chunk in samples.chunks(chunk_size) {
        interleaved.clear();
        for &(l, r) in chunk {
            interleaved.push(l as f64);
            interleaved.push(r as f64);
        }
        ebu.add_frames_f64(&interleaved).unwrap();
    }
    ebu.loudness_global().unwrap()
}

/// ステレオ信号の True Peak (dBTP) を計測する。
fn measure_true_peak(samples: &[(f32, f32)], sample_rate: u32) -> f64 {
    let mode = Mode::TRUE_PEAK;
    let mut ebu = EbuR128::new(2, sample_rate, mode).unwrap();
    let chunk_size = sample_rate as usize / 10;
    let mut interleaved = Vec::with_capacity(chunk_size * 2);

    for chunk in samples.chunks(chunk_size) {
        interleaved.clear();
        for &(l, r) in chunk {
            interleaved.push(l as f64);
            interleaved.push(r as f64);
        }
        ebu.add_frames_f64(&interleaved).unwrap();
    }

    let tp0 = ebu.true_peak(0).unwrap();
    let tp1 = ebu.true_peak(1).unwrap();
    let tp_lin = tp0.max(tp1);
    if tp_lin > 0.0 {
        20.0 * tp_lin.log10()
    } else {
        f64::NEG_INFINITY
    }
}

/// LUFS-I を target_lufs に正規化する。
fn normalize_to_lufs(
    samples: &[(f32, f32)],
    target_lufs: f64,
    sample_rate: u32,
) -> Vec<(f32, f32)> {
    let current_lufs = measure_lufs_integrated(samples, sample_rate);
    let gain_db = target_lufs - current_lufs;
    let gain_lin = 10.0_f64.powf(gain_db / 20.0) as f32;
    samples.iter().map(|&(l, r)| (l * gain_lin, r * gain_lin)).collect()
}

// ── Sample Peak (dBFS) 計測 ─────────────────────────────────────────────────

fn measure_sample_peak_dbfs(samples: &[(f32, f32)]) -> f64 {
    let peak = samples
        .iter()
        .map(|&(l, r)| l.abs().max(r.abs()))
        .fold(0.0_f32, f32::max);
    if peak > 0.0 {
        20.0 * (peak as f64).log10()
    } else {
        f64::NEG_INFINITY
    }
}

// ── Crest Factor 計測 ───────────────────────────────────────────────────────

fn measure_crest(samples: &[(f32, f32)]) -> f64 {
    let peak = samples
        .iter()
        .map(|&(l, r)| l.abs().max(r.abs()) as f64)
        .fold(0.0_f64, f64::max);
    let sum_sq: f64 = samples
        .iter()
        .map(|&(l, r)| {
            let lf = l as f64;
            let rf = r as f64;
            (lf * lf + rf * rf) / 2.0
        })
        .sum();
    let rms = (sum_sq / samples.len() as f64).sqrt();
    if peak > 0.0 && rms > 0.0 {
        20.0 * peak.log10() - 20.0 * rms.log10()
    } else {
        f64::NAN
    }
}

// ══════════════════════════════════════════════════════════════════════════════

#[test]
fn gen_test_signals() {
    let dir = Path::new(OUT_DIR);
    std::fs::create_dir_all(dir).expect("create test_signals dir");

    println!();
    println!("╔══════════════════════════════════════════════════════════════════════════════════════╗");
    println!("║                   テスト信号 S-1〜S-5 生成 + 理論値一覧                              ║");
    println!("╠════╦══════════════════════════════════╦════════════╦════════════╦═══════╦════════════╣");
    println!("║ #  ║ ファイル                         ║ LUFS-I     ║ TP (dBTP)  ║ Crest ║ Peak dBFS  ║");
    println!("╠════╬══════════════════════════════════╬════════════╬════════════╬═══════╬════════════╣");

    // ── S-1: 1kHz sine -6dBFS, 48kHz, stereo, 10秒 ─────────────────────
    {
        let name = "S-1_1kHz_sine_m6dBFS_10s.wav";
        let samples = gen_sine_stereo(1000.0, -6.0, SAMPLE_RATE as usize * 10);
        let path = dir.join(name);
        write_wav_f32_stereo(path.to_str().unwrap(), &samples, SAMPLE_RATE);

        let lufs = measure_lufs_integrated(&samples, SAMPLE_RATE);
        let tp = measure_true_peak(&samples, SAMPLE_RATE);
        let crest = measure_crest(&samples);
        let peak = measure_sample_peak_dbfs(&samples);
        println!(
            "║ S-1║ {:<32} ║ {:>+8.2}   ║ {:>+8.3}  ║ {:>5.2}║ {:>+8.2}   ║",
            name, lufs, tp, crest, peak
        );
    }

    // ── S-2: ピンクノイズ -23 LUFS, 48kHz, stereo, 60秒 ─────────────────
    {
        let name = "S-2_pinknoise_m23LUFS_60s.wav";
        let raw = gen_pink_noise_stereo(SAMPLE_RATE as usize * 60);
        let samples = normalize_to_lufs(&raw, -23.0, SAMPLE_RATE);
        let path = dir.join(name);
        write_wav_f32_stereo(path.to_str().unwrap(), &samples, SAMPLE_RATE);

        let lufs = measure_lufs_integrated(&samples, SAMPLE_RATE);
        let tp = measure_true_peak(&samples, SAMPLE_RATE);
        let crest = measure_crest(&samples);
        let peak = measure_sample_peak_dbfs(&samples);
        println!(
            "║ S-2║ {:<32} ║ {:>+8.2}   ║ {:>+8.3}  ║ {:>5.2}║ {:>+8.2}   ║",
            name, lufs, tp, crest, peak
        );
    }

    // ── S-3: ピンクノイズ -33 LUFS, 48kHz, stereo, 60秒 ─────────────────
    {
        let name = "S-3_pinknoise_m33LUFS_60s.wav";
        let raw = gen_pink_noise_stereo(SAMPLE_RATE as usize * 60);
        let samples = normalize_to_lufs(&raw, -33.0, SAMPLE_RATE);
        let path = dir.join(name);
        write_wav_f32_stereo(path.to_str().unwrap(), &samples, SAMPLE_RATE);

        let lufs = measure_lufs_integrated(&samples, SAMPLE_RATE);
        let tp = measure_true_peak(&samples, SAMPLE_RATE);
        let crest = measure_crest(&samples);
        let peak = measure_sample_peak_dbfs(&samples);
        println!(
            "║ S-3║ {:<32} ║ {:>+8.2}   ║ {:>+8.3}  ║ {:>5.2}║ {:>+8.2}   ║",
            name, lufs, tp, crest, peak
        );
    }

    // ── S-4: 1kHz sine -6dBFS, 48kHz, stereo, 1秒 (48000 samples) ───────
    {
        let name = "S-4_1kHz_sine_m6dBFS_1s.wav";
        let samples = gen_sine_stereo(1000.0, -6.0, 48000);
        let path = dir.join(name);
        write_wav_f32_stereo(path.to_str().unwrap(), &samples, SAMPLE_RATE);

        let lufs = measure_lufs_integrated(&samples, SAMPLE_RATE);
        let tp = measure_true_peak(&samples, SAMPLE_RATE);
        let crest = measure_crest(&samples);
        let peak = measure_sample_peak_dbfs(&samples);
        println!(
            "║ S-4║ {:<32} ║ {:>+8.2}   ║ {:>+8.3}  ║ {:>5.2}║ {:>+8.2}   ║",
            name, lufs, tp, crest, peak
        );
    }

    // ── S-5: ピンクノイズ -14 LUFS, 48kHz, stereo, 60秒 ─────────────────
    {
        let name = "S-5_pinknoise_m14LUFS_60s.wav";
        let raw = gen_pink_noise_stereo(SAMPLE_RATE as usize * 60);
        let samples = normalize_to_lufs(&raw, -14.0, SAMPLE_RATE);
        let path = dir.join(name);
        write_wav_f32_stereo(path.to_str().unwrap(), &samples, SAMPLE_RATE);

        let lufs = measure_lufs_integrated(&samples, SAMPLE_RATE);
        let tp = measure_true_peak(&samples, SAMPLE_RATE);
        let crest = measure_crest(&samples);
        let peak = measure_sample_peak_dbfs(&samples);
        println!(
            "║ S-5║ {:<32} ║ {:>+8.2}   ║ {:>+8.3}  ║ {:>5.2}║ {:>+8.2}   ║",
            name, lufs, tp, crest, peak
        );
    }

    println!("╚════╩══════════════════════════════════╩════════════╩════════════╩═══════╩════════════╝");
    println!();
    println!("  注意:");
    println!("  - TP は ebur128 4× oversampling (ITU-R BS.1770-4 Annex 2)");
    println!("  - ピンクノイズは Voss-McCartney 生成。rand seed 固定なし（再生成で値が変わる）");
    println!("  - Crest = peak_dBFS - RMS_dBFS（per-channel average RMS）");
    println!("  - S-1/S-4: 1kHz @ 48kHz = 48 samples/cycle → ピーク=サンプル点一致");
    println!();
}
