//! guardian_61 C-3 検証用 inspection_report.json サンプル生成。
//!
//! S-1 (1 kHz sine -6 dBFS) と S-2 (pink noise -23 LUFS) を PhaseDStream に通し、
//! PsbSummary を含む JSON を `/tmp/kirin_hypha_inspection/` に書き出す。
//!
//! 検証観点:
//! 1. PsbSummary.low / .mid は ISO 532-1 (Bark 1-16) 由来で従来通り変動する。
//! 2. PsbSummary.high は Bark 21-24 (5.8k–15.5k) + 15.5k–20k FFT エネルギー由来。
//!    - S-1 (1 kHz sine): 高域はほぼ無音 → high は floor 近く (極小 dB)
//!    - S-2 (pink noise): 高域にエネルギーあり → high は実質的な dB 値
//! 3. Bark 1-16 由来 (low/mid) と Bark 21-24 由来 (high) は単位が異なるため、
//!    数値の order だけで素朴に比較しない（仕様書 guardian_61 §3, G-62-02 整合）。
//!
//! 実行:
//!   cargo test -p kirin_measure --test inspection_report_smoke -- --nocapture

use kirin_measure::phase_d::stream::PhaseDStream;
use kirin_measure::phase_d::tables::FieldType;
use std::fs::{self, File};
use std::io::Write;
use std::path::Path;
use symphonia::core::audio::{AudioBufferRef, Signal};
use symphonia::core::codecs::{DecoderOptions, CODEC_TYPE_NULL};
use symphonia::core::formats::FormatOptions;
use symphonia::core::io::MediaSourceStream;
use symphonia::core::meta::MetadataOptions;
use symphonia::core::probe::Hint;

const SIGNALS_DIR: &str = "/Users/nishiodaisuke/Dev/kirin_hypha/test_signals";
const OUTPUT_DIR: &str = "/tmp/kirin_hypha_inspection";

/// 48 kHz stereo WAV を mono f64 (L+R)/2 に decode して返す。
/// 計測時間短縮のため `max_seconds` で先頭部分のみ取得する。
fn decode_stereo_wav_to_mono_48k(path: &str, max_seconds: f64) -> Result<Vec<f64>, String> {
    let file = File::open(path).map_err(|e| format!("open {}: {}", path, e))?;
    let mss = MediaSourceStream::new(Box::new(file), Default::default());
    let mut hint = Hint::new();
    if let Some(ext) = Path::new(path).extension().and_then(|e| e.to_str()) {
        hint.with_extension(ext);
    }
    let probed = symphonia::default::get_probe()
        .format(
            &hint,
            mss,
            &FormatOptions::default(),
            &MetadataOptions::default(),
        )
        .map_err(|e| format!("probe: {}", e))?;
    let mut format = probed.format;
    let track = format
        .tracks()
        .iter()
        .find(|t| t.codec_params.codec != CODEC_TYPE_NULL)
        .ok_or("no audio track")?;
    let track_id = track.id;
    let sample_rate = track.codec_params.sample_rate.ok_or("no sample_rate")?;
    if sample_rate != 48_000 {
        return Err(format!("expected 48000 Hz, got {}", sample_rate));
    }
    let channels = track
        .codec_params
        .channels
        .map(|c| c.count())
        .ok_or("no channels")?;
    let mut decoder = symphonia::default::get_codecs()
        .make(&track.codec_params, &DecoderOptions::default())
        .map_err(|e| format!("decoder: {}", e))?;

    let max_frames = (max_seconds * sample_rate as f64) as usize;
    let mut mono = Vec::with_capacity(max_frames);

    'outer: loop {
        use symphonia::core::errors::Error as SE;
        let packet = match format.next_packet() {
            Ok(p) => p,
            Err(SE::IoError(ref e)) if e.kind() == std::io::ErrorKind::UnexpectedEof => break,
            Err(e) => return Err(format!("packet: {}", e)),
        };
        if packet.track_id() != track_id {
            continue;
        }
        let decoded = decoder
            .decode(&packet)
            .map_err(|e| format!("decode: {}", e))?;
        if let AudioBufferRef::F32(b) = decoded {
            let nch = b.spec().channels.count().min(channels);
            let nf = b.frames();
            for fr in 0..nf {
                let mut sum = 0.0f64;
                for ch in 0..nch {
                    sum += b.chan(ch)[fr] as f64;
                }
                mono.push(sum / nch as f64);
                if mono.len() >= max_frames {
                    break 'outer;
                }
            }
        } else {
            return Err("unsupported sample format (expected F32)".to_string());
        }
    }
    Ok(mono)
}

