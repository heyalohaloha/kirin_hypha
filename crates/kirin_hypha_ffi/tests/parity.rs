//! Phase 1 パリティ検証 — FFI 経由(create→push_samples→poll_result)の出力が、
//! `kirin_measure` を直接叩いた結果と既存 MoSQITo tolerance 内で一致することを示す。
//!
//! 対象 = MoSQITo 系のみ（Zwicker N / Sharpness / PSB / n_prime[20] / psb_bark[20]）。
//! tp_offline_reference / lra_plr_session は SessionSummary 経路（Record=Phase 3）のため Phase 1 対象外。
//!
//! 許容誤差（kirin_measure/Cargo.toml:26-28 の MoSQITo tolerance に準拠 / 新しい緩い閾値は作らない）:
//!   - Zwicker N (n_prime_total / n_prime[]) : max_relative = 1e-3
//!   - Sharpness                              : epsilon = 0.05 acum
//!   - PSB（kirin-original / 文書化 tol 無し）: FFI と direct は同一コード経路なので tight に固定
//!     （N と同等の max_relative=1e-3 + 微小 abs floor。緩めない）。
//!
//! 入力は **L==R のステレオ**（本番 measure_thread の (L+R)*0.5 → mono 等価）。

use std::f64::consts::PI;
use std::thread::sleep;
use std::time::Duration;

use approx::assert_relative_eq;

use kirin_hypha_ffi::KirinHyphaEngine;
use kirin_measure::phase_d::stream::{PhaseDResult, PhaseDStream};
use kirin_measure::phase_d::tables::FieldType;

const SR: u32 = 48_000;

// MoSQITo tolerance（kirin_measure/Cargo.toml:26-28）
const N_MAX_REL: f64 = 1e-3; // N_spec relative
const SHARP_EPS: f64 = 0.05; // sharpness abs [acum]
const PSB_ABS_FLOOR: f64 = 1e-9; // 同一コード経路の数値ノイズ吸収用の微小 abs floor

/// 定常マルチトーン（200/1000/5000 Hz）, L==R, f32 interleaved。
fn gen_stereo_f32(seconds: f64) -> Vec<f32> {
    let n = (SR as f64 * seconds) as usize;
    let mut v = Vec::with_capacity(n * 2);
    for i in 0..n {
        let t = i as f64 / SR as f64;
        let s = 0.3 * (2.0 * PI * 200.0 * t).sin()
            + 0.3 * (2.0 * PI * 1000.0 * t).sin()
            + 0.2 * (2.0 * PI * 5000.0 * t).sin();
        let s = s as f32;
        v.push(s); // L
        v.push(s); // R (== L)
    }
    v
}

/// direct 参照: measure_thread の phase_d 経路を再現（f32→f64→(L+R)*0.5→PhaseDStream）。
fn direct_phase_d(stereo_f32: &[f32]) -> PhaseDResult {
    let mut stream = PhaseDStream::new(FieldType::Free);
    let mono: Vec<f64> = stereo_f32
        .chunks_exact(2)
        .map(|c| (c[0] as f64 + c[1] as f64) * 0.5)
        .collect();
    let frames = stream.push(&mono);
    frames
        .last()
        .cloned()
        .expect("PhaseDStream should emit at least one frame for multi-second input")
}

/// measure_thread::compute_psb_summary（measure_thread.rs:316-334）の再現。
/// FFI の psb_summary(low/mid/high) を検算するための参照。
fn psb_summary_ref(psb: &[f64; 20], psb_bark21_24: &[f64; 4], high_ext: f64) -> (f64, f64, f64) {
    let low: f64 = psb[0..8].iter().sum();
    let mid: f64 = psb[8..16].iter().sum();
    let high_lin: f64 = psb_bark21_24.iter().sum::<f64>() + high_ext;
    let tiny = 1e-12;
    (
        10.0 * (low + tiny).log10(),
        10.0 * (mid + tiny).log10(),
        10.0 * (high_lin + tiny).log10(),
    )
}

