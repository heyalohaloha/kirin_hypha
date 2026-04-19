//! Phase A-04 True Peak オフライン基準値計算
//!
//! Lensエンジンと同一の手法（symphonia decode + ebur128 true_peak running max）で
//! テスト信号06-10のオフラインTP値を計算し、Hypha実機値との比較基準を作る。
//!
//! 実行方法:
//!   cargo test -p kirin_measure tp_offline -- --nocapture

use ebur128::{EbuR128, Mode};
use std::fs::File;
use std::path::Path;
use symphonia::core::audio::{AudioBufferRef, Signal};
use symphonia::core::codecs::{DecoderOptions, CODEC_TYPE_NULL};
use symphonia::core::formats::FormatOptions;
use symphonia::core::io::MediaSourceStream;
use symphonia::core::meta::MetadataOptions;
use symphonia::core::probe::Hint;

const SIGNALS_DIR: &str = "/Users/nishiodaisuke/Dev/kirin_sense_lens/test_signals";

/// Lensエンジンと同一ロジック：ebur128 true_peak(ch) = ファイル全体の running max
fn offline_true_peak_dBTP(path: &str) -> Result<f64, String> {
    // decode
    let file = File::open(path).map_err(|e| format!("open: {}", e))?;
    let mss = MediaSourceStream::new(Box::new(file), Default::default());
    let mut hint = Hint::new();
    if let Some(ext) = Path::new(path).extension().and_then(|e| e.to_str()) {
        hint.with_extension(ext);
    }
    let probed = symphonia::default::get_probe()
        .format(&hint, mss, &FormatOptions::default(), &MetadataOptions::default())
        .map_err(|e| format!("probe: {}", e))?;
    let mut format = probed.format;

    let track = format
        .tracks()
        .iter()
        .find(|t| t.codec_params.codec != CODEC_TYPE_NULL)
        .ok_or("no audio track")?;
    let track_id = track.id;
    let sample_rate = track.codec_params.sample_rate.ok_or("no sample_rate")?;
    let channels = track
        .codec_params
        .channels
        .map(|c| c.count() as u32)
        .ok_or("no channels")?;

    let mut decoder = symphonia::default::get_codecs()
        .make(&track.codec_params, &DecoderOptions::default())
        .map_err(|e| format!("decoder: {}", e))?;

    // ebur128 — TRUE_PEAK モードのみ（Lens loudness.rs と同じ設定）
    let mode = Mode::TRUE_PEAK;
    let mut ebu =
        EbuR128::new(channels, sample_rate, mode).map_err(|e| format!("ebur128: {:?}", e))?;

    // 100ms チャンク単位で投入（Lens と同一）
    let chunk_frames = sample_rate as usize / 10;
    let mut all_interleaved: Vec<f64> = Vec::new();

    loop {
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

        match decoded {
            AudioBufferRef::S24(b) => {
                let nch = b.spec().channels.count().min(channels as usize);
                let nf = b.frames();
                for fr in 0..nf {
                    for ch in 0..nch {
                        all_interleaved.push(b.chan(ch)[fr].inner() as f64 / 8_388_607.0);
                    }
                }
            }
            AudioBufferRef::F32(b) => {
                let nch = b.spec().channels.count().min(channels as usize);
                let nf = b.frames();
                for fr in 0..nf {
                    for ch in 0..nch {
                        all_interleaved.push(b.chan(ch)[fr] as f64);
                    }
                }
            }
            AudioBufferRef::F64(b) => {
                let nch = b.spec().channels.count().min(channels as usize);
                let nf = b.frames();
                for fr in 0..nf {
                    for ch in 0..nch {
                        all_interleaved.push(b.chan(ch)[fr]);
                    }
                }
            }
            _ => return Err("unsupported sample format".to_string()),
        }
    }

    // 100ms チャンク単位で add_frames_f64
    let step = chunk_frames * channels as usize;
    let mut offset = 0;
    while offset < all_interleaved.len() {
        let end = (offset + step).min(all_interleaved.len());
        ebu.add_frames_f64(&all_interleaved[offset..end])
            .map_err(|e| format!("add_frames: {:?}", e))?;
        offset = end;
    }

    // running max true_peak — チャンネル最大を取る（Lens と同一）
    let tp_linear = (0..channels)
        .filter_map(|ch| ebu.true_peak(ch).ok())
        .fold(0.0_f64, f64::max);

    if tp_linear > 0.0 {
        Ok(20.0 * tp_linear.log10())
    } else {
        Err("true_peak is 0 (no audio?)".to_string())
    }
}

#[test]
fn tp_offline_phase_a04() {
    // Phase A-04 テスト信号 5本（manifest 06-10）
    let signals: &[(&str, f64, &str)] = &[
        ("06_truepeak_phase0.wav",    0.0,  "997Hz 0dBFS phase0"),
        ("07_truepeak_phase1.wav",    0.0,  "997Hz 0dBFS phase1"),
        ("08_truepeak_phase2.wav",    0.0,  "997Hz 0dBFS phase2"),
        ("09_truepeak_phase3.wav",    0.0,  "997Hz 0dBFS phase3"),
        ("10_truepeak_near_nyquist.wav", -6.0, "11760Hz -6dBFS near-Nyquist"),
    ];

    println!();
    println!("╔═══════════════════════════════════════════════════════════════════════════════╗");
    println!("║          Phase A-04: True Peak オフライン基準値 (Lens同等エンジン)            ║");
    println!("╠════════════════════════════════╦══════════╦══════════╦══════════╦═════════════╣");
    println!("║ ファイル                       ║ 期待値   ║ 計算値   ║ 誤差     ║ Hypha実機値 ║");
    println!("╠════════════════════════════════╬══════════╬══════════╬══════════╬═════════════╣");

    let mut all_pass = true;
    for (filename, expected, label) in signals {
        let path = format!("{}/{}", SIGNALS_DIR, filename);
        match offline_true_peak_dBTP(&path) {
            Ok(tp) => {
                let diff = tp - expected;
                let ok = diff.abs() < 0.5;
                if !ok { all_pass = false; }
                println!(
                    "║ {:<30} ║ {:>+7.3}  ║ {:>+7.3}  ║ {:>+7.3}  ║ 未測定      ║",
                    label, expected, tp, diff
                );
            }
            Err(e) => {
                all_pass = false;
                println!("║ {:<30} ║ {:>+7.3}  ║ ERROR    ║ ---      ║ 未測定      ║", label, expected);
                eprintln!("  ERROR {}: {}", filename, e);
            }
        }
    }

    println!("╚════════════════════════════════╩══════════╩══════════╩══════════╩═════════════╝");
    println!();
    println!("  合格基準: |計算値 - 期待値| < 0.5 dBTP");
    println!("  Hypha実機値: Studio One で各信号を再生し、PRE JSON の true_peak を記録");
    println!();

    assert!(all_pass, "オフライン TP 計算に失敗した信号があります");
}
