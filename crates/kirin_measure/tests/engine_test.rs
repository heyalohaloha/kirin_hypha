//! kirin_measure engine 精度テスト（guardian_53 T-3 判断基準）。
//!
//! テスト信号定義（guardian_53 §8）:
//! - S-1: 1kHz sine, -6dBFS, 48kHz, stereo, 10秒
//! - S-4: 1kHz sine, -6dBFS, 48kHz, stereo, 48000サンプル（1秒）
//!         Crest 理論値: 3.01 ± 0.05 dB（サンプル点一致条件）

use kirin_measure::{MeasureEngine, N_CHANNELS};
use std::f64::consts::PI;

const SAMPLE_RATE: u32 = 48000;

/// 1kHz sine wave を stereo インターリーブ f64 で生成する。
/// amplitude: 1.0 = 0dBFS
fn gen_sine_interleaved(freq: f64, amplitude: f64, n_frames: usize, sample_rate: u32) -> Vec<f64> {
    let sr = sample_rate as f64;
    let mut out = Vec::with_capacity(n_frames * N_CHANNELS);
    for i in 0..n_frames {
        let s = amplitude * (2.0 * PI * freq * i as f64 / sr).sin();
        out.push(s); // L
        out.push(s); // R
    }
    out
}

/// -6dBFS の振幅（linear）
fn amplitude_minus_6dbfs() -> f64 {
    10.0_f64.powf(-6.0 / 20.0)
}

/// テストを通じてエンジンにサンプルをフィードし、最終結果を得る。
fn feed_and_get_last(engine: &mut MeasureEngine, samples: &[f64]) -> Option<kirin_measure::MeasureResult> {
    // 4096 サンプル単位でフィードして 100ms チャンクを処理させる
    let chunk_size = 4096 * N_CHANNELS;
    let mut last = None;
    let mut offset = 0;
    while offset < samples.len() {
        let end = (offset + chunk_size).min(samples.len());
        if let Some(r) = engine.push(&samples[offset..end]) {
            last = Some(r);
        }
        offset = end;
    }
    last
}

// ── T-3 合格基準: LUFS-M ────────────────────────────────────────────────────

/// S-1: 1kHz sine -6dBFS 10秒。
/// LUFS-M の絶対精度テストは EBU R128 テスト信号（S-2/S-3）が本番。
/// ここでは「値が返ること」と「おおむね妥当な範囲」を確認する。
#[test]
fn test_lufs_m_returns_finite_for_1khz_sine() {
    let mut engine = MeasureEngine::new(SAMPLE_RATE, N_CHANNELS).expect("init");
    let amp = amplitude_minus_6dbfs();
    // 10秒分（400ms ウィンドウが埋まるのに十分）
    let samples = gen_sine_interleaved(1000.0, amp, SAMPLE_RATE as usize * 10, SAMPLE_RATE);
    let result = feed_and_get_last(&mut engine, &samples).expect("should get result");

    let lufs_m = result.lufs_m.expect("LUFS-M should be Some after 10s");
    assert!(
        lufs_m.is_finite(),
        "LUFS-M must be finite, got {}", lufs_m
    );
    // 1kHz sine -6dBFS の LUFS-M はおおむね -7 〜 -5 LUFS の範囲に収まる（粗い範囲チェック）
    assert!(
        (-10.0..0.0).contains(&lufs_m),
        "LUFS-M out of expected range: {:.2} LUFS", lufs_m
    );
}

// ── T-3 合格基準: Crest Factor ──────────────────────────────────────────────

/// S-4: 1kHz sine -6dBFS 48000サンプル（1秒）。
/// 理論値: 3.01 ± 0.05 dB（guardian_53 G-53-03）。
///
/// 正弦波の Crest Factor 理論値:
///   peak = A, RMS = A / sqrt(2)
///   Crest = 20*log10(A) - 20*log10(A/sqrt(2)) = 20*log10(sqrt(2)) ≈ 3.0103 dB
#[test]
fn test_crest_factor_1khz_sine_1sec() {
    let mut engine = MeasureEngine::new(SAMPLE_RATE, N_CHANNELS).expect("init");
    let amp = amplitude_minus_6dbfs();
    // 1秒（guardian_53 S-4: 48000 サンプル、サンプル点一致条件）
    // 整数周期で切り出す: 1kHz @ 48kHz → 48サンプル/周期 → 1000周期 = 48000サンプル
    let samples = gen_sine_interleaved(1000.0, amp, 48000, SAMPLE_RATE);
    let result = feed_and_get_last(&mut engine, &samples).expect("should get result");

    let crest = result.crest.expect("Crest should be Some after 1s");
    let theory = 20.0 * 2.0_f64.sqrt().log10(); // ≈ 3.0103 dB
    let diff = (crest - theory).abs();
    assert!(
        diff <= 0.05,
        "Crest Factor: got {:.4} dB, theory {:.4} dB, diff {:.4} dB (limit 0.05 dB)",
        crest, theory, diff
    );
}

