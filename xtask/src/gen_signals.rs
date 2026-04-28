// G-108-01: テスト信号 S-1〜S-5 生成 (seed 固定)。
// crates/kirin_measure/tests/gen_test_signals.rs から実行経路と seed のみ変更し移管。
// アルゴリズム (Voss-McCartney / WAVE_FORMAT_IEEE_FLOAT / 2ch / 48000Hz) は不変。

use anyhow::{Context, Result};
use ebur128::{EbuR128, Mode};
use hound::{SampleFormat, WavSpec, WavWriter};
use rand::rngs::StdRng;
use rand::{Rng, SeedableRng};
use std::f64::consts::PI;
use std::path::{Path, PathBuf};

const SAMPLE_RATE: u32 = 48000;

pub fn run(args: Vec<String>) -> Result<()> {
    let mut seed: u64 = 42;
    let mut out_dir: Option<PathBuf> = None;
    let mut iter = args.into_iter();
    while let Some(arg) = iter.next() {
        match arg.as_str() {
            "--seed" => {
                let v = iter.next().context("--seed requires a value")?;
                seed = v.parse().context("--seed value must be u64")?;
            }
            "--out-dir" => {
                let v = iter.next().context("--out-dir requires a value")?;
                out_dir = Some(PathBuf::from(v));
            }
            "-h" | "--help" => {
                println!("Usage: cargo xtask gen-signals [--seed <u64>] [--out-dir <PATH>]");
                println!("  --seed     Random seed for pink-noise generation (default: 42)");
                println!("  --out-dir  Output directory (default: <workspace_root>/test_signals)");
                return Ok(());
            }
            other => anyhow::bail!("unknown argument: {other}"),
        }
    }

    let dir = out_dir.unwrap_or_else(|| {
        PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("..")
            .join("test_signals")
    });
    std::fs::create_dir_all(&dir)
        .with_context(|| format!("create dir {}", dir.display()))?;

    let mut rng = StdRng::seed_from_u64(seed);

    // S-1: 1kHz sine -6 dBFS, 10s
    {
        let samples = gen_sine_stereo(1000.0, -6.0, SAMPLE_RATE as usize * 10);
        write_wav_f32_stereo(
            &dir.join("S-1_1kHz_sine_m6dBFS_10s.wav"),
            &samples,
            SAMPLE_RATE,
        )?;
    }

    // S-2: pink -23 LUFS, 60s
    {
        let raw = gen_pink_noise_stereo(SAMPLE_RATE as usize * 60, &mut rng);
        let samples = normalize_to_lufs(&raw, -23.0, SAMPLE_RATE);
        write_wav_f32_stereo(
            &dir.join("S-2_pinknoise_m23LUFS_60s.wav"),
            &samples,
            SAMPLE_RATE,
        )?;
    }

    // S-3: pink -33 LUFS, 60s
    {
        let raw = gen_pink_noise_stereo(SAMPLE_RATE as usize * 60, &mut rng);
        let samples = normalize_to_lufs(&raw, -33.0, SAMPLE_RATE);
        write_wav_f32_stereo(
            &dir.join("S-3_pinknoise_m33LUFS_60s.wav"),
            &samples,
            SAMPLE_RATE,
        )?;
    }

    // S-4: 1kHz sine -6 dBFS, 1s
    {
        let samples = gen_sine_stereo(1000.0, -6.0, 48000);
        write_wav_f32_stereo(
            &dir.join("S-4_1kHz_sine_m6dBFS_1s.wav"),
            &samples,
            SAMPLE_RATE,
        )?;
    }

    // S-5: pink -14 LUFS, 60s
    {
        let raw = gen_pink_noise_stereo(SAMPLE_RATE as usize * 60, &mut rng);
        let samples = normalize_to_lufs(&raw, -14.0, SAMPLE_RATE);
        write_wav_f32_stereo(
            &dir.join("S-5_pinknoise_m14LUFS_60s.wav"),
            &samples,
            SAMPLE_RATE,
        )?;
    }

    println!("Generated S-1..S-5 in {} (seed={seed})", dir.display());
    Ok(())
}

fn write_wav_f32_stereo(path: &Path, samples_lr: &[(f32, f32)], sample_rate: u32) -> Result<()> {
    let spec = WavSpec {
        channels: 2,
        sample_rate,
        bits_per_sample: 32,
        sample_format: SampleFormat::Float,
    };
    let mut writer = WavWriter::create(path, spec)
        .with_context(|| format!("create wav {}", path.display()))?;
    for &(l, r) in samples_lr {
        writer.write_sample(l)?;
        writer.write_sample(r)?;
    }
    writer.finalize()?;
    Ok(())
}

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

/// ピンクノイズ生成（Voss-McCartney アルゴリズム / 16段）
fn gen_pink_noise_stereo(n_frames: usize, rng: &mut StdRng) -> Vec<(f32, f32)> {
    let n_rows = 16;
    let mut rows_l = vec![0.0_f64; n_rows];
    let mut rows_r = vec![0.0_f64; n_rows];
    let mut running_sum_l = 0.0_f64;
    let mut running_sum_r = 0.0_f64;

    for i in 0..n_rows {
        rows_l[i] = rng.gen_range(-1.0..1.0);
        rows_r[i] = rng.gen_range(-1.0..1.0);
        running_sum_l += rows_l[i];
        running_sum_r += rows_r[i];
    }

    let norm = 1.0 / (n_rows as f64 + 1.0);
    let mut out = Vec::with_capacity(n_frames);

    for i in 0..n_frames {
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

fn measure_lufs_integrated(samples: &[(f32, f32)], sample_rate: u32) -> f64 {
    let mut ebu = EbuR128::new(2, sample_rate, Mode::I).unwrap();
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
    ebu.loudness_global().unwrap()
}

fn normalize_to_lufs(
    samples: &[(f32, f32)],
    target_lufs: f64,
    sample_rate: u32,
) -> Vec<(f32, f32)> {
    let current_lufs = measure_lufs_integrated(samples, sample_rate);
    let gain_db = target_lufs - current_lufs;
    let gain_lin = 10.0_f64.powf(gain_db / 20.0) as f32;
    samples
        .iter()
        .map(|&(l, r)| (l * gain_lin, r * gain_lin))
        .collect()
}
