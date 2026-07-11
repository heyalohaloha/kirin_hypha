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

use approx::{abs_diff_eq, assert_relative_eq, relative_eq};

use kirin_hypha_ffi::{ExpectedWavMetadataInput, KirinHyphaEngine};
use kirin_measure::engine::{MeasureEngine, SessionSummary};
use kirin_measure::phase_d::stream::{PhaseDResult, PhaseDStream};
use kirin_measure::phase_d::tables::FieldType;
use kirin_measure::record_take::{CaptureClockSource, RecordTakeBlock};
use kirin_measure::RING_BUFFER_SECONDS;

const SR: u32 = 48_000;

// MoSQITo tolerance（kirin_measure/Cargo.toml:26-28）
const N_MAX_REL: f64 = 1e-3; // N_spec relative
const SHARP_EPS: f64 = 0.05; // sharpness abs [acum]
const PSB_ABS_FLOOR: f64 = 1e-9; // 同一コード経路の数値ノイズ吸収用の微小 abs floor
const PHASE_D_PARITY_SECONDS: f64 = 6.0; // async FFI publication may lag one frame; use converged steady state
const PHASE_D_PARITY_BLOCK_SLEEP_MS: u64 = 110; // keep below the 2s ring cap even under parallel tests
const PHASE_D_PARITY_DIRECT_TAIL_CANDIDATES: usize = 16; // latest 0.1s publish candidates for async FFI

fn arm_expected_wav(engine: &KirinHyphaEngine, label: &str) {
    assert!(
        engine.set_expected_wav_metadata(ExpectedWavMetadataInput {
            bounce_id: format!("bounce-{label}-{}", std::process::id()),
            expected_duration_samples: SR as u64 * 4,
            expected_sample_rate: SR,
            wav_path: format!("/tmp/kirin-hypha-{label}.wav"),
            wav_file_size: 384_044,
            wav_mtime_ms: kirin_measure::record_writer::now_epoch_ms(),
            wav_hash: format!("hash-{label}-{}", std::process::id()),
        }),
        "expected WAV metadata must arm for {label}"
    );
}

fn wait_until_recording(engine: &KirinHyphaEngine, label: &str) {
    for _ in 0..30 {
        if engine.is_recording() {
            return;
        }
        engine.push_samples(&[], 2);
        sleep(Duration::from_millis(100));
    }
    panic!("engine did not enter Record after Keep/ACK barrier: {label}");
}