/// PsbSummary 計算は measure_thread::compute_psb_summary と同じ式。
/// 将来 measure_thread 側を変更したら同期すること。
fn compute_psb_summary_dbg(
    psb: &[f64; 20],
    bark21_24: &[f64; 4],
    high_ext: f64,
) -> (f64, f64, f64) {
    let low: f64 = psb[0..8].iter().sum();
    let mid: f64 = psb[8..16].iter().sum();
    let high_lin: f64 = bark21_24.iter().sum::<f64>() + high_ext;
    let tiny = 1e-12;
    (
        10.0 * (low + tiny).log10(),
        10.0 * (mid + tiny).log10(),
        10.0 * (high_lin + tiny).log10(),
    )
}

struct InspectionRecord<'a> {
    out_path: &'a str,
    label: &'a str,
    source_path: &'a str,
    duration_s: f64,
    psb: &'a [f64; 20],
    bark21_24: &'a [f64; 4],
    high_ext: f64,
    loudness: f64,
    sharpness: f64,
}

fn write_inspection_json(rec: &InspectionRecord) -> std::io::Result<()> {
    let (low_db, mid_db, high_db) =
        compute_psb_summary_dbg(rec.psb, rec.bark21_24, rec.high_ext);
    let mut s = String::new();
    s.push_str("{\n");
    s.push_str(&format!("  \"label\": \"{}\",\n", rec.label));
    s.push_str(&format!("  \"source\": \"{}\",\n", rec.source_path));
    s.push_str(&format!("  \"duration_seconds\": {:.3},\n", rec.duration_s));
    s.push_str(
        "  \"guardian_61_path\": \"C-3 (PsbSummary.high = Bark 21-24 + 15.5k-20k FFT power)\",\n",
    );
    s.push_str(&format!("  \"loudness_n_t_sone\": {:.4},\n", rec.loudness));
    s.push_str(&format!("  \"sharpness_acum\": {:.4},\n", rec.sharpness));
    s.push_str("  \"psb_summary\": {\n");
    s.push_str(&format!("    \"low_db\":  {:.3},   \"unit\": \"sone/Bark sum (Bark 1-8) → dB\",\n", low_db));
    s.push_str(&format!("    \"mid_db\":  {:.3},   \"unit\": \"sone/Bark sum (Bark 9-16) → dB\",\n", mid_db));
    s.push_str(&format!(
        "    \"high_db\": {:.3},   \"unit\": \"FFT power (Bark 21-24 + 15.5k-20k) → dB\"\n",
        high_db
    ));
    s.push_str("  },\n");
    s.push_str("  \"raw\": {\n");
    s.push_str(&format!("    \"psb_bark1_20_specific_loudness\": {:?},\n", rec.psb));
    s.push_str(&format!("    \"psb_bark21_24_fft_power\": {:?},\n", rec.bark21_24));
    s.push_str(&format!("    \"psb_high_ext_15_5k_20k_fft_power\": {}\n", rec.high_ext));
    s.push_str("  }\n");
    s.push_str("}\n");
    let mut f = File::create(rec.out_path)?;
    f.write_all(s.as_bytes())
}