// ── T-3 合格基準: True Peak ──────────────────────────────────────────────────

/// True Peak が返ること、かつ -6dBFS の sine に対して概ね -6 dBTP 付近であることを確認。
/// 正確な精度ゲート（0.005 dBTP 目標）は Step 2（T-7後）でDaisuke提供信号で実施。
#[test]
fn test_true_peak_returns_finite() {
    let mut engine = MeasureEngine::new(SAMPLE_RATE, N_CHANNELS).expect("init");
    let amp = amplitude_minus_6dbfs();
    let samples = gen_sine_interleaved(1000.0, amp, SAMPLE_RATE as usize * 5, SAMPLE_RATE);
    let result = feed_and_get_last(&mut engine, &samples).expect("should get result");

    let tp = result.true_peak.expect("True Peak should be Some");
    assert!(tp.is_finite(), "True Peak must be finite, got {}", tp);
    // -6dBFS 正弦波の TP はサンプル補間により -6dBTP より若干高くなることがある
    // 粗い範囲チェック: -10 〜 0 dBTP
    assert!(
        (-10.0..0.0).contains(&tp),
        "True Peak out of expected range: {:.2} dBTP", tp
    );
}

/// True Peak 精度診断: -6dBFS sine の TP が ±0.5 dBTP 以内であることを確認。
/// guardian_53 T-3 精度要件の検証用。実際の値を eprintln で出力する。
#[test]
fn test_true_peak_accuracy_1khz_minus6dbfs() {
    let mut engine = MeasureEngine::new(SAMPLE_RATE, N_CHANNELS).expect("init");
    let amp = amplitude_minus_6dbfs(); // ≈ 0.5012
    let samples = gen_sine_interleaved(1000.0, amp, SAMPLE_RATE as usize * 5, SAMPLE_RATE);
    let result = feed_and_get_last(&mut engine, &samples).expect("should get result");

    let tp = result.true_peak.expect("True Peak should be Some");
    let lufs = result.lufs_m;
    let crest = result.crest;
    eprintln!(
        "[TP診断] 1kHz -6dBFS sine (5s): TP={:.3} dBTP, LUFS-M={:?}, Crest={:?}",
        tp, lufs, crest
    );

    // 理論値: -6.02 dBTP（1kHz sine は 4×補間でほぼそのまま）
    // 許容誤差 ±0.5 dBTP
    let expected = -6.02_f64;
    let diff = (tp - expected).abs();
    assert!(
        diff <= 0.5,
        "True Peak accuracy failed: got {:.3} dBTP, expected {:.3} dBTP (diff {:.3}, limit 0.5)",
        tp, expected, diff
    );
}

/// True Peak ウィンドウ動作確認: 大きな信号の後に小さな信号を流したとき、
/// TP が現在の信号レベルに追従することを確認（running max 問題の回帰テスト）。
#[test]
fn test_true_peak_follows_current_signal_not_running_max() {
    let mut engine = MeasureEngine::new(SAMPLE_RATE, N_CHANNELS).expect("init");

    // まず 0dBFS に近い大きな信号を 1 秒流す（TP が高くなる）
    let loud_amp = 0.95_f64; // ≈ -0.45 dBFS
    let loud = gen_sine_interleaved(1000.0, loud_amp, SAMPLE_RATE as usize, SAMPLE_RATE);
    feed_and_get_last(&mut engine, &loud);

    // 次に -6dBFS の静かな信号を 2 秒流す（400ms ウィンドウが切り替わるのに十分）
    let quiet_amp = amplitude_minus_6dbfs();
    let quiet = gen_sine_interleaved(1000.0, quiet_amp, SAMPLE_RATE as usize * 2, SAMPLE_RATE);
    let result = feed_and_get_last(&mut engine, &quiet).expect("should get result");

    let tp = result.true_peak.expect("True Peak should be Some");
    eprintln!(
        "[TP ウィンドウ] loud→quiet 後の TP: {:.3} dBTP（期待: -6.0 付近）",
        tp
    );

    // running max のままなら loud の値（≈-0.45）が残るが、ウィンドウ実装なら -6 付近になる
    assert!(
        tp < -5.0,
        "True Peak should track current signal (-6dBFS), not old loud signal. got {:.3} dBTP",
        tp
    );
}