fn push_positioned_stereo(engine: &KirinHyphaEngine, samples: &[f32], position_samples: i64) {
    let num_frames = (samples.len() / 2) as u64;
    let recording = engine.is_recording();
    engine.note_record_block(RecordTakeBlock {
        generation: 0,
        recording,
        rendered: recording,
        playing: true,
        offline: false,
        position_valid: true,
        position_samples,
        num_frames,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    engine.note_capture_window(
        true,
        position_samples,
        num_frames,
        CaptureClockSource::ProjectTimeline,
    );
    engine.push_samples(samples, 2);
}

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

fn gen_mono_f32(seconds: f64) -> Vec<f32> {
    let n = (SR as f64 * seconds) as usize;
    let mut v = Vec::with_capacity(n);
    for i in 0..n {
        let t = i as f64 / SR as f64;
        v.push((0.2 * (2.0 * PI * 1000.0 * t).sin()) as f32);
    }
    v
}

fn duplicate_mono_to_stereo(mono: &[f32]) -> Vec<f32> {
    let mut v = Vec::with_capacity(mono.len() * 2);
    for &s in mono {
        v.push(s);
        v.push(s);
    }
    v
}

/// direct 参照: measure_thread の GUI publish 経路を再現。
///
/// Measure Thread は ring から取得した chunk ごとに `PhaseDStream::push()` し、その chunk の
/// 最後の PhaseD frame だけを 100ms 計測結果へ merge する。一括 push だと STFT hold 値の
/// chunk 境界が本番とずれるため、FFI と同じ 0.1s producer block 境界の publish 候補だけを作る。
fn direct_phase_d_publish_candidates(stereo_f32: &[f32]) -> Vec<PhaseDResult> {
    let mut stream = PhaseDStream::new(FieldType::Free);
    let block_len = (SR as usize / 10) * 2;
    let mut frames = Vec::new();
    for block in stereo_f32.chunks(block_len) {
        let mono: Vec<f64> = block
            .chunks_exact(2)
            .map(|c| (c[0] as f64 + c[1] as f64) * 0.5)
            .collect();
        if let Some(last) = stream.push(&mono).last() {
            frames.push(last.clone());
        }
    }
    assert!(
        !frames.is_empty(),
        "PhaseDStream should emit at least one publish candidate for multi-second input"
    );
    frames
}

fn direct_last_lufs(samples: &[f32], channels: usize) -> f64 {
    let mut engine = MeasureEngine::new(SR, channels).expect("MeasureEngine init");
    let f64buf: Vec<f64> = samples.iter().map(|&s| s as f64).collect();
    let chunk = (SR as usize / 10) * channels;
    let mut last = None;
    for c in f64buf.chunks(chunk) {
        if let Some(r) = engine.push(c) {
            if let Some(lufs) = r.lufs_m {
                last = Some(lufs);
            }
        }
    }
    last.expect("direct engine should produce LUFS-M")
}

fn drive_ffi_lufs(samples: &[f32], channels: u32) -> f64 {
    let engine = KirinHyphaEngine::new(SR, channels);
    engine.set_signal_state(1); // Active (ABI code)

    let block_frames = SR as usize / 10;
    let block_len = block_frames * channels as usize;
    let mut i = 0;
    while i < samples.len() {
        let end = (i + block_len).min(samples.len());
        engine.push_samples(&samples[i..end], channels);
        i = end;
        sleep(Duration::from_millis(30));
    }

    let mut last = None;
    for _ in 0..40 {
        engine.push_samples(&[], channels);
        sleep(Duration::from_millis(50));
        if let Some(r) = engine.poll_result() {
            if let Some(lufs) = r.lufs_m {
                last = Some(lufs);
            }
        }
    }
    last.expect("FFI should produce LUFS-M")
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

fn phase_d_result_matches_direct_tail(
    res: &kirin_measure::MeasureResult,
    direct: &PhaseDResult,
) -> bool {
    let Some(n_ffi) = res.n_prime_total else {
        return false;
    };
    if !relative_eq!(
        n_ffi,
        direct.loudness,
        max_relative = N_MAX_REL,
        epsilon = PSB_ABS_FLOOR
    ) {
        return false;
    }

    let Some(sh_ffi) = res.sharpness else {
        return false;
    };
    if !abs_diff_eq!(sh_ffi, direct.sharpness, epsilon = SHARP_EPS) {
        return false;
    }

    let Some(np) = res.n_prime else {
        return false;
    };
    if !np
        .iter()
        .zip(direct.n_prime.iter())
        .all(|(&a, &b)| relative_eq!(a, b, max_relative = N_MAX_REL, epsilon = PSB_ABS_FLOOR))
    {
        return false;
    }

    let Some(pb) = res.psb_bark else {
        return false;
    };
    if !pb
        .iter()
        .zip(direct.psb.iter())
        .all(|(&a, &b)| relative_eq!(a, b, max_relative = N_MAX_REL, epsilon = PSB_ABS_FLOOR))
    {
        return false;
    }

    let Some(summary) = res.psb_summary.as_ref() else {
        return false;
    };
    let (ref_low, ref_mid, ref_high) = psb_summary_ref(
        &direct.psb,
        &direct.psb_bark21_24,
        direct.psb_high_ext_15_5k_20k,
    );
    relative_eq!(
        summary.low,
        ref_low,
        max_relative = N_MAX_REL,
        epsilon = 1e-6
    ) && relative_eq!(
        summary.mid,
        ref_mid,
        max_relative = N_MAX_REL,
        epsilon = 1e-6
    ) && relative_eq!(
        summary.high,
        ref_high,
        max_relative = N_MAX_REL,
        epsilon = 1e-6
    )
}

fn select_direct_tail_frame<'a>(
    res: &kirin_measure::MeasureResult,
    direct_frames: &'a [PhaseDResult],
) -> Option<(usize, &'a PhaseDResult)> {
    direct_frames
        .iter()
        .enumerate()
        .rev()
        .take(PHASE_D_PARITY_DIRECT_TAIL_CANDIDATES)
        .find(|(_, direct)| phase_d_result_matches_direct_tail(res, direct))
}

/// FFI を駆動して、drain 後に direct publish 候補と一致する phase_d publish を取得する。
fn drive_ffi(
    stereo_f32: &[f32],
    direct_candidates: &[PhaseDResult],
) -> (kirin_measure::MeasureResult, usize, u64) {
    let engine = KirinHyphaEngine::new(SR, 2);
    engine.set_signal_state(1); // Active (ABI code)

    // 0.1s ブロックを実時間より少し遅く投入する。workspace test の並列実行中でも
    // 2s ring を超えず、Phase D steady-state parity のサンプル列を落とさない。
    let block_frames = SR as usize / 10; // 0.1s
    let block_len = block_frames * 2; // stereo
    let mut i = 0;
    while i < stereo_f32.len() {
        let end = (i + block_len).min(stereo_f32.len());
        engine.push_samples(&stereo_f32[i..end], 2);
        i = end;
        sleep(Duration::from_millis(PHASE_D_PARITY_BLOCK_SLEEP_MS));
    }

    // keepalive（heartbeat を進めて Active 維持）しつつ、ring 全消費後の publish を読む。
    // 値のプラトーではなく ring の実 drain を barrier にする（session parity と同じ考え方）。
    let mut last_complete: Option<kirin_measure::MeasureResult> = None;
    let mut drained_streak = 0u32;
    for _ in 0..240 {
        engine.push_samples(&[], 2); // 0-frame keepalive
        sleep(Duration::from_millis(50));
        if engine.__ring_drained_for_test() && engine.overflow_count() == 0 {
            drained_streak += 1;
            if drained_streak >= 3 {
                if let Some(r) = engine.poll_result() {
                    if r.n_prime_total.is_some()
                        && r.sharpness.is_some()
                        && r.psb_summary.is_some()
                        && r.n_prime.is_some()
                        && r.psb_bark.is_some()
                    {
                        if let Some((direct_index, _)) =
                            select_direct_tail_frame(&r, direct_candidates)
                        {
                            let overflow = engine.overflow_count();
                            return (r, direct_index, overflow);
                        }
                        last_complete = Some(r);
                    }
                }
            }
        } else {
            drained_streak = 0;
        }
    }
    let overflow = engine.overflow_count();
    let final_frame = direct_candidates
        .last()
        .expect("direct PhaseDStream should emit a final publish candidate");
    let (_, _, final_high) = psb_summary_ref(
        &final_frame.psb,
        &final_frame.psb_bark21_24,
        final_frame.psb_high_ext_15_5k_20k,
    );
    panic!(
        "FFI should publish a fully-populated MeasureResult matching the direct publish-candidate tail after ring drain \
         (direct_candidates={}, tail={}, overflow={}, last_ffi_high={:?}, final_direct_high={final_high})",
        direct_candidates.len(),
        PHASE_D_PARITY_DIRECT_TAIL_CANDIDATES,
        overflow,
        last_complete
            .as_ref()
            .and_then(|r| r.psb_summary.as_ref().map(|s| s.high))
    );
}

#[test]
fn parity_phase_d_metrics_ffi_vs_direct() {
    let signal = gen_stereo_f32(PHASE_D_PARITY_SECONDS);

    // direct 参照。FFI 側は ring-drain barrier 後の最新 publish を読むが、publish は
    // MeasureEngine の 100ms 結果に Phase D の chunk-last frame を merge する cadenced snapshot。
    // 並列 workspace test では最後の publish 候補ではなく直近候補が残ることがあるため、
    // 同一サンプル列・同一0.1s producer block境界の tail 候補から strict tolerance で一致を選ぶ。
    let direct_candidates = direct_phase_d_publish_candidates(&signal);

    // FFI 経由
    let (res, direct_index, overflow) = drive_ffi(&signal, &direct_candidates);

    // パリティ駆動でサンプルが落ちていない（= 同一サンプル列を処理した）ことを確認。
    assert_eq!(
        overflow, 0,
        "parity push dropped samples (ring overflow={overflow})"
    );

    let direct = &direct_candidates[direct_index];
    assert!(
        direct_index + PHASE_D_PARITY_DIRECT_TAIL_CANDIDATES >= direct_candidates.len(),
        "selected direct publish candidate must come from the tail window"
    );
    let (ref_low, ref_mid, ref_high) = psb_summary_ref(
        &direct.psb,
        &direct.psb_bark21_24,
        direct.psb_high_ext_15_5k_20k,
    );

    // ── Zwicker N (n_prime_total) : MoSQITo N tolerance ──
    let n_ffi = res.n_prime_total.unwrap();
    assert_relative_eq!(
        n_ffi,
        direct.loudness,
        max_relative = N_MAX_REL,
        epsilon = PSB_ABS_FLOOR
    );

    // ── Sharpness : MoSQITo sharpness tolerance ──
    let sh_ffi = res.sharpness.unwrap();
    approx::assert_abs_diff_eq!(sh_ffi, direct.sharpness, epsilon = SHARP_EPS);

    // ── n_prime[20] : MoSQITo N tolerance（band 毎）──
    let np = res.n_prime.unwrap();
    for (k, &np_k) in np.iter().enumerate() {
        assert_relative_eq!(
            np_k,
            direct.n_prime[k],
            max_relative = N_MAX_REL,
            epsilon = PSB_ABS_FLOOR
        );
    }

    // ── psb_bark[20] (= PhaseDResult.psb) : 同一コード経路なので tight ──
    let pb = res.psb_bark.unwrap();
    for (k, &pb_k) in pb.iter().enumerate() {
        assert_relative_eq!(
            pb_k,
            direct.psb[k],
            max_relative = N_MAX_REL,
            epsilon = PSB_ABS_FLOOR
        );
    }

    // ── PSB low/mid/high : compute_psb_summary 再現と一致 ──
    let s = res.psb_summary.unwrap();
    assert_relative_eq!(s.low, ref_low, max_relative = N_MAX_REL, epsilon = 1e-6);
    assert_relative_eq!(s.mid, ref_mid, max_relative = N_MAX_REL, epsilon = 1e-6);
    assert_relative_eq!(s.high, ref_high, max_relative = N_MAX_REL, epsilon = 1e-6);
}

#[test]
fn mono_ffi_is_one_channel_not_dual_mono_plus_3db() {
    let mono = gen_mono_f32(2.0);
    let dual_mono = duplicate_mono_to_stereo(&mono);

    let direct_mono = direct_last_lufs(&mono, 1);
    let direct_dual_mono = direct_last_lufs(&dual_mono, 2);
    let ffi_mono = drive_ffi_lufs(&mono, 1);

    approx::assert_abs_diff_eq!(ffi_mono, direct_mono, epsilon = 0.2);

    let dual_mono_bias = direct_dual_mono - direct_mono;
    approx::assert_abs_diff_eq!(dual_mono_bias, 3.0103, epsilon = 0.2);
    assert!(
        (direct_dual_mono - ffi_mono) > 2.5,
        "mono FFI must not be measured as dual-mono stereo (+3 dB bias)"
    );
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
    assert!(
        engine.enter_record(),
        "Os + Watch なら enter_record は true"
    );
    assert!(engine.is_recording(), "enter_record 成功後は Record 中");

    // Measure Thread が is_recording を観測して engine.reset() するのを ring 空のまま待つ。
    // 待機中も keepalive で heartbeat を進め signal_state Active を維持する
    // （無音待機すると heartbeat stall override で Inactive に落ち計測がスキップされる
    //   / measure_thread.rs:160-169。実プラグインは毎ブロック set_signal_state する）。
    for _ in 0..6 {
        engine.push_samples(&[], 2); // 0-frame keepalive（ring 空のまま heartbeat++）
        sleep(Duration::from_millis(40));
    }

    // B-129 (G-115-372): consumer-paced（slower-than-realtime）駆動で ring overflow を決定化する。
    // 旧: realtime（100ms/0.1s block）は Record 中の重い finalize 消費とギリギリで、machine load 下で
    // consumer が追いつかず overflow → flaky（trust anchor が溶ける）。
    // 新: 各 block 投入後に realtime の DRAIN_MARGIN 倍の猶予を keepalive 付きで与え（heartbeat/Active
    // 維持）、consumer が ring を確実に drain しきってから次 block を押す。ring は常に near-empty となり
    // overflow を構造的に 0 化（最大 DRAIN_MARGIN×realtime の consumer slowdown まで吸収・2s ring slack 併用）。
    // 投入する音声 sample は不変（keepalive は 0-frame）ゆえ finalize 一致＝parity abs=0/rel=0 は不変。
    // 本番 finalize ロジック / engine.rs / FFI 数値サーフェスは一切変えない（test 駆動の決定化のみ）。
    const DRAIN_MARGIN: usize = 3;
    let block_frames = SR as usize / 10;
    let block_len = block_frames * 2;
    let block_dt = Duration::from_secs_f64(block_frames as f64 / SR as f64); // 0.1s
    let mut i = 0;
    while i < stereo_f32.len() {
        let end = (i + block_len).min(stereo_f32.len());
        engine.push_samples(&stereo_f32[i..end], 2);
        i = end;
        for _ in 0..DRAIN_MARGIN {
            engine.push_samples(&[], 2); // 0-frame keepalive（heartbeat / Active 維持・ring 投入なし）
            sleep(block_dt);
        }
    }

    // drain 直接確認（B-129 reopen / G-115-380）: lufs_i の値プラトーで finalize タイミングを決めない。
    // 旧版は「lufs_i が 2 回連続一致 = ring 全消費」と仮定したが、定常信号では integrated LUFS が ring
    // 未空でも早期にプラトーするため、FFI が direct より少ないサンプル集合で finalize し 1e-7〜1e-5 乖離する
    // flaky の原因だった。新版は ring が実際に空であること（__ring_drained_for_test = producer.slots()==capacity
    // ⟺ consumer が全 pop 済）かつ overflow_count()==0（push 失敗 0 ⟹ 投入は全て ring 経由）で、
    // 「push 済み全サンプルを measure thread が pop→engine.push()→finalize() 済」を直接確認する。
    // ring 空を 3 回連続（3×50ms=150ms > measure LOOP_SLEEP=100ms）観測してから poll_session を読み、
    // 最後の実 pop と同一ループ内の finalize→session_summary 書込が完了した後の値を取る。0-frame keepalive は
    // ring に投入しない（is_recording/heartbeat 維持のみ）ので、一度空になった ring は空のまま保たれ、
    // session_summary は最後の実 pop 後の finalize 値で安定する（値比較なしで決定的）。
    let mut drained_streak = 0u32;
    let mut stable: Option<SessionSummary> = None;
    for _ in 0..240 {
        engine.push_samples(&[], 2); // keepalive（is_recording 維持・heartbeat / ring 投入なし）
        sleep(Duration::from_millis(50));
        if engine.__ring_drained_for_test() && engine.overflow_count() == 0 {
            drained_streak += 1;
            if drained_streak >= 3 {
                if let Some(s) = engine.poll_session() {
                    stable = Some(s);
                    break;
                }
            }
        } else {
            drained_streak = 0; // ring に未消費サンプル / push 失敗あり → 全消費ではない
        }
    }
    let overflow = engine.overflow_count();
    let was_recording = engine.is_recording();
    engine.exit_record(); // Watch へ。直近 finalize 値は保持される。
    (
        // STOP 条項（B-132）: ring 全消費を確認できない場合は value 比較で誤魔化さず、ここで panic させ
        // test を FAIL させる。production の (b) drain 不全（ring が空にならない / 消費数が合わない）を
        // 踏んだ証跡として報告する（test を緩めて production 欠陥を隠さない）。
        stable.expect(
            "ring 全消費（__ring_drained_for_test）+ overflow==0 を確認後に poll_session が Some を返すこと\
             （未達なら production drain 不全 = B-132 の証跡。test を緩めて隠さない）",
        ),
        overflow,
        was_recording,
    )
}

/// FFI の poll_session（Record finalize）が、同一サンプルを直接 MeasureEngine に通した
/// finalize と一致することを示す（殻が精度を変えない / abs≈0）。
/// realtime 駆動で遅いため既定 `cargo test` から除外（`--ignored` で実行）。
#[test]
#[ignore = "slow: realtime ~12s record drive"]
fn session_finalize_ffi_matches_direct_engine() {
    // 12 秒（loudness_global の最小窓 ~10s 超）の定常マルチトーン（L==R）。
    let signal = gen_stereo_f32(12.0);

    let (ffi, overflow, was_recording) = drive_ffi_session(&signal);
    assert_eq!(
        overflow, 0,
        "ring overflow: FFI が全サンプルを処理していない (of={overflow})"
    );
    assert!(was_recording, "exit 直前まで Record 中であること");
    let direct = direct_session(&signal);

    // 値域の妥当性（lra_plr_session_test と同基準の広め）。
    let li = ffi.lufs_i.expect("lufs_i must be Some after 12s");
    let tp = ffi.max_true_peak.expect("max_true_peak must be Some");
    assert!((-30.0..0.0).contains(&li), "lufs_i out of range: {li}");
    assert!(
        (-20.0..3.0).contains(&tp),
        "max_true_peak out of range: {tp}"
    );

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
    assert!(
        abs_li < 1e-6,
        "lufs_i FFI vs direct diff too large: {abs_li}"
    );
    assert!(
        abs_tp < 1e-6,
        "max_true_peak FFI vs direct diff too large: {abs_tp}"
    );
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
        assert!(
            !engine.is_recording(),
            "非 Os では Record されない（code {code}）"
        );
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
    assert!(
        !engine.enter_record(),
        "既定 license（Unknown）では enter_record false"
    );
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

// ── Phase 3b: enable_pre_writes → io_thread_pre が plugin_data を書く ──────────

/// `parent_name/*.{suffix}` を再帰探索し最初の 1 件を返す（parent dir 名で絞る）。
fn find_json_under(
    root: &std::path::Path,
    parent_name: &str,
    file_name: &str,
) -> Option<std::path::PathBuf> {
    let rd = std::fs::read_dir(root).ok()?;
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() {
            if let Some(found) = find_json_under(&p, parent_name, file_name) {
                return Some(found);
            }
        } else {
            let fname = p.file_name().and_then(|n| n.to_str());
            let pname = p
                .parent()
                .and_then(|pp| pp.file_name())
                .and_then(|n| n.to_str());
            let name_ok = if file_name == "*.json" {
                fname.map(|n| n.ends_with(".json")).unwrap_or(false)
            } else {
                fname == Some(file_name)
            };
            if name_ok && (parent_name.is_empty() || pname == Some(parent_name)) {
                return Some(p);
            }
        }
    }
    None
}

fn find_failed_json_under_role(
    root: &std::path::Path,
    role_name: &str,
) -> Option<std::path::PathBuf> {
    let rd = std::fs::read_dir(root).ok()?;
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() {
            if let Some(found) = find_failed_json_under_role(&p, role_name) {
                return Some(found);
            }
        } else {
            let fname = p.file_name().and_then(|n| n.to_str());
            let parent = p.parent();
            let parent_name = parent
                .and_then(|pp| pp.file_name())
                .and_then(|n| n.to_str());
            let grandparent_name = parent
                .and_then(|pp| pp.parent())
                .and_then(|pp| pp.file_name())
                .and_then(|n| n.to_str());
            if fname.map(|n| n.ends_with(".json")).unwrap_or(false)
                && parent_name == Some(".failed")
                && grandparent_name == Some(role_name)
            {
                return Some(p);
            }
        }
    }
    None
}

fn pair_member_json_from_manifest(
    plugin_data_root: &std::path::Path,
    role_name: &str,
) -> std::path::PathBuf {
    let manifest = find_json_under(plugin_data_root, "record_sessions", "*.json")
        .expect("PairRecordSession manifest exists");
    let value: serde_json::Value =
        serde_json::from_str(&std::fs::read_to_string(&manifest).unwrap()).unwrap();
    assert_eq!(value["schema_version"], "pair_record_session.v1");
    let side = &value[role_name];
    assert_eq!(
        side["role"].as_str().unwrap(),
        role_name.to_ascii_uppercase()
    );
    let path = side["path"]
        .as_str()
        .expect("manifest side path")
        .to_string();
    assert!(
        path.contains(".pair_committed"),
        "PairRecordSession manifest must point to the durable hidden member: {path}"
    );
    let path = std::path::PathBuf::from(path);
    assert!(
        path.exists(),
        "PairRecordSession manifest points to an existing member: {}",
        path.display()
    );
    path
}

/// transactionless な直接 Record は plugin_data 棚を作らず、Watch `pre.json` と
/// measurement aggregate だけを維持することを検証する。
/// HOME/TMPDIR を temp に差し替えて分離（io_thread spawn 前に設定し、テスト中変更しない）。
/// realtime 駆動で遅いため既定 `cargo test` から除外（`--ignored` で実行）。
#[test]
#[ignore = "slow: realtime ~12s record + io_thread filesystem writes (sets HOME/TMPDIR)"]
fn pre_direct_record_without_pair_does_not_write_plugin_data_json() {
    // 分離: HOME(plugin_data root) と TMPDIR(Watch pre.json root) を temp へ。
    let test_root = std::env::temp_dir()
        .join("kirin_b057_test")
        .join(format!("pid{}", std::process::id()));
    let home = test_root.join("home");
    let tmp = test_root.join("tmp");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    // io_thread spawn 前に設定（default_macos/temp_dir は呼出毎に env を読む）。
    std::env::set_var("HOME", &home);
    std::env::set_var("TMPDIR", &tmp);
    // B-106: role-scoped 共有セルを reset してテスト間 first-wins 汚染を排除する（旧 overwrite
    // 隔離の代替）。各テストは自分の set_identity project_uuid を first-wins で seed できる。
    kirin_hypha_ffi::__reset_shared_ids_for_tests();

    // identity.json fixture: writer_start が load_installation_id_safe で
    // installation_id を要求する（record_writer.rs:191）。実機は Kirin OS が作る。
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"b057-test-install","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-08T00:00:00Z","last_verified_at":"2026-06-08T00:00:00Z"}"#,
    )
    .unwrap();

    let plugin_data_root = home.join("Library/Application Support/Kirin OS/plugin_data");
    let watch_root = tmp.join("kirin");

    let aggregates;
    let watch_pre_exists;
    {
        let engine = KirinHyphaEngine::new(SR, 2);
        engine.set_license(0); // Os（enable より前）
        engine.enable_pre_writes(); // io_thread_pre 起動
        engine.set_signal_state(1); // Active
        assert!(engine.enter_record(), "Os で enter_record true");

        // reset 待ち keepalive（heartbeat 維持で Active を保つ）。
        for _ in 0..6 {
            engine.push_samples(&[], 2);
            sleep(Duration::from_millis(40));
        }

        // realtime push 12s（loudness_global 最小窓 > 10s / overflow 回避）。
        let signal = gen_stereo_f32(12.0);
        let block_frames = SR as usize / 10;
        let block_len = block_frames * 2;
        let block_dt = Duration::from_secs_f64(block_frames as f64 / SR as f64);
        let mut i = 0;
        while i < signal.len() {
            let end = (i + block_len).min(signal.len());
            engine.push_samples(&signal[i..end], 2);
            i = end;
            sleep(block_dt);
        }

        // session 安定化（全サンプル finalize 済）。
        let mut prev: Option<f64> = None;
        for _ in 0..120 {
            engine.push_samples(&[], 2);
            sleep(Duration::from_millis(50));
            if let Some(s) = engine.poll_session() {
                if s.lufs_i.is_some() && s.lufs_i == prev {
                    break;
                }
                prev = s.lufs_i;
            }
        }
        aggregates = engine
            .poll_session()
            .expect("poll_session Some after record");

        // Watch pre.json は engine 生存中に存在（Drop で削除される前に確認）。
        watch_pre_exists = find_json_under(&watch_root, "", "pre.json").is_some();

        // exit → io_thread が Record→Watch を検出し writer_close(status=closed) するのを待つ。
        engine.exit_record();
        sleep(Duration::from_millis(900));

        assert!(
            find_json_under(&plugin_data_root, "pre", "*.json").is_none(),
            "PairRecordSession が無い単独 PRE は通常棚へ publish しない"
        );
        assert!(
            find_failed_json_under_role(&plugin_data_root, "pre").is_none(),
            "PairRecordSession が無い単独 PRE は failed 診断棚も作らない"
        );
        assert!(
            aggregates.lufs_i.is_some(),
            "transactionless direct Record でも実測 aggregate は poll_session で得られる"
        );
    } // engine Drop → io_thread shutdown→join（Watch pre.json/instance dir 後始末）。

    assert!(watch_pre_exists, "Watch pre.json が稼働中に書かれること");

    // 後始末（panic 経路でも temp を残さない）。
    let _ = std::fs::remove_dir_all(&test_root);
}