fn run_signal(filename: &str, duration_s: f64, label: &str) -> Result<(f64, f64, f64), String> {
    let path = format!("{}/{}", SIGNALS_DIR, filename);
    let mono = decode_stereo_wav_to_mono_48k(&path, duration_s)?;
    if mono.len() < 48_000 {
        return Err(format!(
            "decoded too few samples for {}: {} < 48000",
            filename,
            mono.len()
        ));
    }

    let mut stream = PhaseDStream::new(FieldType::Free);
    let frames = stream.push(&mono);
    let last = frames.last().ok_or("no PhaseD frames produced")?;

    let (low_db, mid_db, high_db) =
        compute_psb_summary_dbg(&last.psb, &last.psb_bark21_24, last.psb_high_ext_15_5k_20k);

    fs::create_dir_all(OUTPUT_DIR).map_err(|e| format!("mkdir: {}", e))?;
    let out_path = format!(
        "{}/inspection_{}.json",
        OUTPUT_DIR,
        filename.trim_end_matches(".wav")
    );
    write_inspection_json(&InspectionRecord {
        out_path: &out_path,
        label,
        source_path: &path,
        duration_s: mono.len() as f64 / 48_000.0,
        psb: &last.psb,
        bark21_24: &last.psb_bark21_24,
        high_ext: last.psb_high_ext_15_5k_20k,
        loudness: last.loudness,
        sharpness: last.sharpness,
    })
    .map_err(|e| format!("write json: {}", e))?;

    println!("─────────────────────────────────────────────");
    println!(" inspection_report: {}", out_path);
    println!("   label              : {}", label);
    println!("   N(t)               : {:.4} sone", last.loudness);
    println!("   Sharpness          : {:.4} acum", last.sharpness);
    println!("   PsbSummary.low_db  : {:>+8.3}  (Bark 1-8 sone/Bark)", low_db);
    println!("   PsbSummary.mid_db  : {:>+8.3}  (Bark 9-16 sone/Bark)", mid_db);
    println!("   PsbSummary.high_db : {:>+8.3}  (Bark 21-24 + 15.5k-20k FFT power)", high_db);
    println!("   bark21_24 (linear) : {:?}", last.psb_bark21_24);
    println!("   high_ext  (linear) : {:.6}", last.psb_high_ext_15_5k_20k);

    Ok((low_db, mid_db, high_db))
}

#[test]
fn inspection_report_smoke_two_signals() {
    println!();
    println!("╔══════════════════════════════════════════════════════════════╗");
    println!("║  guardian_61 C-3: inspection_report.json サンプル生成          ║");
    println!("║   PsbSummary.high = Bark 21-24 + 15.5k-20k FFT power           ║");
    println!("╚══════════════════════════════════════════════════════════════╝");

    // ── S-1: 1 kHz sine -6 dBFS, 10s。3 秒だけ取って高速化。──────
    // 1 kHz は Bark 9 (920-1080 Hz) 付近 → high (Bark 21-24 + ext) は floor。
    let (s1_low, s1_mid, s1_high) =
        run_signal("S-1_1kHz_sine_m6dBFS_10s.wav", 3.0, "1 kHz sine -6 dBFS")
            .expect("S-1 inspection");
    assert!(s1_low > -100.0, "S-1 low should be active (1 kHz in Bark 9 leaks via slope into low band)");
    assert!(s1_mid > s1_low - 10.0, "S-1 mid (containing 1 kHz) should dominate or rival low");
    // 1 kHz: 高域 (Bark 21-24 + 15.5k-20k) はほぼ無音 → high は -100 dB 未満に floor。
    assert!(
        s1_high < -50.0,
        "S-1 high should floor (no energy >5.8 kHz): got {:.3} dB",
        s1_high
    );

    // ── S-2: Pink noise -23 LUFS, 60s。3 秒で十分。──────────────
    // Pink noise: 全帯域にエネルギー、-3 dB/oct で減衰。
    // 高域 (5.8k-20k) にも実質エネルギーあり → high は floor から大きく上。
    let (s2_low, s2_mid, s2_high) =
        run_signal("S-2_pinknoise_m23LUFS_60s.wav", 3.0, "Pink noise -23 LUFS")
            .expect("S-2 inspection");
    assert!(s2_low > -50.0, "S-2 low should have substantial energy");
    assert!(s2_mid > -50.0, "S-2 mid should have substantial energy");
    assert!(
        s2_high > s1_high + 20.0,
        "S-2 high should be MUCH greater than S-1 high (pink noise has high-freq content): \
         S-1={:.3}, S-2={:.3}",
        s1_high, s2_high
    );

    println!();
    println!("✓ inspection_report.json 2 件生成完了 → {}", OUTPUT_DIR);
    println!("  S-1 high = {:>+8.3} dB  (1 kHz sine: 高域 floor 期待)", s1_high);
    println!("  S-2 high = {:>+8.3} dB  (pink noise: 高域あり期待)", s2_high);
}
