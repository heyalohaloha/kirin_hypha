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
use kirin_measure::engine::{MeasureEngine, SessionSummary};
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

// ── Phase 3a: Record 遷移 + license gate + poll_session 実体化のパリティ ──────

/// 参照: measure_thread が `engine.push` に渡すのと同一の f64 列（ring f32 を `as f64`・
/// stereo interleaved・48k で resample なし / measure_thread.rs:232,266）を直接
/// `MeasureEngine` に通して finalize する。
fn direct_session(stereo_f32: &[f32]) -> SessionSummary {
    let mut engine = MeasureEngine::new(SR, 2).expect("MeasureEngine init");
    let f64buf: Vec<f64> = stereo_f32.iter().map(|&s| s as f64).collect();
    let chunk = (SR as usize / 10) * 2; // 100ms stereo interleaved
    for c in f64buf.chunks(chunk) {
        let _ = engine.push(c);
    }
    engine.finalize()
}

/// FFI で Record セッションを駆動して poll_session を取得する。
/// 駆動ペースは `drive_ffi`（phase_d parity）と同じ 0.1s/30ms（ring 2s で overflow=0）。
fn drive_ffi_session(stereo_f32: &[f32]) -> (SessionSummary, u64, bool) {
    let engine = KirinHyphaEngine::new(SR, 2);
    engine.set_license(0); // Os
    engine.set_signal_state(1); // Active
    assert!(engine.enter_record(), "Os + Watch なら enter_record は true");
    assert!(engine.is_recording(), "enter_record 成功後は Record 中");

    // Measure Thread が is_recording を観測して engine.reset() するのを ring 空のまま待つ。
    // 待機中も keepalive で heartbeat を進め signal_state Active を維持する
    // （無音待機すると heartbeat stall override で Inactive に落ち計測がスキップされる
    //   / measure_thread.rs:160-169。実プラグインは毎ブロック set_signal_state する）。
    for _ in 0..6 {
        engine.push_samples(&[], 2); // 0-frame keepalive（ring 空のまま heartbeat++）
        sleep(Duration::from_millis(40));
    }

    // 0.1s ブロックを realtime ペース（100ms 間隔）で投入する。
    // Record 中は Measure Thread が毎ループ finalize() も走らせ処理が重くなるため、
    // 3.3x ペース（phase_d parity の 30ms）では追いつかず overflow する。realtime なら
    // 消費が追いつき overflow=0（rt_safety_no_overflow_at_juce_block_sizes と同方針）。
    let block_frames = SR as usize / 10;
    let block_len = block_frames * 2;
    let block_dt = Duration::from_secs_f64(block_frames as f64 / SR as f64); // 0.1s
    let mut i = 0;
    while i < stereo_f32.len() {
        let end = (i + block_len).min(stereo_f32.len());
        engine.push_samples(&stereo_f32[i..end], 2);
        i = end;
        sleep(block_dt);
    }

    // drain + 安定化: lufs_i が 2 回連続一致 = ring 全消費 = 参照と同一サンプル集合。
    let mut prev: Option<f64> = None;
    let mut stable: Option<SessionSummary> = None;
    for _ in 0..120 {
        engine.push_samples(&[], 2); // keepalive（is_recording 維持・heartbeat）
        sleep(Duration::from_millis(50));
        if let Some(s) = engine.poll_session() {
            if s.lufs_i.is_some() && s.lufs_i == prev {
                stable = Some(s);
                break;
            }
            prev = s.lufs_i;
        }
    }
    let overflow = engine.overflow_count();
    let was_recording = engine.is_recording();
    engine.exit_record(); // Watch へ。直近 finalize 値は保持される。
    (
        stable.expect("poll_session の lufs_i が安定すること（全サンプル finalize 済）"),
        overflow,
        was_recording,
    )
}