// ── Phase 3c: state chunk identity get/set + annotation ──────────────────────

/// C 文字列バッファ（null 終端）を Rust String へ。
fn cbuf_to_string(buf: &[std::os::raw::c_char]) -> String {
    let bytes: Vec<u8> = buf
        .iter()
        .take_while(|&&c| c != 0)
        .map(|&c| c as u8)
        .collect();
    String::from_utf8_lossy(&bytes).into_owned()
}

/// C ABI set_identity → get_identity の 4 キー往復 + 別エンジン同一値の決定性
/// （= 同一 chunk 復元で同一識別子 = 同一 plugin_data path の基礎）。
#[test]
fn identity_round_trip_via_c_abi() {
    use kirin_hypha_ffi::{kirin_hypha_get_identity, kirin_hypha_set_identity, KirinIdentity};
    use std::ffi::CString;

    let read_back = |iid: &str, puid: &str, dsid: &str, nm: &str| {
        let mut engine = KirinHyphaEngine::new(SR, 2);
        let ptr = &mut engine as *mut KirinHyphaEngine;
        let (c_iid, c_puid, c_dsid, c_nm) = (
            CString::new(iid).unwrap(),
            CString::new(puid).unwrap(),
            CString::new(dsid).unwrap(),
            CString::new(nm).unwrap(),
        );
        unsafe {
            kirin_hypha_set_identity(
                ptr,
                c_iid.as_ptr(),
                c_puid.as_ptr(),
                c_dsid.as_ptr(),
                c_nm.as_ptr(),
            );
        }
        let mut out: KirinIdentity = unsafe { std::mem::zeroed() };
        unsafe { kirin_hypha_get_identity(ptr, &mut out) };
        (
            cbuf_to_string(&out.instance_id),
            cbuf_to_string(&out.project_uuid),
            cbuf_to_string(&out.daw_session_uuid),
            cbuf_to_string(&out.name),
        )
    };

    let got = read_back("iid-x", "puid-y", "dsid-z", "mix1");
    assert_eq!(
        got,
        (
            "iid-x".to_string(),
            "puid-y".to_string(),
            "dsid-z".to_string(),
            "mix1".to_string()
        )
    );
    // 別エンジンに同一 set_identity → 同一 get_identity（決定性）。
    assert_eq!(got, read_back("iid-x", "puid-y", "dsid-z", "mix1"));
}

/// B-128 (G-115-371 / D1+D2+D3): set_identity が path-unsafe な restore 値を materialize し、
/// self.identity（get_identity 往復）が fresh new_v4 になる（raw 第二源なし=分裂消滅 D1）。
/// その値で reservation は counted（quarantine でない=uncounted bypass 消滅 D2）。
/// invalid-identity event が surface される（D3）。
#[test]
fn set_identity_materializes_unsafe_restore_value() {
    use kirin_hypha_ffi::{kirin_hypha_get_identity, kirin_hypha_set_identity, KirinIdentity};
    use kirin_measure::path_identity::drain_path_events;
    use kirin_measure::reservation::{count_frames, reserve_pairing, ReserveOutcome};
    use std::ffi::CString;

    let _ = drain_path_events(); // sink clean

    let mut engine = KirinHyphaEngine::new(SR, 2);
    let ptr = &mut engine as *mut KirinHyphaEngine;
    let (c_iid, c_puid, c_dsid, c_nm) = (
        CString::new("/tmp/x").unwrap(),          // 絶対パス（§7 i）
        CString::new("../../../../etc").unwrap(), // ../traversal（§7 ii）
        CString::new("").unwrap(),
        CString::new("name").unwrap(),
    );
    unsafe {
        kirin_hypha_set_identity(
            ptr,
            c_iid.as_ptr(),
            c_puid.as_ptr(),
            c_dsid.as_ptr(),
            c_nm.as_ptr(),
        );
    }
    let mut out: KirinIdentity = unsafe { std::mem::zeroed() };
    unsafe { kirin_hypha_get_identity(ptr, &mut out) };
    let iid = cbuf_to_string(&out.instance_id);
    let puid = cbuf_to_string(&out.project_uuid);

    // D1: self.identity が materialize 済 new_v4（raw "/tmp/x" は残らない＝第二源なし）。
    assert_ne!(
        iid, "/tmp/x",
        "unsafe instance_id は materialize される（raw 残存なし）"
    );
    assert_ne!(puid, "../../../../etc");
    assert!(
        uuid::Uuid::parse_str(&iid).is_ok(),
        "instance_id は fresh new_v4: {iid}"
    );
    assert!(
        uuid::Uuid::parse_str(&puid).is_ok(),
        "project_uuid は fresh new_v4: {puid}"
    );

    // D1（単一 source 強化）: get_identity を再取得しても同一値 = materialize 済値が self.identity に
    // **1 度だけ**格納される単一 source（per-read 再 materialize で new_v4 が毎回変わる「第二系統」がない）。
    // keep / record / 永続化 / io_thread は全てこの self.identity（= get_identity が返す値）を読む。
    let mut out2: KirinIdentity = unsafe { std::mem::zeroed() };
    unsafe { kirin_hypha_get_identity(ptr, &mut out2) };
    assert_eq!(
        cbuf_to_string(&out2.instance_id),
        iid,
        "再取得で同一＝単一格納 source（分裂なし）"
    );
    assert_eq!(cbuf_to_string(&out2.project_uuid), puid);

    // D3: invalid-identity event surface（silent swap 禁止）。
    assert!(
        !drain_path_events().is_empty(),
        "invalid 注入で event surface"
    );

    // D2: materialize 済 new_v4 値での reservation は counted（quarantine `_q_` でない）。
    let base = std::env::temp_dir().join(format!("kirin_d2_{}", std::process::id()));
    let _ = std::fs::remove_dir_all(&base);
    std::fs::create_dir_all(&base).unwrap();
    assert!(matches!(
        reserve_pairing(&base, &puid, &iid, &iid).unwrap(),
        ReserveOutcome::Created
    ));
    assert_eq!(
        count_frames(&base, &puid),
        1,
        "materialize 済 new_v4 reservation は counted（uncounted bypass なし=D2）"
    );
    let _ = std::fs::remove_dir_all(&base);
}

/// add_annotation は Os 以外（既定 Unknown / Sense）で false（二重 gate / can_write_plugin_data）。
#[test]
fn add_annotation_denied_without_os() {
    let engine = KirinHyphaEngine::new(SR, 2);
    assert!(
        !engine.add_annotation("x".to_string()),
        "既定 Unknown では false"
    );
    engine.set_license(1); // Sense
    assert!(!engine.add_annotation("x".to_string()), "Sense でも false");
}

/// set_identity の project_uuid/instance_id が Watch path を決める（復元再現）こと、
/// transactionless direct Record が plugin_data 棚を作らず annotation 対象にもならないこと。
#[test]
#[ignore = "slow: realtime ~12s record + io_thread filesystem writes (sets HOME/TMPDIR)"]
fn set_identity_direct_record_does_not_create_annotation_target() {
    let test_root = std::env::temp_dir()
        .join("kirin_b058_test")
        .join(format!("pid{}", std::process::id()));
    let home = test_root.join("home");
    let tmp = test_root.join("tmp");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    std::env::set_var("HOME", &home);
    std::env::set_var("TMPDIR", &tmp);
    // B-106: role-scoped 共有セルを reset してテスト間 first-wins 汚染を排除する（旧 overwrite
    // 隔離の代替）。各テストは自分の set_identity project_uuid を first-wins で seed できる。
    kirin_hypha_ffi::__reset_shared_ids_for_tests();
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"b058-test","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-08T00:00:00Z","last_verified_at":"2026-06-08T00:00:00Z"}"#,
    )
    .unwrap();

    let plugin_data_root = home.join("Library/Application Support/Kirin OS/plugin_data");
    let watch_root = tmp.join("kirin");
    let known_iid = "iid-b058-fixed";
    let known_puid = "puid-b058-fixed";

    {
        let engine = KirinHyphaEngine::new(SR, 2);
        engine.set_license(0); // Os
        engine.set_identity(
            known_iid.to_string(),
            known_puid.to_string(),
            "dsid-b058".to_string(),
            "mix-b058".to_string(),
        );
        engine.enable_pre_writes();
        engine.set_signal_state(1);
        assert!(engine.enter_record());
        for _ in 0..6 {
            engine.push_samples(&[], 2);
            sleep(Duration::from_millis(40));
        }
        let signal = gen_stereo_f32(12.0);
        let bf = SR as usize / 10;
        let bl = bf * 2;
        let dt = Duration::from_secs_f64(bf as f64 / SR as f64);
        let mut i = 0;
        while i < signal.len() {
            let e = (i + bl).min(signal.len());
            engine.push_samples(&signal[i..e], 2);
            i = e;
            sleep(dt);
        }
        let mut prev: Option<f64> = None;
        for _ in 0..120 {
            engine.push_samples(&[], 2);
            sleep(Duration::from_millis(50));
            if let Some(s) = engine.poll_session() {
                if s.lufs_i.is_some() && s.lufs_i == prev {
                    break;
                }
                prev = s.lufs_i;
            }
        }

        let watch = find_json_under(&watch_root, "", "pre.json").expect("watch pre.json exists");
        let watch_s = watch.to_string_lossy().to_string();
        assert!(
            watch_s.contains(known_puid),
            "Watch path に set した project_uuid を使う: {watch_s}"
        );
        assert!(
            watch_s.contains(known_iid),
            "Watch path に set した instance_id を使う: {watch_s}"
        );

        engine.exit_record();
        sleep(Duration::from_millis(900));

        assert!(
            find_json_under(&plugin_data_root, "pre", "*.json").is_none(),
            "PairRecordSession が無い単独 PRE は通常棚へ publish しない"
        );
        assert!(
            find_failed_json_under_role(&plugin_data_root, "pre").is_none(),
            "PairRecordSession が無い単独 PRE は failed 診断棚も作らない"
        );
        // PairRecordSession 不足で Record shelf が存在しないため annotation 対象にしない。
        assert!(
            !engine.add_annotation("note-A".to_string()),
            "transactionless direct Record への add_annotation は false"
        );

        // gate: Sense へ降格 → add_annotation false・annotations 不変。
        engine.set_license(1);
        assert!(
            !engine.add_annotation("note-B".to_string()),
            "Sense の add_annotation は false"
        );
    }

    let _ = std::fs::remove_dir_all(&test_root);
}

