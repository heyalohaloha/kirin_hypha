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
/// realtime 駆動で遅いため既定 `cargo test` から除外（`--ignored` で実行）。
#[test]
#[ignore = "slow: realtime ~12s record drive"]
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

// ── Phase 3b: enable_pre_writes → io_thread_pre が plugin_data を書く ──────────

/// `parent_name/*.{suffix}` を再帰探索し最初の 1 件を返す（parent dir 名で絞る）。
fn find_json_under(root: &std::path::Path, parent_name: &str, file_name: &str) -> Option<std::path::PathBuf> {
    let rd = std::fs::read_dir(root).ok()?;
    for e in rd.flatten() {
        let p = e.path();
        if p.is_dir() {
            if let Some(found) = find_json_under(&p, parent_name, file_name) {
                return Some(found);
            }
        } else {
            let fname = p.file_name().and_then(|n| n.to_str());
            let pname = p.parent().and_then(|pp| pp.file_name()).and_then(|n| n.to_str());
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

/// enable_pre_writes → Record セッションで `{ph}/{iid}/pre/{wall}.json`（永続）と
/// Watch `pre.json`（揮発）が io_thread_pre により書かれることを検証する。
/// HOME/TMPDIR を temp に差し替えて分離（io_thread spawn 前に設定し、テスト中変更しない）。
/// realtime 駆動で遅いため既定 `cargo test` から除外（`--ignored` で実行）。
#[test]
#[ignore = "slow: realtime ~12s record + io_thread filesystem writes (sets HOME/TMPDIR)"]
fn pre_writes_records_plugin_data_json() {
    use kirin_measure::plugin_data::{PluginDataFile, Role, Status};

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
        aggregates = engine.poll_session().expect("poll_session Some after record");

        // Watch pre.json は engine 生存中に存在（Drop で削除される前に確認）。
        watch_pre_exists = find_json_under(&watch_root, "", "pre.json").is_some();

        // exit → io_thread が Record→Watch を検出し writer_close(status=closed) するのを待つ。
        engine.exit_record();
        sleep(Duration::from_millis(900));

        // Record 永続 .json（{ph}/{iid}/pre/{wall}.json）を読み戻す。
        let rec = find_json_under(&plugin_data_root, "pre", "*.json")
            .expect("Record pre/{wall}.json must exist after record");
        eprintln!("[pre write] record file = {}", rec.display());
        let content = std::fs::read_to_string(&rec).unwrap();
        let pd: PluginDataFile =
            serde_json::from_str(&content).expect("written file deserializes as PluginDataFile");

        // schema 検証（SCHEMA_VERSION 1.3 / role=PRE / status active→closed）。
        assert_eq!(pd.schema_version, "1.3", "schema_version");
        assert_eq!(pd.role, Role::Pre, "role must be PRE");
        assert_eq!(pd.status, Status::Closed, "exit+flush 後は status=closed");
        assert!(!pd.frames.is_empty(), "frames[] non-empty");
        let f0 = &pd.frames[0];
        assert!(f0.lufs_m.is_finite() && f0.true_peak.is_finite() && f0.crest.is_finite(),
            "Frame.{{lufs_m,true_peak,crest}} finite");

        // aggregates 一致: io_thread の set_session_aggregates(=poll_session と同じ
        // session_summary 経路) で焼いた lufs_i が、poll_session の値と 1 桁丸め内で一致。
        let pd_li = pd.lufs_i.expect("json lufs_i present");
        let a_li = aggregates.lufs_i.expect("poll lufs_i present");
        eprintln!("[pre write] aggregates lufs_i json={pd_li} poll={a_li} | lra={:?} plr={:?}", pd.lra, pd.plr);
        assert!((pd_li - a_li).abs() < 0.06, "lufs_i json={pd_li} vs poll={a_li}（1 桁丸め内）");
    } // engine Drop → io_thread shutdown→join（Watch pre.json/instance dir 後始末）。

    assert!(watch_pre_exists, "Watch pre.json が稼働中に書かれること");

    // 後始末（panic 経路でも temp を残さない）。
    let _ = std::fs::remove_dir_all(&test_root);
}

// ── Phase 3c: state chunk identity get/set + annotation ──────────────────────

/// C 文字列バッファ（null 終端）を Rust String へ。
fn cbuf_to_string(buf: &[std::os::raw::c_char]) -> String {
    let bytes: Vec<u8> = buf.iter().take_while(|&&c| c != 0).map(|&c| c as u8).collect();
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
        ("iid-x".to_string(), "puid-y".to_string(), "dsid-z".to_string(), "mix1".to_string())
    );
    // 別エンジンに同一 set_identity → 同一 get_identity（決定性）。
    assert_eq!(got, read_back("iid-x", "puid-y", "dsid-z", "mix1"));
}

/// add_annotation は Os 以外（既定 Unknown / Sense）で false（二重 gate / can_write_plugin_data）。
#[test]
fn add_annotation_denied_without_os() {
    let engine = KirinHyphaEngine::new(SR, 2);
    assert!(!engine.add_annotation("x".to_string()), "既定 Unknown では false");
    engine.set_license(1); // Sense
    assert!(!engine.add_annotation("x".to_string()), "Sense でも false");
}

/// set_identity の project_uuid/instance_id が plugin_data path を決める（復元再現）こと、
/// add_annotation が Record の .json に memo を追記し、非 Os では追記されないこと（gate）。
#[test]
#[ignore = "slow: realtime ~12s record + io_thread filesystem writes (sets HOME/TMPDIR)"]
fn set_identity_drives_path_and_annotation() {
    use kirin_measure::plugin_data::PluginDataFile;

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
    let kirin_os = home.join("Library/Application Support/Kirin OS");
    std::fs::create_dir_all(&kirin_os).unwrap();
    std::fs::write(
        kirin_os.join("identity.json"),
        r#"{"schema_version":"1.0","installation_id":"b058-test","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-08T00:00:00Z","last_verified_at":"2026-06-08T00:00:00Z"}"#,
    )
    .unwrap();

    let plugin_data_root = home.join("Library/Application Support/Kirin OS/plugin_data");
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
        engine.exit_record();
        sleep(Duration::from_millis(900));

        // path = {known_puid}/{known_iid}/pre/ （set_identity が path を決める = 復元再現）。
        let rec = find_json_under(&plugin_data_root, "pre", "*.json").expect("record .json exists");
        let rec_s = rec.to_string_lossy().to_string();
        eprintln!("[3c] record path = {rec_s}");
        assert!(rec_s.contains(known_puid), "path に set した project_uuid を使う: {rec_s}");
        assert!(rec_s.contains(known_iid), "path に set した instance_id を使う: {rec_s}");

        // Note: Os + Record close 後に add_annotation → annotations[] に memo。
        assert!(engine.add_annotation("note-A".to_string()), "Os の add_annotation は true");
        let pd: PluginDataFile =
            serde_json::from_str(&std::fs::read_to_string(&rec).unwrap()).unwrap();
        assert_eq!(pd.annotations.len(), 1, "annotation が 1 件追記される");
        assert_eq!(pd.annotations[0].memo, "note-A");

        // gate: Sense へ降格 → add_annotation false・annotations 不変。
        engine.set_license(1);
        assert!(!engine.add_annotation("note-B".to_string()), "Sense の add_annotation は false");
        let pd2: PluginDataFile =
            serde_json::from_str(&std::fs::read_to_string(&rec).unwrap()).unwrap();
        assert_eq!(pd2.annotations.len(), 1, "Sense では追記されない（gate）");
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
        post.set_identity("iid-post".into(), "puid-post".into(), "".into(), "mix".into());
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
    eprintln!("[3d-a] delta mode={:?} lufs={:?} tp={:?} crest={:?}", delta.mode, delta.lufs, delta.tp, delta.crest);
    assert_eq!(delta.mode, DeltaMode::Active, "PRE 一意・Active・fresh → Δ Active");
    let dl = delta.lufs.expect("delta lufs Some");
    assert!((-8.0..-4.0).contains(&dl), "Δ_lufs ≈ -6（POST 半振幅）, got {dl}");
    assert!(post_pre_state_found, "post.json pre_signal_state=active（PRE 発見の観測証拠）");

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
        post.set_identity("iid-post".into(), "puid-post".into(), "".into(), "mix".into());
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
        assert!(!post.keep(), "不一致名は keep=false（write_pending しない）");
        assert!(!post.is_recording(), "keep false で Record しない");

        // 成功: "mix"（pair target setter で別名選択が効く）→ keep true。
        post.set_pair_target("mix".into());
        assert!(post.keep(), "一意 PRE 'mix' で keep=true");
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
            assert_eq!(s.status, SignalStatus::Released, "Stop で record_signal=released");
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
        post.set_identity("iid-post".into(), "puid-post".into(), "".into(), "mix".into());
        post.enable_post_writes();
        post.set_signal_state(1);
        post.set_pair_target("mix".into());

        // realtime ブロック投入ヘルパ（PRE フル / POST 半）。
        let drive = |secs: f64| {
            let ticks = (secs / 0.1) as usize;
            for _ in 0..ticks {
                pre.push_samples(&pre_block, 2);
                post.push_samples(&post_block, 2);
                sleep(dt);
            }
        };

        // 1) PRE pre.json を active+fresh にする（POST が select できる状態）。
        drive(1.5);

        // 2) POST Keep → PRE が discover→ack→自動 Record（1s discover + 1s poll throttle）。
        assert!(post.keep(), "一意 PRE 'mix' で keep=true");
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

    // 5) 両 Record .json を読み戻す（PRE は effective_project_hash=puid-post を adopt）。
    let pre_json = find_json_under(&plugin_data_root, "pre", "*.json")
        .expect("PRE Record pre/{wall}.json exists");
    let post_json = find_json_under(&plugin_data_root, "post", "*.json")
        .expect("POST Record post/{wall}.json exists");
    eprintln!("[capstone] PRE  = {}", pre_json.display());
    eprintln!("[capstone] POST = {}", post_json.display());

    // 両者が同一 project_uuid(=POST の puid-post) 配下に揃う（PRE が adopt）。
    assert!(pre_json.to_string_lossy().contains("puid-post"), "PRE は POST の uuid 配下に Record: {}", pre_json.display());
    assert!(post_json.to_string_lossy().contains("puid-post"), "POST は自 uuid 配下: {}", post_json.display());

    let pre_pd: PluginDataFile = serde_json::from_str(&std::fs::read_to_string(&pre_json).unwrap()).unwrap();
    let post_pd: PluginDataFile = serde_json::from_str(&std::fs::read_to_string(&post_json).unwrap()).unwrap();

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
    assert_eq!(pre_pd.paired_post_instance_id.as_deref(), Some("iid-post"), "PRE.paired_post_instance_id == POST");
    assert_eq!(post_pd.paired_pre_instance_id.as_deref(), Some("iid-pre"), "POST.paired_pre_instance_id == PRE");

    // Δ 物理妥当: POST 半振幅 → -6.02dB 相当。
    assert!((-8.0..-4.0).contains(&delta_lufs), "Δ_lufs ≈ -6（POST 半振幅）, got {delta_lufs}");

    let _ = std::fs::remove_dir_all(&test_root);
}

// ── B-066 / F2: keep() が try_enter_record 成功後に失敗した時の rollback ───────────

/// keep() が PRE 選定 + try_enter_record 成功の**後**で StoragePaths 解決に失敗した時、
/// 本 keep が行った Record 遷移を巻き戻して false を返す（record_sm が Record で残らない）。
/// HOME を一時的に外して StoragePaths::default_macos() を強制失敗させる（⑤経路）。
/// positive control（HOME 復帰で同 PRE が keep 成功）により、失敗時に select=Some まで
/// 到達していた＝②の select-None 早期 return ではなく⑤の post-enter 失敗だったことを立証。
#[test]
#[ignore = "slow: PRE+POST co-located; forces StoragePaths failure to test keep() rollback (sets HOME/TMPDIR)"]
fn keep_failure_after_enter_reverts_record_state() {
    let test_root = std::env::temp_dir()
        .join("kirin_b066_test")
        .join(format!("pid{}", std::process::id()));
    let home = test_root.join("home");
    let tmp = test_root.join("tmp");
    let _ = std::fs::remove_dir_all(&test_root);
    std::fs::create_dir_all(&home).unwrap();
    std::fs::create_dir_all(&tmp).unwrap();
    std::env::set_var("HOME", &home);
    std::env::set_var("TMPDIR", &tmp);
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
        post.set_identity("iid-post".into(), "puid-post".into(), "".into(), "mix".into());
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

        // ── 失敗ケース: HOME を外して StoragePaths::default_macos() を強制失敗（⑤経路）──
        std::env::remove_var("HOME");
        let kept_fail = post.keep();
        std::env::set_var("HOME", &home); // 後続/cleanup 用に即復帰。

        assert!(!kept_fail, "StoragePaths 失敗 → keep()=false");
        assert!(
            !post.is_recording(),
            "F2 fix: keep() 失敗時に record_sm を Watch へ巻き戻す（Record で残さない）"
        );

        // ── positive control: HOME 復帰で同 PRE が keep 成功 → 上の失敗は select=Some 後
        //    （= ⑤ post-enter 失敗）だったと立証。失敗とこの control の間に sleep を挟まない。
        assert!(
            post.keep(),
            "HOME 復帰: 同 PRE が選定可 → keep()=true（失敗経路が select 到達済を立証）"
        );
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
        post.set_identity("iid-post".into(), "puid-post".into(), "".into(), "mix".into());
        post.enable_post_writes();
        post.set_signal_state(1);
        post.set_pair_target("mix".into());

        let bf = SR as usize / 10;
        let bl = bf * 2;
        let dt = Duration::from_secs_f64(bf as f64 / SR as f64);
        let blk = gen_stereo_f32(0.1);
        let drive = |secs: f64| {
            for _ in 0..((secs / 0.1) as usize) {
                pre.push_samples(&blk, 2);
                post.push_samples(&blk, 2);
                sleep(dt);
            }
        };
        let _ = bl;

        drive(1.5); // PRE pre.json active
        assert!(post.keep(), "keep true");
        drive(4.0); // PRE acks + both record frames
        post.stop();
        drive(2.0); // PRE closes; both Record .json status=closed

        // Record close 後に POST へ注釈（header 注: 確実なのは close 後）。
        assert!(
            post.add_annotation("post-note".into()),
            "F3 fix: POST add_annotation が POST role の Record .json を見つけて true"
        );
    }

    // POST .json（post/）に注釈が入ったこと。
    let post_json =
        find_json_under(&plugin_data_root, "post", "*.json").expect("POST Record post/.json");
    eprintln!("[B-067] POST = {}", post_json.display());
    let post_pd: PluginDataFile =
        serde_json::from_str(&std::fs::read_to_string(&post_json).unwrap()).unwrap();
    assert!(
        post_pd.annotations.iter().any(|a| a.memo == "post-note"),
        "annotation が POST role(.json) に入る（PRE 固定をやめた）"
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
    assert_eq!(kirin_hypha_ffi::kirin_hypha_load_license(), 0, "license=os → 0");

    write_license("sense");
    assert_eq!(kirin_hypha_ffi::kirin_hypha_load_license(), 1, "license=sense → 1");

    // 不明値 → Unknown(2)。
    write_license("bogus");
    assert_eq!(kirin_hypha_ffi::kirin_hypha_load_license(), 2, "unknown value → 2");

    // ファイル不在 → Unknown(2)。
    std::fs::remove_file(&id_path).unwrap();
    assert_eq!(kirin_hypha_ffi::kirin_hypha_load_license(), 2, "missing file → 2");

    let _ = std::fs::remove_dir_all(&test_root);
}