/// FFI の poll_session（Record finalize）が、同一サンプルを直接 MeasureEngine に通した
/// finalize と一致することを示す（殻が精度を変えない / abs≈0）。
#[test]
fn session_finalize_ffi_matches_direct_engine() {
    // 12 秒（loudness_global の最小窓 ~10s 超）の定常マルチトーン（L==R）。
    let signal = gen_stereo_f32(12.0);

    let (ffi, overflow, was_recording) = drive_ffi_session(&signal);
    assert_eq!(overflow, 0, "ring overflow: FFI が全サンプルを処理していない (of={overflow})");
    assert!(was_recording, "exit 直前まで Record 中であること");
    let direct = direct_session(&signal);

    // 値域の妥当性（lra_plr_session_test と同基準の広め）。
    let li = ffi.lufs_i.expect("lufs_i must be Some after 12s");
    let tp = ffi.max_true_peak.expect("max_true_peak must be Some");
    assert!((-30.0..0.0).contains(&li), "lufs_i out of range: {li}");
    assert!((-20.0..3.0).contains(&tp), "max_true_peak out of range: {tp}");

    // FFI vs direct engine: 同一 f32→f64 サンプル列 → 同一 ebur128 → 一致（abs≈0）。
    let d_li = direct.lufs_i.expect("direct lufs_i");
    let d_tp = direct.max_true_peak.expect("direct max_true_peak");
    let abs_li = (li - d_li).abs();
    let abs_tp = (tp - d_tp).abs();
    eprintln!(
        "[session parity] lufs_i ffi={li:.10} direct={d_li:.10} abs={abs_li:.3e} | \
         max_tp ffi={tp:.10} direct={d_tp:.10} abs={abs_tp:.3e} | lra ffi={:?} direct={:?}",
        ffi.lra, direct.lra
    );
    // ebur128 integrated はチャンク非依存だが、加算順差で ~1e-12 が出得るため floor。
    assert!(abs_li < 1e-6, "lufs_i FFI vs direct diff too large: {abs_li}");
    assert!(abs_tp < 1e-6, "max_true_peak FFI vs direct diff too large: {abs_tp}");
    if let (Some(l), Some(dl)) = (ffi.lra, direct.lra) {
        assert!((l - dl).abs() < 1e-6, "lra FFI vs direct diff too large");
    }
}

/// license gate: Os 以外（Sense / Unknown）では enter_record=false・Record されず
/// poll_session は None（二重 gate / E-21 / record.rs:110）。
#[test]
fn license_gate_blocks_record_when_not_os() {
    for code in [1u8 /* Sense */, 2u8 /* Unknown */] {
        let engine = KirinHyphaEngine::new(SR, 2);
        engine.set_license(code);
        engine.set_signal_state(1);
        assert!(
            !engine.enter_record(),
            "license code {code}（非 Os）で enter_record は false であるべき"
        );
        assert!(!engine.is_recording(), "非 Os では Record されない（code {code}）");
        // Record されないので push しても finalize されず poll_session は None。
        let signal = gen_stereo_f32(0.5);
        engine.push_samples(&signal, 2);
        sleep(Duration::from_millis(250));
        assert!(
            engine.poll_session().is_none(),
            "非 Os では poll_session は None（code {code}）"
        );
    }
}

/// 既定（set_license 前）は Unknown 安全側で Record 不可。
#[test]
fn default_license_is_unknown_record_denied() {
    let engine = KirinHyphaEngine::new(SR, 2);
    engine.set_signal_state(1);
    assert!(!engine.enter_record(), "既定 license（Unknown）では enter_record false");
    assert!(!engine.is_recording());
}

/// E-21 降格保険: Os で Record 開始 → Sense へ降格すると強制 Watch（enforce_license）。
#[test]
fn license_demotion_forces_watch() {
    let engine = KirinHyphaEngine::new(SR, 2);
    engine.set_license(0); // Os
    assert!(engine.enter_record(), "Os で enter_record は true");
    assert!(engine.is_recording(), "Record 中");
    engine.set_license(1); // Sense へ降格 → enforce_license で強制 Watch
    assert!(
        !engine.is_recording(),
        "Os 以外への降格で Record が強制 Watch されること（E-21）"
    );
}