// ── Phase 3d-a: POST ランタイム（enable_post_writes）+ Δ（PRE 同居 end-to-end）────

/// 単一 FFI プロセスに PRE engine + POST engine を同居させ、POST が PRE を
/// `select_target_pre`（B-059 厳格）で見つけ Δ を算出することを実証する。
/// PRE をフル振幅、POST を半振幅(-6dB)で駆動 → Δ_lufs ≈ -6（POST − PRE）。
/// 併せて project_uuid_cell 共有が選定/path に影響しないこと（cross-uuid flatten）を確認。
#[test]
#[ignore = "slow: PRE+POST co-located realtime + io_thread filesystem (sets HOME/TMPDIR)"]
fn post_writes_delta_against_colocated_pre() {
    use kirin_measure::{DeltaMode, DeltaResult};

    // 分離: HOME(plugin_data) + TMPDIR(Watch pre/post.json root)。
    let test_root = std::env::temp_dir()
        .join("kirin_b060_test")
        .join(format!("pid{}", std::process::id()));
    let home = test_root.join("home");
    let tmp = test_root.join("tmp");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    std::env::set_var("HOME", &home);
    std::env::set_var("TMPDIR", &tmp);
    // B-106: role-scoped 共有セルを reset してテスト間 first-wins 汚染を排除する（旧 overwrite
    // 隔離の代替）。各テストは自分の set_identity project_uuid を first-wins で seed できる。
    kirin_hypha_ffi::__reset_shared_ids_for_tests();
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"b060-test","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#,
    )
    .unwrap();

    let watch_root = tmp.join("kirin");
    let delta: DeltaResult;
    let post_pre_state_found: bool;
    {
        // PRE engine: 同名 "mix"、別 project_uuid "puid-pre"。Watch（録音不要）。
        let pre = KirinHyphaEngine::new(SR, 2);
        pre.set_license(0);
        pre.set_identity("iid-pre".into(), "puid-pre".into(), "".into(), "mix".into());
        pre.enable_pre_writes();
        pre.set_signal_state(1); // Active

        // POST engine: 同名 "mix"（= 対 PRE 名）、別 project_uuid "puid-post"。
        let post = KirinHyphaEngine::new(SR, 2);
        post.set_license(0);
        post.set_identity(
            "iid-post".into(),
            "puid-post".into(),
            "".into(),
            "mix".into(),
        );
        post.enable_post_writes();
        post.set_signal_state(1); // Active

        // reset/observe 待ち keepalive（両 io_thread が is_recording=false=Watch で起動）。
        for _ in 0..6 {
            pre.push_samples(&[], 2);
            post.push_samples(&[], 2);
            sleep(Duration::from_millis(40));
        }

        // realtime ~3s: PRE フル振幅 / POST 半振幅(-6dB)。lufs_m は momentary(400ms) で速く収束。
        let pre_sig = gen_stereo_f32(3.0);
        let post_sig: Vec<f32> = pre_sig.iter().map(|&s| s * 0.5).collect();
        let bf = SR as usize / 10;
        let bl = bf * 2;
        let dt = Duration::from_secs_f64(bf as f64 / SR as f64);
        let mut i = 0;
        while i < pre_sig.len() {
            let e = (i + bl).min(pre_sig.len());
            pre.push_samples(&pre_sig[i..e], 2);
            post.push_samples(&post_sig[i..e], 2);
            i = e;
            sleep(dt);
        }
        // settle: keepalive しつつ POST の Δ(lufs) が安定するまで待つ。
        let mut prev: Option<f64> = None;
        for _ in 0..60 {
            pre.push_samples(&[], 2);
            post.push_samples(&[], 2);
            sleep(Duration::from_millis(50));
            if let Some(d) = post.poll_delta() {
                if d.mode == DeltaMode::Active && d.lufs.is_some() && d.lufs == prev {
                    break;
                }
                prev = d.lufs;
            }
        }
        delta = post.poll_delta().expect("poll_delta Some");

        // POST の post.json が書かれ、pre_signal_state に PRE の active が反映されること
        // （= POST が PRE を発見した観測可能な証拠）。
        let post_json = find_json_under(&watch_root, "", "post.json").expect("post.json exists");
        let content = std::fs::read_to_string(&post_json).unwrap();
        eprintln!("[3d-a] post.json = {content}");
        post_pre_state_found = content.contains(r#""pre_signal_state":"active""#);

        // post.json は POST 自身の project_uuid(puid-post) 配下（cell 共有でも自分の path）。
        assert!(
            post_json.to_string_lossy().contains("puid-post"),
            "POST post.json は自 project_uuid 配下: {}",
            post_json.display()
        );
    } // engines Drop → io_thread join。

    // Δ 検証: POST(半振幅) − PRE(フル) ≈ -6.02 dB（lufs は 20log10(0.5)）。
    eprintln!(
        "[3d-a] delta mode={:?} lufs={:?} tp={:?} crest={:?}",
        delta.mode, delta.lufs, delta.tp, delta.crest
    );
    assert_eq!(
        delta.mode,
        DeltaMode::Active,
        "PRE 一意・Active・fresh → Δ Active"
    );
    let dl = delta.lufs.expect("delta lufs Some");
    assert!(
        (-8.0..-4.0).contains(&dl),
        "Δ_lufs ≈ -6（POST 半振幅）, got {dl}"
    );
    assert!(
        post_pre_state_found,
        "post.json pre_signal_state=active（PRE 発見の観測証拠）"
    );

    let _ = std::fs::remove_dir_all(&test_root);
}

// ── Phase 3d-b: POST Keep/ack ペアリング実働（PRE 同居 end-to-end）──────────────

/// PRE+POST 同居（同名 "mix" / 別 project_uuid）で POST「Keep」→ write_pending(target=PRE)
/// → PRE が cross-uuid で discover→ack（record_signal status: pending→acknowledged）を実証。
/// 厳格: 不一致名は keep=false（write_pending しない）。Stop で released。
#[test]
#[ignore = "slow: PRE+POST co-located Keep/ack realtime + io_thread filesystem (sets HOME/TMPDIR)"]
fn post_keep_acked_by_colocated_pre() {
    use kirin_measure::{read_signal, SignalStatus};

    let test_root = std::env::temp_dir()
        .join("kirin_b061_test")
        .join(format!("pid{}", std::process::id()));
    let home = test_root.join("home");
    let tmp = test_root.join("tmp");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    std::env::set_var("HOME", &home);
    std::env::set_var("TMPDIR", &tmp);
    // B-106: role-scoped 共有セルを reset してテスト間 first-wins 汚染を排除する（旧 overwrite
    // 隔離の代替）。各テストは自分の set_identity project_uuid を first-wins で seed できる。
    kirin_hypha_ffi::__reset_shared_ids_for_tests();
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"b061-test","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#,
    )
    .unwrap();
    let plugin_data_root = home.join("Library/Application Support/Kirin OS/plugin_data");

    {
        // PRE: 同名 "mix" / project_uuid "puid-pre"（io_thread が autonomous に ack する）。
        let pre = KirinHyphaEngine::new(SR, 2);
        pre.set_license(0);
        pre.set_identity("iid-pre".into(), "puid-pre".into(), "".into(), "mix".into());
        pre.enable_pre_writes();
        pre.set_signal_state(1);

        // POST: 同名 "mix" / project_uuid "puid-post"（別 uuid = cross-uuid ack の肝）。
        let post = KirinHyphaEngine::new(SR, 2);
        post.set_license(0);
        post.set_identity(
            "iid-post".into(),
            "puid-post".into(),
            "".into(),
            "mix".into(),
        );
        post.enable_post_writes();
        post.set_signal_state(1);

        // ~1.5s 駆動して PRE pre.json を active+fresh にする（POST が select できる状態）。
        let sig = gen_stereo_f32(1.5);
        let bf = SR as usize / 10;
        let bl = bf * 2;
        let dt = Duration::from_secs_f64(bf as f64 / SR as f64);
        let mut i = 0;
        while i < sig.len() {
            let e = (i + bl).min(sig.len());
            pre.push_samples(&sig[i..e], 2);
            post.push_samples(&sig[i..e], 2);
            i = e;
            sleep(dt);
        }
        for _ in 0..8 {
            pre.push_samples(&[], 2);
            post.push_samples(&[], 2);
            sleep(Duration::from_millis(50));
        }

        // 厳格: 不一致名 → keep false（select None / write_pending しない）。
        post.set_pair_target("nonexistent".into());
        assert!(
            !post.keep(),
            "不一致名は keep=false（write_pending しない）"
        );
        assert!(!post.is_recording(), "keep false で Record しない");

        // Kirin OS が未起動でも Keep の入口は狭めない。metadata が無い場合は
        // usable frames を通常 TRACE 棚へ出し、完全性だけ degraded reason に残す。
        post.set_pair_target("mix".into());
        assert!(
            post.keep(),
            "一意 PRE 'mix' は expected WAV metadata なしでも keep=true"
        );
        wait_until_recording(&post, "strict-pair-mix");
        assert!(post.is_recording(), "keep 成功で POST Record 開始");

        // poll_delta は Δ を返す（PRE active）。
        let d = post.poll_delta().expect("poll_delta Some");
        eprintln!("[3d-b] post-keep delta mode={:?} lufs={:?}", d.mode, d.lufs);

        // PRE が cross-uuid で discover→ack するまで待つ（1s discover + 1s poll throttle）。
        let mut acked = false;
        for _ in 0..50 {
            pre.push_samples(&[], 2);
            post.push_samples(&[], 2);
            sleep(Duration::from_millis(100));
            if let Some(s) = read_signal(&plugin_data_root, "puid-post", "iid-post") {
                if s.status == SignalStatus::Acknowledged {
                    assert_eq!(s.target_pre_instance_id, "iid-pre", "target は選定 PRE");
                    assert!(
                        s.expected_wav.is_none(),
                        "Kirin OS 未起動相当の Keep は expected_wav なしで Record を継続する"
                    );
                    acked = true;
                    break;
                }
            }
        }
        assert!(
            acked,
            "PRE が別 uuid の record_signal(puid-post/record_signal/iid-post.json) を ack"
        );

        // Stop → released。
        post.stop();
        assert!(!post.is_recording(), "Stop で Watch へ");
        sleep(Duration::from_millis(300));
        if let Some(s) = read_signal(&plugin_data_root, "puid-post", "iid-post") {
            assert_eq!(
                s.status,
                SignalStatus::Released,
                "Stop で record_signal=released"
            );
        }
    }

    let _ = std::fs::remove_dir_all(&test_root);
}

// ── B-062 capstone: ペア録音の実出力 end-to-end ─────────────────────────────────