/// transport 停止 → 再開でピークが失効することを確認。2 パターン検証:
///
/// A) タイムスタンプ自然失効: 400ms 超スリープ後に再開すると古い tp_window エントリが
///    実時間フィルタで除外される。ただし ebur128 の FIR フィルタ遅延ライン（12 タップ）
///    の状態は残るため、最初のチャンクで一時的にピークが高くなる可能性がある。
///    このため複数チャンク後の値で判定する（8 チャンク = 800ms で旧エントリが全失効）。
///
/// B) reset() 呼び出し: transport 検出で reset() を呼ぶと ebur128 の FIR 状態も含め
///    完全クリアされ、再開直後から正確な値が返ることを確認する。
#[test]
fn test_true_peak_expires_after_transport_stop() {
    // ── A) タイムスタンプ自然失効 ────────────────────────────────────────────
    {
        let mut engine = MeasureEngine::new(SAMPLE_RATE, N_CHANNELS).expect("init");

        // 大きな信号（TP ≈ -0.45 dBTP）
        let loud_amp = 0.95_f64;
        let loud = gen_sine_interleaved(1000.0, loud_amp, SAMPLE_RATE as usize, SAMPLE_RATE);
        let r = feed_and_get_last(&mut engine, &loud).expect("result");
        let tp_before_stop = r.true_peak.expect("TP before stop");
        eprintln!("[A: 自然失効] 停止前 TP: {:.3} dBTP", tp_before_stop);
        assert!(tp_before_stop > -2.0, "loud signal TP should be high before stop");

        // transport 停止: 400ms 超スリープ → 全 tp_window エントリが実時間失効する
        std::thread::sleep(std::time::Duration::from_millis(450));

        // transport 再開: -6dBFS を 1 秒（10 チャンク）流す。
        // 最初の数チャンクは FIR フィルタ遷移アーティファクトで高くなる可能性があるが、
        // 8 チャンク後（旧エントリがサイズ制限で押し出され、新エントリが全て -6dBFS 由来）
        // には確実に -6dBTP 付近になる。
        let quiet_amp = amplitude_minus_6dbfs();
        let quiet = gen_sine_interleaved(1000.0, quiet_amp, SAMPLE_RATE as usize, SAMPLE_RATE);
        let result = feed_and_get_last(&mut engine, &quiet).expect("result after restart");

        let tp_after = result.true_peak.expect("TP after restart");
        eprintln!("[A: 自然失効] 再開後 1 秒後 TP: {:.3} dBTP（期待: -6.0 付近）", tp_after);
        assert!(
            tp_after < -5.0,
            "After transport stop (>400ms) + 1s quiet, TP should track -6dBFS. got {:.3} dBTP",
            tp_after
        );
    }

    // ── B) reset() 呼び出し: FIR 含む完全クリア ──────────────────────────────
    {
        let mut engine = MeasureEngine::new(SAMPLE_RATE, N_CHANNELS).expect("init");

        // 大きな信号（TP ≈ -0.45 dBTP）
        let loud_amp = 0.95_f64;
        let loud = gen_sine_interleaved(1000.0, loud_amp, SAMPLE_RATE as usize, SAMPLE_RATE);
        feed_and_get_last(&mut engine, &loud);

        // transport 停止を検出 → reset() でエンジン完全クリア（FIR 状態含む）
        engine.reset();

        // transport 再開: -6dBFS を 400ms（4 チャンク）流す
        // reset() で FIR 状態がクリアされているため、最初のチャンクから正確な値が返る
        let quiet_amp = amplitude_minus_6dbfs();
        let quiet = gen_sine_interleaved(1000.0, quiet_amp, SAMPLE_RATE as usize * 2 / 5, SAMPLE_RATE);
        let result = feed_and_get_last(&mut engine, &quiet).expect("result after reset");

        let tp_after_reset = result.true_peak.expect("TP after reset");
        eprintln!("[B: reset()] 再開後 TP: {:.3} dBTP（期待: -6.0 付近）", tp_after_reset);
        assert!(
            tp_after_reset < -5.0,
            "After engine.reset() + quiet signal, TP should be -6dBFS. got {:.3} dBTP",
            tp_after_reset
        );
    }
}

