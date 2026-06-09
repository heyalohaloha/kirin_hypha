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
//!   唯一 K-weight gain G_k(1kHz) のみ閉形式で出ないが、B-080 で ITU-R BS.1770 規定の K-weighting
//!   係数から `kweight_gain_db()` で独立算出し（観測非依存）、PSR/LUFS-I(sine) を point 照合する。
//!   S-1(-6 dBFS): LUFS-I = -6.0 - 0.691 + G_k、PSR = 0.691 - G_k。一致は engine の K-weighting
//!   が ITU 準拠であることの実証。
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

/// 信号を MeasureEngine に 100ms チャンクで流し、最後の MeasureResult / finalize() / sample_rate。
fn run_engine(filename: &str) -> (MeasureResult, SessionSummary, u32) {
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
    (last, summary, sr)
}

/// ITU-R BS.1770-4 K-weighting の `freq` における gain [dB] を **規定パラメータから独立算出**する
/// （観測からの逆算ではない）。2 段カスケード:
/// - Stage 1 high-shelf pre-filter: f0=1681.974450955533, G=+3.999843853973347 dB, Q=0.7071752369554196
/// - Stage 2 RLB high-pass:         f0=38.13547087602444,  Q=0.5003270373238773
///
/// 各段を ITU 規定の双一次変換式で biquad 化し、z=e^(jω) を代入して |H(e^jω)| を求め dB 化する。
/// この規定パラメータは ebur128 0.1.10 `filter.rs::filter_coefficients` と同一（= engine が使う係数）。
/// 一致すれば engine の K-weighting が ITU 準拠であることの実証になる。
fn kweight_gain_db(freq: f64, rate: f64) -> f64 {
    // 単一 biquad の |H(e^jω)| = |Σ b_k e^{-jkω}| / |Σ a_k e^{-jkω}|。
    fn biquad_mag(b: [f64; 3], a: [f64; 3], w: f64) -> f64 {
        let (c1, s1) = ((-w).cos(), (-w).sin()); // e^{-jω}
        let (c2, s2) = ((-2.0 * w).cos(), (-2.0 * w).sin()); // e^{-j2ω}
        let nr = b[0] + b[1] * c1 + b[2] * c2;
        let ni = b[1] * s1 + b[2] * s2;
        let dr = a[0] + a[1] * c1 + a[2] * c2;
        let di = a[1] * s1 + a[2] * s2;
        (nr * nr + ni * ni).sqrt() / (dr * dr + di * di).sqrt()
    }
    // Stage 1: high-shelf
    let (f0, g, q) = (1681.974450955533_f64, 3.999843853973347_f64, 0.7071752369554196_f64);
    let k = (std::f64::consts::PI * f0 / rate).tan();
    let vh = 10.0_f64.powf(g / 20.0);
    let vb = vh.powf(0.4996667741545416);
    let a0 = 1.0 + k / q + k * k;
    let b1 = [
        (vh + vb * k / q + k * k) / a0,
        2.0 * (k * k - vh) / a0,
        (vh - vb * k / q + k * k) / a0,
    ];
    let a1 = [1.0, 2.0 * (k * k - 1.0) / a0, (1.0 - k / q + k * k) / a0];
    // Stage 2: RLB high-pass
    let (f0, q) = (38.13547087602444_f64, 0.5003270373238773_f64);
    let k = (std::f64::consts::PI * f0 / rate).tan();
    let d = 1.0 + k / q + k * k;
    let b2 = [1.0, -2.0, 1.0];
    let a2 = [1.0, 2.0 * (k * k - 1.0) / d, (1.0 - k / q + k * k) / d];

    let w = 2.0 * std::f64::consts::PI * freq / rate;
    20.0 * (biquad_mag(b1, a1, w) * biquad_mag(b2, a2, w)).log10()
}

/// Crest: 正弦の peak/RMS 比 = 20·log10(√2) = 3.0103 dB（exact / K-weight 非依存）。
#[test]
fn golden_crest_sine_is_3dot01_db() {
    for f in ["S-1_1kHz_sine_m6dBFS_10s.wav", "S-4_1kHz_sine_m6dBFS_1s.wav"] {
        let (last, _, _) = run_engine(f);
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
        let (_, s, _) = run_engine(f);
        let lufs = s.lufs_i.unwrap_or_else(|| panic!("{f}: lufs_i None"));
        println!("[golden] {f}: lufs_i = {lufs:.3} LUFS (校正 target {target})");
        assert!(
            (lufs - target).abs() < 1.0,
            "{f}: lufs_i={lufs} expected≈{target} (calibrated via same ebur128)"
        );
    }
}

/// PSR / LUFS-I / LRA(sine): K-weight G_k(1kHz) を ITU 規定係数から独立算出し **point 照合**する
/// （B-080: B-079 の帯域照合を point へ締める）。一致は engine の K-weighting が ITU 準拠である実証。
#[test]
fn golden_psr_lufs_sine_point_via_itu_kweight() {
    let (last, s, sr) = run_engine("S-1_1kHz_sine_m6dBFS_10s.wav");

    // ITU-R BS.1770 規定係数から算出した 1kHz の K-weight gain（観測非依存）。
    let g_k = kweight_gain_db(1000.0, sr as f64);
    println!("[golden] G_k(1kHz @ {sr}Hz) = {g_k:.4} dB (ITU 規定係数から独立算出)");

    // S-1 peak = -6.0 dBFS（gen_sine: amp = 10^(-6/20)）。dual-mono 正弦で正弦 RMS(-3.01) と
    // ITU stereo 合算(+3.01) が相殺するため:
    //   PSR    = peak - LUFS_S = 0.691 - G_k
    //   LUFS-I = peak - 0.691 + G_k
    let peak_dbfs = -6.0_f64;
    let psr_theory = 0.691 - g_k;
    let lufs_theory = peak_dbfs - 0.691 + g_k;

    let psr = last.psr.unwrap_or_else(|| panic!("S-1: psr None (10s ≥ 3s short-term)"));
    println!("[golden] S-1 psr = {psr:.4} LU  (理論 {psr_theory:.4} = 0.691 - G_k / 残差 {:.4})", psr - psr_theory);
    assert!(
        (psr - psr_theory).abs() < 0.2,
        "S-1: psr={psr} vs ITU theory {psr_theory} (0.691 - G_k(1kHz)); residual too large"
    );

    let lufs = s.lufs_i.expect("S-1: lufs_i");
    println!("[golden] S-1 lufs_i = {lufs:.4} LUFS (理論 {lufs_theory:.4} = peak - 0.691 + G_k / 残差 {:.4})", lufs - lufs_theory);
    assert!(
        (lufs - lufs_theory).abs() < 0.2,
        "S-1: lufs_i={lufs} vs ITU theory {lufs_theory} (peak - 0.691 + G_k(1kHz)); residual too large"
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