/// PRE+POST 同居（同名 "mix" / 別 project_uuid）でペア録音を end-to-end 検証する:
/// POST Keep → PRE が ack & **自動 Record** → 両者の Record plugin_data .json が
/// 同一 project_uuid(=POST の) 配下に書かれ、paired linkage で対として辿れることを実証。
#[test]
#[ignore = "slow: paired PRE+POST record session realtime + io_thread filesystem (sets HOME/TMPDIR)"]
fn capstone_paired_record_output_and_linkage() {
    use kirin_measure::plugin_data::{PluginDataFile, Role, Status};

    let test_root = std::env::temp_dir()
        .join("kirin_b062_test")
        .join(format!("pid{}", std::process::id()));
    let home = test_root.join("home");
    let tmp = test_root.join("tmp");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    std::env::set_var("HOME", &home);
    std::env::set_var("TMPDIR", &tmp);
    // B-106: role-scoped 共有セルを reset してテスト間 first-wins 汚染を排除する（旧 overwrite
    // 隔離の代替）。各テストは自分の set_identity project_uuid を first-wins で seed できる。
    kirin_hypha_ffi::__reset_shared_ids_for_tests();
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"b062-test","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#,
    )
    .unwrap();
    let plugin_data_root = home.join("Library/Application Support/Kirin OS/plugin_data");

    let bf = SR as usize / 10;
    let dt = Duration::from_secs_f64(bf as f64 / SR as f64);
    let pre_block = gen_stereo_f32(0.1); // 0.1s フル振幅
    let post_block: Vec<f32> = pre_block.iter().map(|&s| s * 0.5).collect(); // 半振幅(-6dB)

    let delta_lufs: f64;
    {
        let pre = KirinHyphaEngine::new(SR, 2);
        pre.set_license(0);
        pre.set_identity("iid-pre".into(), "puid-pre".into(), "".into(), "mix".into());
        pre.enable_pre_writes();
        pre.set_signal_state(1);

        let post = KirinHyphaEngine::new(SR, 2);
        post.set_license(0);
        post.set_identity(
            "iid-post".into(),
            "puid-post".into(),
            "".into(),
            "mix".into(),
        );
        post.enable_post_writes();
        post.set_signal_state(1);
        post.set_pair_target("mix".into());

        // realtime ブロック投入ヘルパ（PRE フル / POST 半）。
        let position = std::cell::Cell::new(0_i64);
        let drive = |secs: f64| {
            let ticks = (secs / 0.1) as usize;
            for _ in 0..ticks {
                let start = position.get();
                push_positioned_stereo(&pre, &pre_block, start);
                push_positioned_stereo(&post, &post_block, start);
                position.set(start.saturating_add(bf as i64));
                sleep(dt);
            }
        };

        // 1) PRE pre.json を active+fresh にする（POST が select できる状態）。
        drive(1.5);

        // 2) POST Keep → PRE が discover→ack→自動 Record（1s discover + 1s poll throttle）。
        arm_expected_wav(&post, "capstone");
        assert!(post.keep(), "一意 PRE 'mix' で keep=true");
        wait_until_recording(&post, "capstone");
        assert!(post.is_recording(), "POST Record 開始");

        // 3) ペア録音セッション（PRE が ack して Record に入り、両者 frames を書く）。
        drive(4.0);
        let d = post.poll_delta().expect("poll_delta Some");
        delta_lufs = d.lufs.expect("delta lufs Some");
        eprintln!("[capstone] mid-record delta lufs={delta_lufs}");

        // 4) POST Stop → released → PRE が検出して exit_record + writer_close。
        post.stop();
        assert!(!post.is_recording());
        drive(2.0); // PRE が released を検出して閉じるのを待つ + 両 writer close。
    } // engines Drop → io_thread join（残り writer は status=closed flush）。

    // 5) Kirin OS が読む通常 TRACE 棚と PairRecordSession manifest の両方から読み戻す。
    let pre_trace_json = find_json_under(&plugin_data_root, "pre", "*.json")
        .expect("paired PRE must publish a normal TRACE shelf JSON");
    let post_trace_json = find_json_under(&plugin_data_root, "post", "*.json")
        .expect("paired POST must publish a normal TRACE shelf JSON");
    let pre_json = pair_member_json_from_manifest(&plugin_data_root, "pre");
    let post_json = pair_member_json_from_manifest(&plugin_data_root, "post");
    eprintln!("[capstone] PRE hidden  = {}", pre_json.display());
    eprintln!("[capstone] POST hidden = {}", post_json.display());
    eprintln!("[capstone] PRE trace   = {}", pre_trace_json.display());
    eprintln!("[capstone] POST trace  = {}", post_trace_json.display());

    // 両者が同一 project_uuid(=POST の puid-post) 配下に揃う（PRE が adopt）。
    assert!(
        pre_json.to_string_lossy().contains("puid-post"),
        "PRE は POST の uuid 配下に Record: {}",
        pre_json.display()
    );
    assert!(
        post_json.to_string_lossy().contains("puid-post"),
        "POST は自 uuid 配下: {}",
        post_json.display()
    );

    let pre_pd: PluginDataFile =
        serde_json::from_str(&std::fs::read_to_string(&pre_json).unwrap()).unwrap();
    let post_pd: PluginDataFile =
        serde_json::from_str(&std::fs::read_to_string(&post_json).unwrap()).unwrap();
    let pre_trace_pd: PluginDataFile =
        serde_json::from_str(&std::fs::read_to_string(&pre_trace_json).unwrap()).unwrap();
    let post_trace_pd: PluginDataFile =
        serde_json::from_str(&std::fs::read_to_string(&post_trace_json).unwrap()).unwrap();
    assert_eq!(
        pre_trace_pd.checksum, pre_pd.checksum,
        "PRE normal TRACE shelf must mirror the hidden committed member"
    );
    assert_eq!(
        post_trace_pd.checksum, post_pd.checksum,
        "POST normal TRACE shelf must mirror the hidden committed member"
    );

    // schema / status / frames。
    assert_eq!(pre_pd.schema_version, "1.3");
    assert_eq!(post_pd.schema_version, "1.3");
    assert_eq!(pre_pd.role, Role::Pre);
    assert_eq!(post_pd.role, Role::Post);
    assert_eq!(pre_pd.status, Status::Closed, "PRE status=closed");
    assert_eq!(post_pd.status, Status::Closed, "POST status=closed");
    assert!(!pre_pd.frames.is_empty(), "PRE frames 非空");
    assert!(!post_pd.frames.is_empty(), "POST frames 非空");

    // ★ paired linkage: 双方向で対として辿れる。
    assert_eq!(
        pre_pd.paired_post_instance_id.as_deref(),
        Some("iid-post"),
        "PRE.paired_post_instance_id == POST"
    );
    assert_eq!(
        post_pd.paired_pre_instance_id.as_deref(),
        Some("iid-pre"),
        "POST.paired_pre_instance_id == PRE"
    );
    assert_eq!(
        pre_pd.pair_name.as_deref(),
        Some("mix"),
        "PRE.pair_name carries the human pair label for Lens"
    );
    assert_eq!(
        pre_pd.pair_pre_name.as_deref(),
        Some("mix"),
        "PRE.pair_pre_name carries the PRE name for Lens"
    );
    assert_eq!(
        post_pd.pair_name.as_deref(),
        Some("mix"),
        "POST.pair_name carries the selected PRE label for Lens"
    );
    assert_eq!(
        post_pd.pair_pre_name.as_deref(),
        Some("mix"),
        "POST.pair_pre_name carries the selected PRE name for Lens"
    );

    // P2 回帰: paired PRE/POST が同一の非空 record_session_id を持ち、成功経路で
    // .failed 診断棚が出ないことを capstone で直接 assert する（record.rs の session
    // レースや将来のリファクタで、通常棚に出るのに session が抜け/不一致になる退行を検出する）。
    assert!(
        pre_pd
            .record_session_id
            .as_deref()
            .is_some_and(|s| !s.is_empty()),
        "PRE published TRACE must carry a non-empty record_session_id"
    );
    assert_eq!(
        pre_pd.record_session_id, post_pd.record_session_id,
        "paired PRE/POST TRACE shelves must share the same record_session_id"
    );
    assert!(
        find_failed_json_under_role(&plugin_data_root, "pre").is_none(),
        "successful paired Record must not leave a .failed diagnostic shelf for PRE"
    );
    assert!(
        find_failed_json_under_role(&plugin_data_root, "post").is_none(),
        "successful paired Record must not leave a .failed diagnostic shelf for POST"
    );

    // Δ 物理妥当: POST 半振幅 → -6.02dB 相当。
    assert!(
        (-8.0..-4.0).contains(&delta_lufs),
        "Δ_lufs ≈ -6（POST 半振幅）, got {delta_lufs}"
    );

    let _ = std::fs::remove_dir_all(&test_root);
}

// ── B-066 / F2: keep() が try_enter_record 成功後に失敗した時の rollback ───────────

/// keep() が PRE 選定 + try_enter_record 成功の**後**で StoragePaths 解決に失敗した時、
/// 本 keep が行った Record 遷移を巻き戻して false を返す（record_sm が Record で残らない）。
/// platform storage env を一時的に外して StoragePaths::default_platform() を強制失敗させる（⑤経路）。
/// positive control（platform env 復帰で同 PRE が keep 成功）により、失敗時に select=Some まで
/// 到達していた＝②の select-None 早期 return ではなく⑤の post-enter 失敗だったことを立証。
#[test]
#[ignore = "slow: PRE+POST co-located; forces StoragePaths failure to test keep() rollback (sets HOME/APPDATA/LOCALAPPDATA/TMPDIR)"]
fn keep_failure_after_enter_reverts_record_state() {
    let test_root = std::env::temp_dir()
        .join("kirin_b066_test")
        .join(format!("pid{}", std::process::id()));
    let home = test_root.join("home");
    let appdata = home.join("AppData").join("Roaming");
    let local_appdata = home.join("AppData").join("Local");
    let tmp = test_root.join("tmp");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&appdata).unwrap();
    std::fs::create_dir_all(&local_appdata).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    std::env::set_var("HOME", &home);
    std::env::set_var("APPDATA", &appdata);
    std::env::set_var("LOCALAPPDATA", &local_appdata);
    std::env::set_var("TMPDIR", &tmp);
    // B-106: role-scoped 共有セルを reset してテスト間 first-wins 汚染を排除する（旧 overwrite
    // 隔離の代替）。各テストは自分の set_identity project_uuid を first-wins で seed できる。
    kirin_hypha_ffi::__reset_shared_ids_for_tests();
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"b066-test","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#,
    )
    .unwrap();
    let watch_root = tmp.join("kirin");

    {
        let pre = KirinHyphaEngine::new(SR, 2);
        pre.set_license(0);
        pre.set_identity("iid-pre".into(), "puid-pre".into(), "".into(), "mix".into());
        pre.enable_pre_writes();
        pre.set_signal_state(1);

        let post = KirinHyphaEngine::new(SR, 2);
        post.set_license(0);
        post.set_identity(
            "iid-post".into(),
            "puid-post".into(),
            "".into(),
            "mix".into(),
        );
        post.enable_post_writes();
        post.set_signal_state(1);
        post.set_pair_target("mix".into());

        // PRE pre.json を active+fresh にする（POST が select_target_pre できる状態）。
        let sig = gen_stereo_f32(1.5);
        let bf = SR as usize / 10;
        let bl = bf * 2;
        let dt = Duration::from_secs_f64(bf as f64 / SR as f64);
        let mut i = 0;
        while i < sig.len() {
            let e = (i + bl).min(sig.len());
            pre.push_samples(&sig[i..e], 2);
            post.push_samples(&sig[i..e], 2);
            i = e;
            sleep(dt);
        }
        // PRE が pre.json を実際に書くまで待つ（select 可能を保証）。pushはここで止め、
        // 直後に keep を撃つので heartbeat は fresh（Inactive 化前）。
        for _ in 0..30 {
            if find_json_under(&watch_root, "", "pre.json").is_some() {
                break;
            }
            pre.push_samples(&[], 2);
            sleep(Duration::from_millis(50));
        }
        assert!(
            find_json_under(&watch_root, "", "pre.json").is_some(),
            "PRE pre.json が書かれていること（select 前提）"
        );
        // 直前に PRE を押して Active/heartbeat を確実に保つ。
        pre.push_samples(&[], 2);

        // ── 失敗ケース: platform storage env を外して StoragePaths::default_platform() を強制失敗（⑤経路）──
        std::env::remove_var("HOME");
        std::env::remove_var("APPDATA");
        std::env::remove_var("LOCALAPPDATA");
        let kept_fail = post.keep();
        std::env::set_var("HOME", &home); // 後続/cleanup 用に即復帰。
        std::env::set_var("APPDATA", &appdata);
        std::env::set_var("LOCALAPPDATA", &local_appdata);

        assert!(!kept_fail, "StoragePaths 失敗 → keep()=false");
        assert!(
            !post.is_recording(),
            "F2 fix: keep() 失敗時に record_sm を Watch へ巻き戻す（Record で残さない）"
        );

        // ── positive control: platform env 復帰で同 PRE が keep 成功 → 上の失敗は select=Some 後
        //    （= ⑤ post-enter 失敗）だったと立証。失敗とこの control の間に sleep を挟まない。
        arm_expected_wav(&post, "storage-recovered");
        assert!(
            post.keep(),
            "HOME 復帰: 同 PRE が選定可 → keep()=true（失敗経路が select 到達済を立証）"
        );
        wait_until_recording(&post, "storage-recovered");
        assert!(post.is_recording(), "成功 keep で Record 開始");
        post.stop();
        assert!(!post.is_recording(), "stop で Watch へ");
    }

    let _ = std::fs::remove_dir_all(&test_root);
}

// ── B-067 / F3: annotation がこの engine の role を対象にする ─────────────────────

/// enable していない engine（write_role 未確定）は Os でも add_annotation を no-op にする。
#[test]
fn add_annotation_noop_without_role() {
    let engine = KirinHyphaEngine::new(SR, 2);
    engine.set_license(0); // Os
    assert!(
        !engine.add_annotation("x".into()),
        "未 enable（role None）は add_annotation=false（既定 ::Pre で勝手に書かない）"
    );
}