// ── T-3 合格基準: 信号なし = None ──────────────────────────────────────────

/// 無音時（0 サンプル）は全項目 None を返すことを確認（GUI `---` 表示用）。
#[test]
fn test_silence_returns_none() {
    let mut engine = MeasureEngine::new(SAMPLE_RATE, N_CHANNELS).expect("init");
    // 無音（全サンプル 0）を 1 秒フィード
    let samples = vec![0.0_f64; SAMPLE_RATE as usize * N_CHANNELS];
    let result = feed_and_get_last(&mut engine, &samples);
    if let Some(r) = result {
        // LUFS-M は -inf になり finite フィルタで None になるはず
        assert!(r.lufs_m.is_none(), "LUFS-M should be None for silence");
        assert!(r.true_peak.is_none(), "True Peak should be None for silence");
        assert!(r.crest.is_none(), "Crest should be None for silence");
    }
    // result が None の場合は 100ms チャンクが揃わなかっただけなので OK
}

// ── サンプルレート変化テスト ────────────────────────────────────────────────

/// 44.1kHz でも初期化・計測できることを確認（guardian_53 T-3 判断基準）。
#[test]
fn test_sample_rate_44100() {
    let sr = 44100u32;
    let mut engine = MeasureEngine::new(sr, N_CHANNELS).expect("init at 44.1kHz");
    let amp = amplitude_minus_6dbfs();
    let samples = gen_sine_interleaved(1000.0, amp, sr as usize * 5, sr);
    let result = feed_and_get_last(&mut engine, &samples).expect("should get result");
    assert!(result.lufs_m.is_some(), "LUFS-M should be Some at 44.1kHz");
    assert!(result.true_peak.is_some(), "True Peak should be Some at 44.1kHz");
}

/// 96kHz でも初期化・計測できることを確認。
#[test]
fn test_sample_rate_96000() {
    let sr = 96000u32;
    let mut engine = MeasureEngine::new(sr, N_CHANNELS).expect("init at 96kHz");
    let amp = amplitude_minus_6dbfs();
    let samples = gen_sine_interleaved(1000.0, amp, sr as usize * 5, sr);
    let result = feed_and_get_last(&mut engine, &samples).expect("should get result");
    assert!(result.lufs_m.is_some(), "LUFS-M should be Some at 96kHz");
}

// ── 3dBずれ診断テスト ────────────────────────────────────────────────────────