/// FFI を駆動して、phase_d 系が揃った最新 RT 結果を取得する。
fn drive_ffi(stereo_f32: &[f32]) -> (kirin_measure::MeasureResult, u64) {
    let engine = KirinHyphaEngine::new(SR, 2);
    engine.set_signal_state(1); // Active (ABI code)

    // 0.1s ブロックを ~30ms 間隔で投入（consumer は 100ms ごとに全 drain → ring 2s に十分収まる）。
    let block_frames = SR as usize / 10; // 0.1s
    let block_len = block_frames * 2; // stereo
    let mut i = 0;
    while i < stereo_f32.len() {
        let end = (i + block_len).min(stereo_f32.len());
        engine.push_samples(&stereo_f32[i..end], 2);
        i = end;
        sleep(Duration::from_millis(30));
    }

    // keepalive（heartbeat を進めて Active 維持）しつつ、残サンプルが drain され結果が
    // 安定するまでポーリング。定常入力なので後半フレームは収束している。
    let mut last: Option<kirin_measure::MeasureResult> = None;
    for _ in 0..40 {
        engine.push_samples(&[], 2); // 0-frame keepalive
        sleep(Duration::from_millis(50));
        if let Some(r) = engine.poll_result() {
            if r.n_prime_total.is_some()
                && r.sharpness.is_some()
                && r.psb_summary.is_some()
                && r.n_prime.is_some()
                && r.psb_bark.is_some()
            {
                last = Some(r);
            }
        }
    }
    let overflow = engine.overflow_count();
    (
        last.expect("FFI should produce a fully-populated MeasureResult (phase_d merged)"),
        overflow,
    )
}

#[test]
fn parity_phase_d_metrics_ffi_vs_direct() {
    let signal = gen_stereo_f32(3.0);

    // direct 参照
    let direct = direct_phase_d(&signal);
    let (ref_low, ref_mid, ref_high) =
        psb_summary_ref(&direct.psb, &direct.psb_bark21_24, direct.psb_high_ext_15_5k_20k);

    // FFI 経由
    let (res, overflow) = drive_ffi(&signal);

    // パリティ駆動でサンプルが落ちていない（= 同一サンプル列を処理した）ことを確認。
    assert_eq!(overflow, 0, "parity push dropped samples (ring overflow={overflow})");

    // ── Zwicker N (n_prime_total) : MoSQITo N tolerance ──
    let n_ffi = res.n_prime_total.unwrap();
    assert_relative_eq!(n_ffi, direct.loudness, max_relative = N_MAX_REL, epsilon = PSB_ABS_FLOOR);

    // ── Sharpness : MoSQITo sharpness tolerance ──
    let sh_ffi = res.sharpness.unwrap();
    approx::assert_abs_diff_eq!(sh_ffi, direct.sharpness, epsilon = SHARP_EPS);

    // ── n_prime[20] : MoSQITo N tolerance（band 毎）──
    let np = res.n_prime.unwrap();
    for (k, &np_k) in np.iter().enumerate() {
        assert_relative_eq!(np_k, direct.n_prime[k], max_relative = N_MAX_REL, epsilon = PSB_ABS_FLOOR);
    }

    // ── psb_bark[20] (= PhaseDResult.psb) : 同一コード経路なので tight ──
    let pb = res.psb_bark.unwrap();
    for (k, &pb_k) in pb.iter().enumerate() {
        assert_relative_eq!(pb_k, direct.psb[k], max_relative = N_MAX_REL, epsilon = PSB_ABS_FLOOR);
    }

    // ── PSB low/mid/high : compute_psb_summary 再現と一致 ──
    let s = res.psb_summary.unwrap();
    assert_relative_eq!(s.low, ref_low, max_relative = N_MAX_REL, epsilon = 1e-6);
    assert_relative_eq!(s.mid, ref_mid, max_relative = N_MAX_REL, epsilon = 1e-6);
    assert_relative_eq!(s.high, ref_high, max_relative = N_MAX_REL, epsilon = 1e-6);
}

#[test]
fn poll_session_is_false_in_phase1() {
    // Phase 1 では SessionSummary 経路（Record 依存）を埋めない。常に None。
    let engine = KirinHyphaEngine::new(SR, 2);
    engine.set_signal_state(1);
    let signal = gen_stereo_f32(0.5);
    engine.push_samples(&signal, 2);
    sleep(Duration::from_millis(250));
    assert!(
        engine.poll_session().is_none(),
        "poll_session must be None in Phase 1 (Record out of scope)"
    );
}

#[test]
fn rt_safety_no_overflow_at_juce_block_sizes() {
    // JUCE 代表ブロック 64/128/256/512/1024 frames×2ch を ~real-time で連投し、
    // rtrb push が一度も fail しない（ring 2s で足りる）ことを確認（§8）。
    for &block in &[64usize, 128, 256, 512, 1024] {
        let engine = KirinHyphaEngine::new(SR, 2);
        engine.set_signal_state(1);

        // 小さな非ゼロ値（Active 維持。silent 判定は host 側責務だが念のため非ゼロ）。
        let blk = vec![0.01f32; block * 2];
        let total_frames = SR as usize; // 1 秒
        let mut pushed = 0usize;
        let block_dt = Duration::from_secs_f64(block as f64 / SR as f64);
        while pushed < total_frames {
            engine.push_samples(&blk, 2);
            pushed += block;
            sleep(block_dt); // real-time pacing
        }
        assert_eq!(
            engine.overflow_count(),
            0,
            "ring overflow at block size {block} (overflow={})",
            engine.overflow_count()
        );
    }
}
