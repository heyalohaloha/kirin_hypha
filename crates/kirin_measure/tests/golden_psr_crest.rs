//! B-079: Crest / PSR / LUFS-I / LRA の golden — 既知信号 (test_signals S-1..S-5) で
//! 計測精度を「主張でなく実証」する。engine の計算式は不変・test 追加のみ。
//!
//! 期待値はすべて信号定義 (xtask/src/gen_signals.rs) からの理論値 (ZSA / 推測値なし):
//! - S-1/S-4: gen_sine_stereo(1000Hz, -6 dBFS)。amp = 10^(-6/20) = 0.501。
//! - S-2/S-3/S-5: pink noise を normalize_to_lufs(target) で ebur128 loudness_global 校正。
//!
//! 許容根拠:
//! - Crest = 20·log10(√2) = 3.0103 dB。正弦の peak/RMS 比で **振幅・周波数・K-weight 非依存の
//!   exact 値**。許容 0.3 dB は 400ms 窓が周期の非整数倍になる離散化誤差の上限。
//! - LUFS-I(pink) は同一 ebur128 で target に gain 校正済 → engine が読み戻す。許容 1.0 LU は
//!   gating/チャンク差の上限。
//! - LUFS-I(sine)/PSR(sine) は K-weighting を含むため厳密な閉形式が出ない（帯域照合 + 理由を
//!   report）。dual-mono(L=R) では ITU LUFS が両 ch を G=1.0 で合算し +3.01 dB、これが正弦の
//!   RMS(peak より -3.01 dB)と **相殺**する。よって
//!   LUFS(dual-mono sine) = peak_dBFS - 0.691 + G_k(1kHz) （0.691 = ITU LUFS 校正定数）、
//!   PSR = peak_dBFS - LUFS_S = 0.691 - G_k(1kHz)。
//!   閉形式で出ないのは K-weight gain G_k(1kHz)(≈+0.7 dB) のみ。S-1(-6 dBFS) では
//!   LUFS-I ≈ -6.69 + G_k ≈ -6.0、PSR ≈ 0.691 - G_k ≈ 0.0（観測 -0.007 が G_k≈0.7 を裏付ける）。
//! - LRA(定常トーン) ≈ 0（ロードネス変動なし）。

use kirin_measure::engine::{MeasureEngine, SessionSummary};
use kirin_measure::MeasureResult;

use std::fs::File;
use std::path::Path;
use symphonia::core::audio::{AudioBufferRef, Signal};
use symphonia::core::codecs::{DecoderOptions, CODEC_TYPE_NULL};
use symphonia::core::formats::FormatOptions;
use symphonia::core::io::MediaSourceStream;
use symphonia::core::meta::MetadataOptions;
use symphonia::core::probe::Hint;

const SIGNALS_DIR: &str = concat!(env!("CARGO_MANIFEST_DIR"), "/../../test_signals");

/// WAV を decode して (interleaved f64, sample_rate, channels)。
fn decode_wav(path: &str) -> (Vec<f64>, u32, usize) {
    let file = File::open(path).unwrap_or_else(|e| panic!("open {path}: {e}"));
    let mss = MediaSourceStream::new(Box::new(file), Default::default());
    let mut hint = Hint::new();
    if let Some(ext) = Path::new(path).extension().and_then(|e| e.to_str()) {
        hint.with_extension(ext);
    }
    let probed = symphonia::default::get_probe()
        .format(&hint, mss, &FormatOptions::default(), &MetadataOptions::default())
        .expect("probe");
    let mut format = probed.format;
    let track = format
        .tracks()
        .iter()
        .find(|t| t.codec_params.codec != CODEC_TYPE_NULL)
        .expect("audio track");
    let track_id = track.id;
    let sample_rate = track.codec_params.sample_rate.expect("sample_rate");
    let channels = track.codec_params.channels.map(|c| c.count()).expect("channels");

    let mut decoder = symphonia::default::get_codecs()
        .make(&track.codec_params, &DecoderOptions::default())
        .expect("decoder");

    let mut inter: Vec<f64> = Vec::new();
    loop {
        use symphonia::core::errors::Error as SE;
        let packet = match format.next_packet() {
            Ok(p) => p,
            Err(SE::IoError(ref e)) if e.kind() == std::io::ErrorKind::UnexpectedEof => break,
            Err(e) => panic!("packet: {e}"),
        };
        if packet.track_id() != track_id {
            continue;
        }
        let decoded = decoder.decode(&packet).expect("decode");
        match decoded {
            AudioBufferRef::S24(b) => {
                let nch = b.spec().channels.count().min(channels);
                for fr in 0..b.frames() {
                    for ch in 0..nch {
                        inter.push(b.chan(ch)[fr].inner() as f64 / 8_388_607.0);
                    }
                }
            }
            AudioBufferRef::F32(b) => {
                let nch = b.spec().channels.count().min(channels);
                for fr in 0..b.frames() {
                    for ch in 0..nch {
                        inter.push(b.chan(ch)[fr] as f64);
                    }
                }
            }
            AudioBufferRef::F64(b) => {
                let nch = b.spec().channels.count().min(channels);
                for fr in 0..b.frames() {
                    for ch in 0..nch {
                        inter.push(b.chan(ch)[fr]);
                    }
                }
            }
            _ => panic!("unsupported sample format in {path}"),
        }
    }
    (inter, sample_rate, channels)
}