/// POST engine の add_annotation が POST role（post/ の Record .json）を対象にすることを実証。
/// 修正前: ハードコード role=Pre で {puid}/{iid-post}/pre/（POST 自身の空 subdir）を見て false。
/// 修正後: 保持 role=Post で {puid}/{iid-post}/post/ の Record .json に追記。
#[test]
#[ignore = "slow: paired PRE+POST record then POST annotation by role (sets HOME/TMPDIR)"]
fn post_add_annotation_targets_post_role() {
    use kirin_measure::plugin_data::PluginDataFile;

    let test_root = std::env::temp_dir()
        .join("kirin_b067_test")
        .join(format!("pid{}", std::process::id()));
    let home = test_root.join("home");
    let tmp = test_root.join("tmp");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    std::env::set_var("HOME", &home);
    std::env::set_var("TMPDIR", &tmp);
    // B-106: role-scoped 共有セルを reset してテスト間 first-wins 汚染を排除する（旧 overwrite
    // 隔離の代替）。各テストは自分の set_identity project_uuid を first-wins で seed できる。
    kirin_hypha_ffi::__reset_shared_ids_for_tests();
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"b067-test","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#,
    )
    .unwrap();
    let plugin_data_root = home.join("Library/Application Support/Kirin OS/plugin_data");

    {
        let pre = KirinHyphaEngine::new(SR, 2);
        pre.set_license(0);
        pre.set_identity("iid-pre".into(), "puid-pre".into(), "".into(), "mix".into());
        pre.enable_pre_writes();
        pre.set_signal_state(1);

        let post = KirinHyphaEngine::new(SR, 2);
        post.set_license(0);
        post.set_identity(
            "iid-post".into(),
            "puid-post".into(),
            "".into(),
            "mix".into(),
        );
        post.enable_post_writes();
        post.set_signal_state(1);
        post.set_pair_target("mix".into());

        let bf = SR as usize / 10;
        let bl = bf * 2;
        let dt = Duration::from_secs_f64(bf as f64 / SR as f64);
        let blk = gen_stereo_f32(0.1);
        let position = std::cell::Cell::new(0_i64);
        let drive = |secs: f64| {
            for _ in 0..((secs / 0.1) as usize) {
                let start = position.get();
                push_positioned_stereo(&pre, &blk, start);
                push_positioned_stereo(&post, &blk, start);
                position.set(start.saturating_add(bf as i64));
                sleep(dt);
            }
        };
        let _ = bl;

        drive(1.5); // PRE pre.json active
        arm_expected_wav(&post, "annotation-post");
        assert!(post.keep(), "keep true");
        wait_until_recording(&post, "annotation-post");
        drive(4.0); // PRE acks + both record frames
        post.stop();
        drive(2.0); // PRE closes; both Record .json status=closed

        // Record close 後に POST へ注釈（header 注: 確実なのは close 後）。
        assert!(
            post.add_annotation("post-note".into()),
            "F3 fix: POST add_annotation が POST role の Record .json を見つけて true"
        );
    }

    // POST の通常 TRACE 棚と hidden member の両方に注釈が入ったこと。
    let post_trace_json = find_json_under(&plugin_data_root, "post", "*.json")
        .expect("paired POST normal TRACE shelf JSON exists");
    let post_json = pair_member_json_from_manifest(&plugin_data_root, "post");
    eprintln!("[B-067] POST hidden = {}", post_json.display());
    eprintln!("[B-067] POST trace  = {}", post_trace_json.display());
    let post_pd: PluginDataFile =
        serde_json::from_str(&std::fs::read_to_string(&post_json).unwrap()).unwrap();
    let post_trace_pd: PluginDataFile =
        serde_json::from_str(&std::fs::read_to_string(&post_trace_json).unwrap()).unwrap();
    assert!(
        post_pd.annotations.iter().any(|a| a.memo == "post-note"),
        "annotation が POST hidden member に入る（PRE 固定をやめた）"
    );
    assert!(
        post_trace_pd
            .annotations
            .iter()
            .any(|a| a.memo == "post-note"),
        "annotation が POST normal TRACE shelf にも入る"
    );

    let _ = std::fs::remove_dir_all(&test_root);
}

// ── B-068: kirin_hypha_load_license が identity.json の license を読む ──────────────

/// load_license が identity.json の "license" を読み Os=0/Sense=1/Unknown=2 を返す。
/// ファイル不在・$HOME 不在は 2=Unknown（安全側）。
#[test]
#[ignore = "slow: mutates HOME/identity.json (run with --test-threads=1)"]
fn load_license_reads_identity_json() {
    let test_root = std::env::temp_dir()
        .join("kirin_b068_test")
        .join(format!("pid{}", std::process::id()));
    let home = test_root.join("home");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::env::set_var("HOME", &home);
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    let id_path = kirin_os.join("identity.json");

    let write_license = |lic: &str| {
        std::fs::write(
            &id_path,
            format!(
                r#"{{"schema_version":"1.0","installation_id":"b068","hardware_id":"hw","hardware_components":{{"iop":"a","sn":"b","bd":"c"}},"machine_signature":"sig","license":"{lic}","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}}"#
            ),
        )
        .unwrap();
    };

    write_license("os");
    assert_eq!(
        kirin_hypha_ffi::kirin_hypha_load_license(),
        0,
        "license=os → 0"
    );

    write_license("sense");
    assert_eq!(
        kirin_hypha_ffi::kirin_hypha_load_license(),
        1,
        "license=sense → 1"
    );

    // 不明値 → Unknown(2)。
    write_license("bogus");
    assert_eq!(
        kirin_hypha_ffi::kirin_hypha_load_license(),
        2,
        "unknown value → 2"
    );

    // ファイル不在 → Unknown(2)。
    std::fs::remove_file(&id_path).unwrap();
    assert_eq!(
        kirin_hypha_ffi::kirin_hypha_load_license(),
        2,
        "missing file → 2"
    );

    let _ = std::fs::remove_dir_all(&test_root);
}

// ── B-071 / F2 follow-up: double-keep が既存 linkage を温存（clobber しない）─────────

/// 同一 POST が keep を 2 回呼んだとき、2 回目（既に Record 中）は no-op で false を返し、
/// 1 回目に確定した paired_pre_target linkage を温存する（None 化＝clobber しない）。
/// 修正前: 2 回目は投機的 set→AlreadyRecording→None クリアで linkage を壊していた。
#[test]
#[ignore = "slow: PRE+POST co-located double-keep linkage (sets HOME/TMPDIR)"]
fn double_keep_preserves_linkage() {
    let test_root = std::env::temp_dir()
        .join("kirin_b071_test")
        .join(format!("pid{}", std::process::id()));
    let home = test_root.join("home");
    let tmp = test_root.join("tmp");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    std::env::set_var("HOME", &home);
    std::env::set_var("TMPDIR", &tmp);
    // B-106: role-scoped 共有セルを reset してテスト間 first-wins 汚染を排除する（旧 overwrite
    // 隔離の代替）。各テストは自分の set_identity project_uuid を first-wins で seed できる。
    kirin_hypha_ffi::__reset_shared_ids_for_tests();
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"b071","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#,
    )
    .unwrap();
    let watch_root = tmp.join("kirin");

    {
        let pre = KirinHyphaEngine::new(SR, 2);
        pre.set_license(0);
        pre.set_identity("iid-pre".into(), "puid-pre".into(), "".into(), "mix".into());
        pre.enable_pre_writes();
        pre.set_signal_state(1);

        let post = KirinHyphaEngine::new(SR, 2);
        post.set_license(0);
        post.set_identity(
            "iid-post".into(),
            "puid-post".into(),
            "".into(),
            "mix".into(),
        );
        post.enable_post_writes();
        post.set_signal_state(1);
        post.set_pair_target("mix".into());

        let sig = gen_stereo_f32(1.5);
        let bf = SR as usize / 10;
        let bl = bf * 2;
        let dt = Duration::from_secs_f64(bf as f64 / SR as f64);
        let mut i = 0;
        while i < sig.len() {
            let e = (i + bl).min(sig.len());
            pre.push_samples(&sig[i..e], 2);
            post.push_samples(&sig[i..e], 2);
            i = e;
            sleep(dt);
        }
        for _ in 0..20 {
            if find_json_under(&watch_root, "", "pre.json").is_some() {
                break;
            }
            pre.push_samples(&[], 2);
            sleep(Duration::from_millis(50));
        }
        pre.push_samples(&[], 2);

        // 1 回目 keep: 成功 → Record + linkage Some(iid-pre)。
        arm_expected_wav(&post, "double-keep");
        assert!(post.keep(), "1st keep true");
        wait_until_recording(&post, "double-keep");
        assert!(post.is_recording(), "1st keep enters Record");
        assert_eq!(
            post.paired_pre_target_snapshot().as_deref(),
            Some("iid-pre"),
            "1st keep sets linkage to selected PRE"
        );

        // 2 回目 keep: 既に Record 中 → no-op false。linkage は温存（None 化しない）。
        assert!(
            !post.keep(),
            "2nd keep is no-op (already recording) -> false"
        );
        assert!(post.is_recording(), "2nd keep does not drop out of Record");
        assert_eq!(
            post.paired_pre_target_snapshot().as_deref(),
            Some("iid-pre"),
            "B-071: 2nd keep must PRESERVE the linkage (not clobber to None)"
        );

        post.stop();
        assert!(!post.is_recording(), "stop -> Watch");
    }

    let _ = std::fs::remove_dir_all(&test_root);
}

// ── B-075: ring overflow → dropped_samples を C ABI に露出（欠落の沈黙解消・data 層）──

/// ring 満杯を強制（現在の ring 保持長を超えるバーストを一括 push）→ poll_result の dropped_samples が >0 で
/// 露出されることを実証。計測式は不変（露出のみ）— 計測 parity は既存テストが担保。
#[test]
fn dropped_samples_surfaced_via_c_abi_on_ring_overflow() {
    use kirin_hypha_ffi::{
        kirin_hypha_create, kirin_hypha_destroy, kirin_hypha_poll_result, kirin_hypha_push_samples,
        kirin_hypha_set_signal_state, KirinMeasureResult,
    };
    unsafe {
        let h = kirin_hypha_create(SR, 2);
        assert!(!h.is_null(), "create");
        kirin_hypha_set_signal_state(h, 1); // Active

        // ring 容量 = SR * RING_BUFFER_SECONDS * 2ch。その 2 倍を一括 push
        // → Measure Thread が並行消費しても Audio Thread 側で push 失敗が起き
        // push_overflow が積み上がる。
        let ring_capacity = SR as usize * RING_BUFFER_SECONDS * 2;
        let burst = vec![0.05f32; ring_capacity * 2];
        kirin_hypha_push_samples(h, burst.as_ptr(), burst.len() / 2, 2);

        // Measure Thread が結果を出すまで keepalive しつつ poll。
        let mut out: KirinMeasureResult = std::mem::zeroed();
        let mut got = false;
        for _ in 0..12 {
            kirin_hypha_push_samples(h, std::ptr::null(), 0, 2);
            sleep(Duration::from_millis(50));
            if kirin_hypha_poll_result(h, &mut out) {
                got = true;
            }
        }
        assert!(got, "measure result should be produced");
        assert!(
            out.dropped_samples > 0,
            "ring overflow must surface as dropped_samples (got {})",
            out.dropped_samples
        );
        kirin_hypha_destroy(h);
    }
}

// ── B-125: oversized block drop を push_overflow とは別カウンタで計数し dropped_samples に合算 ──

/// (ii) push_overflow と oversized_drop が独立に計数される（混ざらない）。
/// note_oversized_drop は oversized_drop のみ・ring overflow は push_overflow のみを動かす。
#[test]
fn b125_overflow_and_oversized_drop_are_independent_counters() {
    // note_oversized_drop だけ → oversized_drop_count のみ増え overflow_count は 0 のまま。
    let engine = KirinHyphaEngine::new(SR, 2);
    assert_eq!(engine.overflow_count(), 0);
    assert_eq!(engine.oversized_drop_count(), 0);
    engine.note_oversized_drop(100);
    engine.note_oversized_drop(50);
    assert_eq!(engine.oversized_drop_count(), 150, "oversized のみ積算");
    assert_eq!(
        engine.overflow_count(),
        0,
        "push_overflow は note_oversized_drop で増えない（別カウンタ＝混ざらない）"
    );

    // 逆: ring 満杯 burst だけ → overflow_count のみ増え oversized_drop_count は 0 のまま。
    let engine2 = KirinHyphaEngine::new(SR, 2);
    engine2.set_signal_state(1);
    let ring_capacity = SR as usize * RING_BUFFER_SECONDS * 2;
    let burst = vec![0.05f32; ring_capacity * 2]; // Measure Thread が並行消費しても ring 容量を超過
    engine2.push_samples(&burst, 2);
    assert!(
        engine2.overflow_count() > 0,
        "ring overflow は push_overflow を積む"
    );
    assert_eq!(
        engine2.oversized_drop_count(),
        0,
        "oversized_drop は ring overflow では増えない（別カウンタ＝混ざらない）"
    );
}