/// TP 3dBずれ診断: ebur128 の prev_true_peak() raw 値を直接確認する。
///
/// ユーザー報告: ステレオ -14dBFS 997Hz で TP=-17.0 が出る。
/// -17.0 = 20*log10(A/√2) = RMS値。期待は -14.0 = 20*log10(A) = peak値。
///
/// このテストで prev_true_peak(ch) の生の返り値を表示して調べる。
#[test]
fn test_true_peak_3db_diagnosis() {
    use ebur128::{EbuR128, Mode};

    let sample_rate: u32 = 48000;
    let n_channels: usize = 2;
    let amplitude = 10.0_f64.powf(-14.0 / 20.0); // -14dBFS linear ≈ 0.1995
    let expected_tp_db = -14.0_f64;
    let expected_tp_linear = amplitude; // ≈ 0.1995

    println!("\n=== TP 3dB診断 ===");
    println!("振幅: {:.6} (= -14dBFS = {:.4} dBFS)", amplitude, 20.0 * amplitude.log10());

    // --- Case A: L=R 両チャンネル同じ信号（ユニットテストと同じ） ---
    {
        let mode = Mode::M | Mode::S | Mode::TRUE_PEAK;
        let mut ebu = EbuR128::new(n_channels as u32, sample_rate, mode).unwrap();

        // 100ms * 5 = 500ms 分
        let frames = sample_rate as usize / 10 * 5;
        let mut samples = Vec::with_capacity(frames * n_channels);
        for i in 0..frames {
            let s = amplitude * (2.0 * std::f64::consts::PI * 997.0 * i as f64 / sample_rate as f64).sin();
            samples.push(s); // L
            samples.push(s); // R (same as L)
        }

        // 100ms チャンクで投入
        let chunk = sample_rate as usize / 10 * n_channels;
        for c in samples.chunks(chunk) {
            ebu.add_frames_f64(c).unwrap();
        }

        let tp0 = ebu.prev_true_peak(0).unwrap();
        let tp1 = ebu.prev_true_peak(1).unwrap();
        let tp_db = 20.0 * tp0.max(tp1).log10();
        println!("\n[Case A] L=R 両ch同一 -14dBFS");
        println!("  prev_true_peak(L) linear = {:.6}", tp0);
        println!("  prev_true_peak(R) linear = {:.6}", tp1);
        println!("  TP (max ch) dBTP = {:.3}", tp_db);
        println!("  期待値: {:.3} dBTP", expected_tp_db);
        assert!((tp_db - expected_tp_db).abs() < 0.5,
            "Case A: TP {:.3} は期待値 {:.3} から±0.5以上ずれている", tp_db, expected_tp_db);
    }

    // --- Case B: L のみ信号、R=silent（EBU mono-on-stereo） ---
    {
        let mode = Mode::M | Mode::S | Mode::TRUE_PEAK;
        let mut ebu = EbuR128::new(n_channels as u32, sample_rate, mode).unwrap();

        let frames = sample_rate as usize / 10 * 5;
        let mut samples = Vec::with_capacity(frames * n_channels);
        for i in 0..frames {
            let s = amplitude * (2.0 * std::f64::consts::PI * 997.0 * i as f64 / sample_rate as f64).sin();
            samples.push(s); // L
            samples.push(0.0_f64); // R = silent
        }

        let chunk = sample_rate as usize / 10 * n_channels;
        for c in samples.chunks(chunk) {
            ebu.add_frames_f64(c).unwrap();
        }

        let tp0 = ebu.prev_true_peak(0).unwrap();
        let tp1 = ebu.prev_true_peak(1).unwrap();
        let tp_db = 20.0 * tp0.max(tp1).log10();
        println!("\n[Case B] L のみ -14dBFS、R=silent");
        println!("  prev_true_peak(L) linear = {:.6}", tp0);
        println!("  prev_true_peak(R) linear = {:.6}", tp1);
        println!("  TP (max ch) dBTP = {:.3}", tp_db);
        println!("  LUFS-M = {:.3}", ebu.loudness_momentary().unwrap_or(f64::NEG_INFINITY));
        println!("  期待値: {:.3} dBTP", expected_tp_db);
        assert!((tp_db - expected_tp_db).abs() < 0.5,
            "Case B: TP {:.3} は期待値 {:.3} から±0.5以上ずれている", tp_db, expected_tp_db);
    }

    // --- Case C: MeasureEngine 経由（実プラグインと同じパス） ---
    {
        let mut engine = MeasureEngine::new(sample_rate, n_channels).expect("init");

        let frames = sample_rate as usize / 10 * 5;
        let mut samples: Vec<f64> = Vec::with_capacity(frames * n_channels);
        for i in 0..frames {
            let s = amplitude * (2.0 * std::f64::consts::PI * 997.0 * i as f64 / sample_rate as f64).sin();
            samples.push(s); // L
            samples.push(0.0_f64); // R = silent
        }

        // f32 経由シミュレーション（ring buffer は f32）
        let samples_f32: Vec<f32> = samples.iter().map(|&x| x as f32).collect();
        let samples_f64_via_f32: Vec<f64> = samples_f32.iter().map(|&x| x as f64).collect();

        let result = feed_and_get_last(&mut engine, &samples_f64_via_f32).expect("result");
        let tp_db = result.true_peak.expect("TP must be Some");
        let lufs_m = result.lufs_m.unwrap_or(f64::NEG_INFINITY);

        println!("\n[Case C] MeasureEngine 経由 (f32→f64 変換あり) L のみ -14dBFS");
        println!("  TP = {:.3} dBTP", tp_db);
        println!("  LUFS-M = {:.3}", lufs_m);
        println!("  Crest  = {:.3}", result.crest.unwrap_or(f64::NAN));
        println!("  期待値: {:.3} dBTP", expected_tp_db);
        assert!((tp_db - expected_tp_db).abs() < 0.5,
            "Case C: TP {:.3} は期待値 {:.3} から±0.5以上ずれている (LUFS-M={:.3})", tp_db, expected_tp_db, lufs_m);
    }

    println!("\n=== 診断完了 ===");
}