/// 信号を MeasureEngine に 100ms チャンクで流し、最後の MeasureResult と finalize() を返す。
fn run_engine(filename: &str) -> (MeasureResult, SessionSummary) {
    let path = format!("{SIGNALS_DIR}/{filename}");
    let (inter, sr, ch) = decode_wav(&path);
    let mut eng = MeasureEngine::new(sr, ch).expect("engine init");
    let chunk_elems = (sr as usize / 10) * ch; // 100ms
    let mut last = MeasureResult::default();
    for c in inter.chunks(chunk_elems) {
        if let Some(r) = eng.push(c) {
            last = r;
        }
    }
    let summary = eng.finalize();
    (last, summary)
}

/// Crest: 正弦の peak/RMS 比 = 20·log10(√2) = 3.0103 dB（exact / K-weight 非依存）。
#[test]
fn golden_crest_sine_is_3dot01_db() {
    for f in ["S-1_1kHz_sine_m6dBFS_10s.wav", "S-4_1kHz_sine_m6dBFS_1s.wav"] {
        let (last, _) = run_engine(f);
        let crest = last.crest.unwrap_or_else(|| panic!("{f}: crest None"));
        println!("[golden] {f}: crest = {crest:.4} dB (理論 3.0103)");
        assert!(
            (crest - 3.0103).abs() < 0.3,
            "{f}: crest={crest} expected 3.0103 dB (sine peak/RMS, exact)"
        );
    }
}

/// LUFS-I(pink): gen_signals が ebur128 loudness_global で target に gain 校正済 →
/// 同 ebur128 の engine が target を読み戻す。
#[test]
fn golden_lufs_i_pink_matches_calibrated_target() {
    for (f, target) in [
        ("S-2_pinknoise_m23LUFS_60s.wav", -23.0_f64),
        ("S-3_pinknoise_m33LUFS_60s.wav", -33.0),
        ("S-5_pinknoise_m14LUFS_60s.wav", -14.0),
    ] {
        let (_, s) = run_engine(f);
        let lufs = s.lufs_i.unwrap_or_else(|| panic!("{f}: lufs_i None"));
        println!("[golden] {f}: lufs_i = {lufs:.3} LUFS (校正 target {target})");
        assert!(
            (lufs - target).abs() < 1.0,
            "{f}: lufs_i={lufs} expected≈{target} (calibrated via same ebur128)"
        );
    }
}

/// PSR / LUFS-I / LRA(sine): K-weighting を含むため帯域照合（締められない理由は report）。
#[test]
fn golden_psr_lufs_lra_sine_within_derived_bands() {
    let (last, s) = run_engine("S-1_1kHz_sine_m6dBFS_10s.wav");

    // PSR = peak_dBFS - LUFS_S = 0.691 - G_k(1kHz)。dual-mono 正弦では sine RMS(-3.01) と
    // ITU stereo 合算(+3.01) が相殺し ≈0（観測 -0.007 → G_k(1kHz)≈0.7）。K-weight 厳密値が
    // 閉形式で出ないため帯域 [-1.0, 1.5]（G_k ∈ 約 -0.8..1.7 を許容）。
    let psr = last.psr.unwrap_or_else(|| panic!("S-1: psr None (10s ≥ 3s short-term)"));
    println!("[golden] S-1 psr = {psr:.3} LU (理論 0.691 - K-weight(1kHz) ≈ 0)");
    assert!(
        (-1.0..1.5).contains(&psr),
        "S-1: psr={psr} expected 0.691 - G_k(1kHz) ≈ 0 (dual-mono sine: RMS と stereo 合算が相殺)"
    );

    // LUFS-I = peak_dBFS - 0.691 + G_k(1kHz) ≈ -6.69 + G_k ≈ -6.0（S-1 peak -6 dBFS）。帯域 [-7.5, -5.5]。
    let lufs = s.lufs_i.expect("S-1: lufs_i");
    println!("[golden] S-1 lufs_i = {lufs:.3} LUFS (理論 -6.69 + K-weight(1kHz) ≈ -6.0)");
    assert!(
        (-7.5..-5.5).contains(&lufs),
        "S-1: lufs_i={lufs} expected -6.69 + G_k(1kHz) ≈ -6.0"
    );

    // LRA: 定常トーン → ロードネス変動なし → ≈0（None もあり得る）。
    match s.lra {
        Some(lra) => {
            println!("[golden] S-1 lra = {lra:.3} LU (理論 ≈0 定常)");
            assert!(lra < 1.0, "S-1: steady-tone LRA={lra} expected≈0");
        }
        None => println!("[golden] S-1 lra = None (定常トーンで未確定 = 許容)"),
    }
}