/// (iv-FFI) prealloc-max 超の病的 block の drop（JUCE 殻が kirin_hypha_note_oversized_drop で計上）が
/// ring overflow ゼロでも C ABI dropped_samples に合算露出される（無記録欠落の解消＝ZSA）。
/// JUCE 側の `needed <= scratchCapacitySamples` 算術境界自体は C++（PluginProcessor.cpp）で、
/// repo に C++ 単体ハーネスが無いため build + コードレビューで担保（report 参照）。本テストは
/// その fallback が呼ぶ FFI 経路（note → dropped_samples 合算）を実値で検証する。
#[test]
fn b125_oversized_drop_surfaces_in_c_abi_dropped_samples() {
    use kirin_hypha_ffi::{
        kirin_hypha_create, kirin_hypha_destroy, kirin_hypha_note_oversized_drop,
        kirin_hypha_poll_result, kirin_hypha_push_samples, kirin_hypha_set_signal_state,
        KirinMeasureResult,
    };
    unsafe {
        let h = kirin_hypha_create(SR, 2);
        assert!(!h.is_null(), "create");
        kirin_hypha_set_signal_state(h, 1); // Active

        // 1 病的 block 相当（例: 2048 frames × 2ch）を JUCE oversized 分岐と同じ note で計上。
        // ring overflow は意図的に起こさない（容量内の小ブロックのみを流す）。
        const OVERSIZED: u64 = 4096;
        kirin_hypha_note_oversized_drop(h, OVERSIZED);

        let mut out: KirinMeasureResult = std::mem::zeroed();
        let mut got = false;
        for _ in 0..20 {
            // 容量内（256 frames × 2ch）= overflow を起こさず measure result を生成。
            let blk = [0.01f32; 512];
            kirin_hypha_push_samples(h, blk.as_ptr(), 256, 2);
            sleep(Duration::from_millis(30));
            if kirin_hypha_poll_result(h, &mut out) {
                got = true;
            }
        }
        assert!(got, "measure result should be produced");
        // dropped_samples = overflow_count(0) + oversized_drop_count(OVERSIZED)（合算注入）。
        assert_eq!(
            out.dropped_samples, OVERSIZED,
            "oversized drop must surface in dropped_samples with no ring overflow (got {})",
            out.dropped_samples
        );
        kirin_hypha_destroy(h);
    }
}

// ── B-106: 2 POST instance が同一 dylib 共有セルで同一棚に first-wins 収束する（棚分裂修正）──

/// 単一プロセスに POST engine を 2 つ enable し、別々の set_identity project_uuid を渡しても
/// FFI dylib 共有セル（`shared_post_project_hash_cell`）が first-wins で 1 つの値に収束する
/// ＝両 POST の io_thread broadcast scan 棚が一致することを、**実 module statics 経由**で検証する
/// （B-105 で判明した「押したペアだけ Record」棚分裂の直接の回帰ガード）。b106_shared_id_tests は
/// 局所 Arc で resolve_shared_id を叩くのみで実 enable_post_writes 経路を通らないため本テストで補完。
/// enable は io_thread spawn + filesystem 触り + global HOME/TMPDIR set のため #[ignore]（--test-threads=1）。
#[test]
#[ignore = "B-106: 2 POST convergence; sets HOME/TMPDIR + spawns io_threads (run with --test-threads=1)"]
fn two_post_instances_converge_on_one_shelf() {
    use kirin_hypha_ffi::{kirin_hypha_get_identity, KirinIdentity};

    let test_root = std::env::temp_dir()
        .join("kirin_b106_test")
        .join(format!("pid{}", std::process::id()));
    let home = test_root.join("home");
    let tmp = test_root.join("tmp");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    std::env::set_var("HOME", &home);
    std::env::set_var("TMPDIR", &tmp);
    // B-106: 他テストの first-wins seed を引き継がないよう reset（旧 overwrite 隔離の代替）。
    kirin_hypha_ffi::__reset_shared_ids_for_tests();
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"b106-test","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-11T00:00:00Z","last_verified_at":"2026-06-11T00:00:00Z"}"#,
    )
    .unwrap();

    let read_puid = |e: &mut KirinHyphaEngine| -> String {
        let ptr = e as *mut KirinHyphaEngine;
        let mut out: KirinIdentity = unsafe { std::mem::zeroed() };
        unsafe { kirin_hypha_get_identity(ptr, &mut out) };
        cbuf_to_string(&out.project_uuid)
    };

    {
        // POST A: 自分の project_uuid "puid-a" を持ち最初に enable → 共有 POST セルを seed。
        let mut post_a = KirinHyphaEngine::new(SR, 2);
        post_a.set_license(0);
        post_a.set_identity("iid-a".into(), "puid-a".into(), "".into(), "mix".into());
        post_a.enable_post_writes();

        // POST B: 別の project_uuid "puid-b" を持つが、enable で共有 POST セル既値を採用（first-wins）。
        let mut post_b = KirinHyphaEngine::new(SR, 2);
        post_b.set_license(0);
        post_b.set_identity("iid-b".into(), "puid-b".into(), "".into(), "mix".into());
        post_b.enable_post_writes();

        let puid_a = read_puid(&mut post_a);
        let puid_b = read_puid(&mut post_b);

        // 旧実装（per-instance 生成・凍結）では puid_a="puid-a" / puid_b="puid-b" で棚分裂し、
        // B のクロスインスタンス broadcast が A の棚に届かなかった（症状の根因）。
        // B-106 では両者が 1st POST の値に収束する＝同一 broadcast 棚。
        assert_eq!(puid_a, "puid-a", "1st POST が共有 POST セルを seed する");
        assert_eq!(
            puid_b, "puid-a",
            "B-106: 2nd POST は 1st の棚を採用（first-wins / 上書きせず）= 棚分裂しない"
        );
    } // engines Drop → io_thread join。

    let _ = std::fs::remove_dir_all(&test_root);
}

// ── B-118: FFI watchdog（T-8 再採用）再起動・UAF 検証（realtime / #[ignore]）─────

/// B-118 (iii): Measure Thread 死 → watchdog 再 spawn → 計測再開（FFI 経路）。
/// `__force_measure_restart_for_test` で measure を強制終了し、watchdog（1s 検査）が
/// is_finished を検出して再 spawn → measure_alive 復帰 + poll_result 再開を観測する。
#[test]
#[ignore]
fn b118_measure_restart_recovers_via_watchdog() {
    let engine = KirinHyphaEngine::new(SR, 2);
    engine.set_signal_state(1); // Active
    assert!(engine.measure_alive(), "起動直後は measure 生存");

    // Measure Thread を強制終了（watchdog が再 spawn するはず）。
    engine.__force_measure_restart_for_test();

    // watchdog の 1s 検査で再 spawn。新 producer は pending_producer → push_samples が swap。
    let block = vec![0.05f32; (SR as usize / 10) * 2]; // 0.1s stereo
    let mut recovered = false;
    for _ in 0..100 {
        // ~5s 上限
        engine.push_samples(&block, 2);
        sleep(Duration::from_millis(50));
        if engine.measure_alive() && engine.poll_result().is_some() {
            recovered = true;
            break;
        }
    }
    assert!(
        recovered,
        "watchdog 再 spawn 後に measure_alive 復帰 + 計測再開（poll_result Some）"
    );
}

/// B-118 (iv): 再起動を跨いだ Drop で UAF/hang なし。force restart で再起動世代を作り、
/// scope 終端の Drop（watchdog_shutdown → watchdog join[io→measure] → identity_detach）が
/// 完了することを観測する（hang すれば本テストが harness timeout で落ちる）。
#[test]
#[ignore]
fn b118_drop_after_measure_restart_no_uaf() {
    let completed = {
        let engine = KirinHyphaEngine::new(SR, 2);
        engine.set_signal_state(1);
        engine.__force_measure_restart_for_test();
        sleep(Duration::from_millis(1500)); // watchdog 再 spawn（再起動世代を確定）
        assert!(engine.measure_alive(), "Drop 前に再 spawn 済み");
        true
        // engine が scope を抜けて Drop → 全世代 join。hang/UAF があればここで止まる。
    };
    assert!(completed, "再起動跨ぎの Drop が hang せず完了（UAF なし）");
}

/// B-118 Phase 3 (② getter): 未 enable（project_hash 空）では排他 conflict は false（保守側）。
#[test]
fn b118_record_exclusion_conflict_false_before_enable() {
    let engine = KirinHyphaEngine::new(SR, 2);
    assert!(
        !engine.record_exclusion_conflict(),
        "未 enable は exclusion 非 conflict（ブロックしない）"
    );
}

/// B-118 Phase 3 (③ getter): io 失敗が無ければ record_error_message は None（R-26 沈黙）。
#[test]
fn b118_record_error_message_none_initially() {
    let engine = KirinHyphaEngine::new(SR, 2);
    assert_eq!(engine.record_error_message(), None, "通常時は None");
}

// ── B-127 (G-115-365): cap 真実源 = O_EXCL 枠存在。engine resolve_and_enter_keep（両殻 parity）──

/// G-115-365: cap 真実源 = O_EXCL 枠存在。`n` 個の distinct pairing 枠を作る（reserve_pairing）。
fn b127_make_frames(base: &std::path::Path, project_hash: &str, n: usize) {
    for i in 0..n {
        kirin_measure::reservation::reserve_pairing(
            base,
            project_hash,
            &format!("cap-pre-{i}"),
            &format!("cap-post-{i}"),
        )
        .unwrap();
    }
}

/// HOME/TMPDIR を temp へ分離し Os identity.json を置く（既存 #[ignore] 群と同手順）。
/// グローバル env を触るため --test-threads=1（FFI gate と同じ）。戻り値 (home, tmp)。
fn b127_isolate(tag: &str) -> (std::path::PathBuf, std::path::PathBuf) {
    let test_root = std::env::temp_dir()
        .join("kirin_b127")
        .join(format!("{tag}_pid{}", std::process::id()));
    let home = test_root.join("home");
    let tmp = test_root.join("tmp");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    std::env::set_var("HOME", &home);
    std::env::set_var("TMPDIR", &tmp);
    kirin_hypha_ffi::__reset_shared_ids_for_tests();
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"b127-test","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#,
    )
    .unwrap();
    (home, tmp)
}

/// "mix" の fresh pre.json が TMPDIR/kirin に書かれるまで待つ PRE engine（POST の arm 先）。
/// 返す engine は生かし続けること（drop すると io_thread 停止で pre.json が stale 化する）。
fn b127_spawn_pre(puid: &str, tmp: &std::path::Path) -> KirinHyphaEngine {
    let pre = KirinHyphaEngine::new(SR, 2);
    pre.set_license(0);
    pre.set_identity("iid-pre".into(), puid.into(), "".into(), "mix".into());
    pre.enable_pre_writes();
    pre.set_signal_state(1); // Active
    let watch_root = tmp.join("kirin");
    let mut found = false;
    for _ in 0..40 {
        pre.push_samples(&[], 2);
        sleep(Duration::from_millis(50));
        if find_json_under(&watch_root, "", "pre.json").is_some() {
            found = true;
            break;
        }
    }
    assert!(found, "PRE pre.json must be written (arm target)");
    pre
}