/// フルパイプライン統合テスト: ring buffer → measure_thread → MeasureResult。
///
/// 実機と同じ経路（f32 ring buffer → f32→f64 変換 → engine.push()）で
/// TP が -14.0 dBTP（≠ -17.0）になることを確認する。
/// TP = -17 = LUFS-M が返る場合、measure_thread 内のデータ経路にバグがある。
#[test]
fn test_full_pipeline_tp_via_measure_thread() {
    use kirin_measure::{spawn_measure_thread, MeasureResult, SignalState};
    use std::sync::atomic::{AtomicBool, AtomicU32, AtomicU8, Ordering};
    use std::sync::{Arc, Mutex};
    use std::time::Duration;

    let sample_rate: u32 = 48000;
    let n_channels = N_CHANNELS;
    let amplitude = 10.0_f64.powf(-14.0 / 20.0); // -14dBFS

    // ring buffer 作成（実機と同じ容量: 2秒分）
    let capacity = sample_rate as usize * 2 * n_channels;
    let (mut producer, consumer) = rtrb::RingBuffer::<f32>::new(capacity);

    let result = Arc::new(Mutex::new(MeasureResult::default()));
    let signal_state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let shutdown = Arc::new(AtomicBool::new(false));
    let heartbeat = Arc::new(AtomicU32::new(0));

    // Measure Thread 起動（実機と同じ spawn_measure_thread）
    let handle = spawn_measure_thread(
        consumer,
        sample_rate,
        Arc::clone(&result),
        Arc::clone(&signal_state),
        Arc::clone(&shutdown),
        Arc::clone(&heartbeat),
    );

    // 997Hz -14dBFS L=R ステレオ信号を f32 で ring buffer に投入（1秒分）
    // DAW のバッファサイズ 512 frames を模倣
    let buffer_size = 512;
    let total_frames = sample_rate as usize; // 1秒
    let sr = sample_rate as f64;
    for frame_start in (0..total_frames).step_by(buffer_size) {
        let frame_end = (frame_start + buffer_size).min(total_frames);
        for i in frame_start..frame_end {
            let s = amplitude * (2.0 * PI * 997.0 * i as f64 / sr).sin();
            let s_f32 = s as f32;
            let _ = producer.push(s_f32); // L
            let _ = producer.push(s_f32); // R
        }
        // process() heartbeat を模倣（stall detection 回避）
        heartbeat.fetch_add(1, Ordering::Relaxed);
        // DAW の process() 間隔を模倣（~10ms @ 512 samples / 48kHz）
        std::thread::sleep(Duration::from_millis(1));
    }

    // Measure Thread が処理完了するまで待機。
    // heartbeat を更新し続けて stall detection を回避する（実機では process() が継続するため）。
    for _ in 0..10 {
        heartbeat.fetch_add(1, Ordering::Relaxed);
        std::thread::sleep(Duration::from_millis(50));
    }

    // 結果取得
    let m = result.lock().unwrap().clone();
    println!("\n=== フルパイプライン統合テスト ===");
    println!("  TP       = {:?}", m.true_peak.map(|v| format!("{:.3} dBTP", v)));
    println!("  LUFS-M   = {:?}", m.lufs_m.map(|v| format!("{:.3} LUFS", v)));
    println!("  Crest    = {:?}", m.crest.map(|v| format!("{:.3} dB", v)));

    // shutdown
    shutdown.store(true, Ordering::Relaxed);
    handle.join().expect("measure thread join");

    // 検証: TP ≈ -14.0 dBTP（NOT -17.0）
    let tp = m.true_peak.expect("TP must be Some after 1s signal");
    let lufs_m = m.lufs_m.expect("LUFS-M must be Some after 1s signal");

    println!("  TP = {:.3}, LUFS-M = {:.3}", tp, lufs_m);
    println!("  TP - LUFS-M = {:.3} (期待: ≈ 3.0)", tp - lufs_m);

    // TP は -14.0 付近であること（NOT -17.0）
    assert!(
        (tp - (-14.0)).abs() < 0.5,
        "TP must be ≈ -14.0 dBTP, got {:.3} (LUFS-M = {:.3}). If TP ≈ LUFS-M ≈ -17, the signal level is wrong.",
        tp, lufs_m
    );
    // L=R ステレオの場合: LUFS-M ≈ peak_dBFS（両chのパワー合算で RMS -3dB が相殺される）
    // つまり TP ≈ LUFS-M は L=R stereo では正常（≠バグ）
    // mono の場合のみ TP - LUFS-M ≈ 3 dB になる
    assert!(
        (tp - (-14.0)).abs() < 0.5 && (lufs_m - (-14.0)).abs() < 1.0,
        "L=R stereo: both TP ({:.3}) and LUFS-M ({:.3}) should be near -14.0",
        tp, lufs_m
    );
}