/// B-140: 無音/停止中に Keep した POST が、Record 中に PRE ラッチ無しのまま NoPre 固着しないこと。
///
/// 再現条件:
/// 1. PRE/POST とも Inactive のまま PRE pre.json だけ fresh にする。
/// 2. POST Keep（Arm は Inactive PRE を許容）で Record に入る。
/// 3. その後に両者を Active にして音を入れる。
///
/// 旧挙動では Keep 時に display ラッチが空のまま Record 中の「ラッチ凍結」に入り、音が来ても
/// `poll_delta()` が NoPre/None のままだった。Keep 成功時に選定 PRE をラッチへ焼くことで、
/// 再生後に同じ PRE から Δ が出る。
#[test]
#[ignore = "slow: HOME/TMPDIR + io_thread filesystem; inactive Keep must latch for later delta (sets env)"]
fn b140_inactive_keep_latches_pre_for_delta_after_audio() {
    use kirin_measure::DeltaMode;

    let (_home, tmp) = b127_isolate("inactive-keep-latch");
    let puid = "puid-b140-inactive";
    let watch_root = tmp.join("kirin");

    let pre = KirinHyphaEngine::new(SR, 2);
    pre.set_license(0);
    pre.set_identity("iid-pre-b140".into(), puid.into(), "".into(), "mix".into());
    pre.enable_pre_writes();
    pre.set_signal_state(0); // Inactive ABI

    let mut found_inactive = false;
    for _ in 0..40 {
        pre.push_samples(&[], 2);
        sleep(Duration::from_millis(50));
        if let Some(path) = find_json_under(&watch_root, "", "pre.json") {
            let content = std::fs::read_to_string(path).unwrap();
            if content.contains(r#""name":"mix""#)
                && content.contains(r#""signal_state":"inactive""#)
            {
                found_inactive = true;
                break;
            }
        }
    }
    assert!(
        found_inactive,
        "Inactive PRE pre.json must be fresh and arm-selectable"
    );

    let post = KirinHyphaEngine::new(SR, 2);
    post.set_license(0);
    post.set_identity("iid-post-b140".into(), puid.into(), "".into(), "mix".into());
    post.enable_post_writes();
    post.set_signal_state(0); // Inactive ABI
    post.set_pair_target("mix".to_string());
    for _ in 0..8 {
        pre.push_samples(&[], 2);
        post.push_samples(&[], 2);
        sleep(Duration::from_millis(50));
    }

    arm_expected_wav(&post, "inactive-keep-latch");
    assert!(post.keep(), "Inactive but fresh PRE should be armable");
    wait_until_recording(&post, "inactive-keep-latch");
    assert!(
        post.is_recording(),
        "POST enters Record while still inactive"
    );

    pre.set_signal_state(1); // Active ABI
    post.set_signal_state(1);
    let pre_block = gen_stereo_f32(0.1);
    let post_block: Vec<f32> = pre_block.iter().map(|&s| s * 0.5).collect();
    let dt = Duration::from_millis(100);

    let mut delta_active = None;
    let mut stable_hits = 0;
    for _ in 0..80 {
        pre.push_samples(&pre_block, 2);
        post.push_samples(&post_block, 2);
        sleep(dt);
        if let Some(d) = post.poll_delta() {
            if d.mode == DeltaMode::Active {
                if let Some(dl) = d.lufs {
                    delta_active = Some(dl);
                    if (-8.0..-4.0).contains(&dl) {
                        stable_hits += 1;
                        if stable_hits >= 2 {
                            break;
                        }
                    } else {
                        stable_hits = 0;
                    }
                }
            }
        }
    }

    let dl = delta_active.expect("delta should become Active after audio starts");
    assert!(
        (-8.0..-4.0).contains(&dl),
        "Δ_lufs should track POST half-amplitude vs PRE full-amplitude, got {dl}"
    );

    drop(post);
    drop(pre);
    let _ = std::fs::remove_dir_all(tmp.parent().unwrap());
}

/// base/<ph>/<iid>/<role>/ に paired_pre/paired_post を設定した active marker を 1 件書く。
fn b127_write_paired_marker(
    base: &std::path::Path,
    ph: &str,
    iid: &str,
    role: kirin_measure::PluginDataRole,
    paired_pre: Option<&str>,
    paired_post: Option<&str>,
) {
    use kirin_measure::{PluginDataWriter, WriterPaths};
    let paths = WriterPaths::build(base, ph, iid, role, "2026-06-13T00:00:00Z");
    let mut w = PluginDataWriter::create(
        paths,
        "b127-install".to_string(),
        ph.to_string(),
        iid.to_string(),
        role,
        None,
        SR,
        paired_pre.map(|s| s.to_string()),
        paired_post.map(|s| s.to_string()),
        None,
        None,
    )
    .unwrap();
    w.flush().unwrap();
}

/// `n` 個の双方向 pairing（PRE: paired_post=post / POST: paired_pre=pre・各 active）を書く（=2n marker）。
fn b127_write_bidi_pairs(base: &std::path::Path, ph: &str, n: usize) {
    use kirin_measure::PluginDataRole;
    for i in 0..n {
        let pre = format!("bidi-pre-{i}");
        let post = format!("bidi-post-{i}");
        b127_write_paired_marker(base, ph, &pre, PluginDataRole::Pre, None, Some(&post));
        b127_write_paired_marker(base, ph, &post, PluginDataRole::Post, Some(&pre), None);
    }
}

/// (i) G-115-365 端到端: cap 真実源 = O_EXCL **枠存在のみ**。active marker は数えない。12 双方向
/// pairing = **24 active marker** を置いても **枠が 0** なら keep は通る（旧 marker 計数なら 24
/// marker で即 cap 超過 reject していた）。markers でなく frames を数える是正の実証。
#[test]
#[ignore = "slow: HOME/TMPDIR + io_thread filesystem; cap counts frames not markers (sets env)"]
fn b127_engine_counts_pairings_not_markers() {
    let (home, tmp) = b127_isolate("bidi12");
    let puid = "puid-b127-bidi";
    let base = home.join("Library/Application Support/Kirin OS/plugin_data");

    let _pre = b127_spawn_pre(puid, &tmp);
    b127_write_bidi_pairs(&base, puid, 12); // 24 active marker・**枠は 0**

    let post = KirinHyphaEngine::new(SR, 2);
    post.set_license(0);
    post.set_identity("iid-frame".into(), puid.into(), "".into(), "mix".into());
    post.enable_post_writes();
    post.set_signal_state(1);
    post.set_pair_target("mix".to_string());
    for _ in 0..8 {
        post.push_samples(&[], 2);
        sleep(Duration::from_millis(40));
    }

    arm_expected_wav(&post, "b127-bidi12");
    let entered = post.keep();
    assert!(
        entered,
        "keep must succeed despite 24 active markers (枠=0 / cap 真実源は marker でなく O_EXCL 枠存在)"
    );
    wait_until_recording(&post, "b127-bidi12");
    assert!(
        post.is_recording(),
        "engine enters Record (frame-counted, markers ignored)"
    );

    drop(post);
    let _ = std::fs::remove_dir_all(home.parent().unwrap());
}

/// (v) reservation 解放: keep で O_EXCL 枠が作られ、stop で解放される（再予約可になる）。
#[test]
#[ignore = "slow: HOME/TMPDIR + io_thread filesystem; reservation lifecycle (sets env)"]
fn b127_reservation_released_on_stop() {
    let (home, tmp) = b127_isolate("release");
    let puid = "puid-b127-rel";
    let base = home.join("Library/Application Support/Kirin OS/plugin_data");

    let _pre = b127_spawn_pre(puid, &tmp); // PRE "mix" = "iid-pre"

    let post = KirinHyphaEngine::new(SR, 2);
    post.set_license(0);
    post.set_identity("iid-rel".into(), puid.into(), "".into(), "mix".into());
    post.enable_post_writes();
    post.set_signal_state(1);
    post.set_pair_target("mix".to_string());
    for _ in 0..8 {
        post.push_samples(&[], 2);
        sleep(Duration::from_millis(40));
    }

    // pairing key = (resolved PRE iid "iid-pre", POST iid "iid-rel")。
    let res_path = base
        .join(puid)
        .join("record_reservation")
        .join("iid-pre__iid-rel.json");
    arm_expected_wav(&post, "b127-release");
    assert!(post.keep(), "keep succeeds");
    assert!(res_path.exists(), "keep が O_EXCL reservation 枠を作る");
    post.stop();
    assert!(
        !res_path.exists(),
        "stop が reservation 枠を解放する（再予約可）"
    );

    drop(post);
    let _ = std::fs::remove_dir_all(home.parent().unwrap());
}

/// Drop / DAW unload / offline bounce 終了でも reservation 枠が残らない。
#[test]
#[ignore = "slow: HOME/TMPDIR + io_thread filesystem; reservation release on drop (sets env)"]
fn b127_reservation_released_on_drop() {
    let (home, tmp) = b127_isolate("release-drop");
    let puid = "puid-b127-rel-drop";
    let base = home.join("Library/Application Support/Kirin OS/plugin_data");

    let _pre = b127_spawn_pre(puid, &tmp);

    let post = KirinHyphaEngine::new(SR, 2);
    post.set_license(0);
    post.set_identity("iid-rel-drop".into(), puid.into(), "".into(), "mix".into());
    post.enable_post_writes();
    post.set_signal_state(1);
    post.set_pair_target("mix".to_string());
    for _ in 0..8 {
        post.push_samples(&[], 2);
        sleep(Duration::from_millis(40));
    }

    let res_path = base
        .join(puid)
        .join("record_reservation")
        .join("iid-pre__iid-rel-drop.json");
    arm_expected_wav(&post, "b127-release-drop");
    assert!(post.keep(), "keep succeeds");
    assert!(res_path.exists(), "keep creates the reservation frame");
    drop(post);
    assert!(
        !res_path.exists(),
        "Drop must release the reservation frame to avoid stale 12-cap lockout"
    );

    let _ = std::fs::remove_dir_all(home.parent().unwrap());
}

/// (iv): 12 distinct pairing（ここでは 12 lone POST marker = 12 lone pairing）で 13 ペア目の keep が
/// engine で hard reject され、Record に入らず R-28 通知（record_error_message）に出る（silent でない）。
/// keep_all / 単一 keep / broadcast 受信は全て resolve_and_enter_keep を通るため authoritative cap を実証。
#[test]
#[ignore = "slow: HOME/TMPDIR + io_thread filesystem; engine 12-pair enforcement (sets env)"]
fn b127_engine_caps_at_twelve_and_notifies() {
    let (home, tmp) = b127_isolate("cap12");
    let puid = "puid-b127-cap";
    let base = home.join("Library/Application Support/Kirin OS/plugin_data");

    let _pre = b127_spawn_pre(puid, &tmp); // 生かし続ける（arm 先 "mix"）。
    b127_make_frames(&base, puid, 12); // cap ちょうど（12 枠 = 12 pairing / 真実源 = 枠存在）。

    let post = KirinHyphaEngine::new(SR, 2);
    post.set_license(0);
    post.set_identity("iid-13".into(), puid.into(), "".into(), "mix".into());
    post.enable_post_writes();
    post.set_signal_state(1);
    post.set_pair_target("mix".to_string());
    for _ in 0..8 {
        post.push_samples(&[], 2);
        sleep(Duration::from_millis(40));
    }

    arm_expected_wav(&post, "b127-cap12");
    let entered = post.keep();
    assert!(
        !entered,
        "13th keep must be rejected at the 12-cap (engine-enforced)"
    );
    assert!(!post.is_recording(), "engine must NOT enter Record at cap");
    assert_eq!(
        post.record_error_message().as_deref(),
        Some("Maximum 12 pairs reached"),
        "cap rejection must notify (R-28: not silent)"
    );

    drop(post);
    let _ = std::fs::remove_dir_all(home.parent().unwrap());
}

/// 11 distinct pairing（< cap）では 12 ペア目の keep が通り Record に入る（`> MAX` 判定で 12 は許容・
/// 13 で reject）。成功 enter は stale 通知を消す。
#[test]
#[ignore = "slow: HOME/TMPDIR + io_thread filesystem; under-cap keep succeeds (sets env)"]
fn b127_engine_allows_keep_under_cap() {
    let (home, tmp) = b127_isolate("under11");
    let puid = "puid-b127-under";
    let base = home.join("Library/Application Support/Kirin OS/plugin_data");

    let _pre = b127_spawn_pre(puid, &tmp);
    b127_make_frames(&base, puid, 11); // < cap（11 枠 = 11 pairing）。

    let post = KirinHyphaEngine::new(SR, 2);
    post.set_license(0);
    post.set_identity("iid-12".into(), puid.into(), "".into(), "mix".into());
    post.enable_post_writes();
    post.set_signal_state(1);
    post.set_pair_target("mix".to_string());
    for _ in 0..8 {
        post.push_samples(&[], 2);
        sleep(Duration::from_millis(40));
    }

    arm_expected_wav(&post, "b127-under11");
    let entered = post.keep();
    assert!(entered, "12th keep (< cap) must succeed");
    wait_until_recording(&post, "b127-under11");
    assert!(post.is_recording(), "engine enters Record under cap");
    assert_eq!(
        post.record_error_message(),
        None,
        "successful enter clears any stale notice"
    );

    drop(post);
    let _ = std::fs::remove_dir_all(home.parent().unwrap());
}

/// 12 個の古い孤児 reservation が残っていても、Keep 直前/起動時 sweep で回収してから
/// concrete pairing を予約するため、実在ペアが 12 未満なら Keep は通る。
#[test]
#[ignore = "slow: HOME/TMPDIR + io_thread filesystem; stale 12 reservation sweep before keep (sets env)"]
fn b127_keep_sweeps_stale_orphan_frames_before_cap() {
    let (home, tmp) = b127_isolate("stale12");
    let puid = "puid-b127-stale";
    let base = home.join("Library/Application Support/Kirin OS/plugin_data");

    let _pre = b127_spawn_pre(puid, &tmp);
    let post = KirinHyphaEngine::new(SR, 2);
    post.set_license(0);
    post.set_identity(
        "iid-stale-keep".into(),
        puid.into(),
        "".into(),
        "mix".into(),
    );
    post.enable_post_writes();
    post.set_signal_state(1);
    post.set_pair_target("mix".to_string());
    for _ in 0..8 {
        post.push_samples(&[], 2);
        sleep(Duration::from_millis(40));
    }

    let stale_dir = base.join(puid).join("record_reservation");
    std::fs::create_dir_all(&stale_dir).unwrap();
    for i in 0..12 {
        let pre = format!("stale-pre-{i}");
        let post_iid = format!("stale-post-{i}");
        std::fs::write(
            stale_dir.join(format!("{pre}__{post_iid}.json")),
            format!(
                r#"{{"pre_instance_id":"{pre}","post_instance_id":"{post_iid}","reserved_at":"2026-01-01T00:00:00Z"}}"#
            ),
        )
        .unwrap();
    }
    assert_eq!(kirin_measure::reservation::count_frames(&base, puid), 12);

    arm_expected_wav(&post, "b127-stale12");
    assert!(
        post.keep(),
        "stale orphan frames must not block a valid keep after sweep"
    );
    wait_until_recording(&post, "b127-stale12");
    assert!(post.is_recording());
    assert_eq!(
        kirin_measure::reservation::count_frames(&base, puid),
        1,
        "sweep removes 12 stale orphans and keep leaves only the concrete current pairing"
    );

    drop(post);
    let _ = std::fs::remove_dir_all(home.parent().unwrap());
}
