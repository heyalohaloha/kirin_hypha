//! Record 時の PluginDataWriter 共通ライフサイクル（PRE / POST 共有）。
//!
//! 責務:
//! - Watch↔Record 遷移に合わせた writer の生成・破棄
//! - Phase D 計測結果を frame (10 fps) / PSB (2 fps) で追記
//! - 30 秒間隔で heartbeat + atomic flush
//!
//! PRE / POST どちらも同じロジックで動く。違いは Role の指定と、
//! state machine を駆動するトリガ（POST=GUI ボタン、PRE=record_signal.json poll）のみ。
//!
//! # t_ms 軸
//! `started_at_ms` は record_signal.json の `started_at` を epoch ms に変換したもの。
//! POST / PRE が同じ軸上で frame を並べるため、record_signal を単一真実として参照する。
//! 不在・パース失敗時は現在時刻にフォールバック（defensive）。

use std::path::PathBuf;
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use crate::plugin_data::{PluginDataWriter, Role, WriterPaths};
use crate::record::RecordStateMachine;
use crate::storage::{load_installation_id_safe, StoragePaths};
use crate::{record_signal, MeasureResult, BUS_PHASE1, PROJECT_HASH_PHASE1};

/// Frame サンプリング間隔（10 fps / G-50-17 リアルタイム Record）。
pub const FRAME_INTERVAL_MS: u64 = 100;

/// PSB スナップショット間隔（2 fps / G-50-17）。
pub const PSB_INTERVAL_MS: u64 = 500;

/// heartbeat + atomic flush 間隔（正本 30 秒）。
pub const FLUSH_INTERVAL: Duration = Duration::from_secs(30);

/// Record 中の writer + タイミング状態。
///
/// IO Thread ローカル（他スレッドと共有しない）。
pub struct RecordingCtx {
    pub(crate) writer: PluginDataWriter,
    pub final_path: PathBuf,
    /// Record 開始 wall-clock（epoch ms）。frame t_ms はこの値からの差分。
    pub started_at_ms: i64,
    /// 次に Frame を書く最小 t_ms。
    pub next_frame_ms: u64,
    /// 次に PSB を書く最小 t_ms。
    pub next_psb_ms: u64,
    /// 次に flush/heartbeat する Instant。
    pub next_flush: Instant,
    /// 最初の Frame 書込をログしたか（1 Record セッションにつき 1 回）。
    pub first_frame_logged: bool,
}

impl RecordingCtx {
    #[cfg(test)]
    pub fn data(&self) -> &crate::plugin_data::PluginDataFile {
        self.writer.data()
    }
}

/// 現在時刻を epoch ms で返す。
pub fn now_epoch_ms() -> i64 {
    chrono::Utc::now().timestamp_millis()
}

/// ISO 8601 / RFC 3339 文字列を epoch ms に変換。パース失敗時は `None`。
pub fn parse_iso8601_to_epoch_ms(s: &str) -> Option<i64> {
    if s.is_empty() {
        return None;
    }
    chrono::DateTime::parse_from_rfc3339(s)
        .ok()
        .map(|dt| dt.with_timezone(&chrono::Utc).timestamp_millis())
}

/// record_signal.json から `started_at` を読み取り epoch ms に変換。
///
/// 失敗時（ファイル不在・`started_at` 空・パース不能）は現在時刻を返し、
/// 警告ログを出す。t_ms = 0 起点にフォールバックすることで Record 続行可能。
pub fn resolve_started_at_ms(tmp_base: &std::path::Path, project_hash: &str, bus: &str) -> i64 {
    let signal_path = tmp_base
        .join(project_hash)
        .join(bus)
        .join("record_signal.json");
    match record_signal::read_signal(tmp_base, project_hash, bus) {
        Some(sig) => parse_iso8601_to_epoch_ms(&sig.started_at).unwrap_or_else(|| {
            log::warn!(
                "[signal] started_at missing, using now() as fallback ({})",
                signal_path.display()
            );
            now_epoch_ms()
        }),
        None => {
            log::warn!(
                "[signal] started_at missing, using now() as fallback ({})",
                signal_path.display()
            );
            now_epoch_ms()
        }
    }
}

/// Record 開始: `PluginDataWriter` を生成し、空ファイルで初回 flush する。
///
/// # 引数
/// - `role`: Pre / Post
/// - `sample_rate`: writer メタデータに埋め込む sample_rate
/// - `started_at_ms`: record_signal.started_at を epoch ms に変換したもの
///
/// # 失敗要因（いずれも `None` を返してログ記録）
/// - `$HOME` 未解決
/// - identity.json 不在 or `installation_id` フィールド欠落
/// - `PluginDataWriter::create` が IO エラー
/// - 初回 flush 失敗（tmp 書込 or rename）
pub fn writer_start(role: Role, sample_rate: u32, started_at_ms: i64) -> Option<RecordingCtx> {
    let paths = match StoragePaths::default_macos() {
        Ok(p) => p,
        Err(e) => {
            log::warn!("[writer] StoragePaths error: {:?}", e);
            return None;
        }
    };
    let base = paths.plugin_data_dir();
    let installation_id = load_installation_id_safe()?;
    let wall_clock_iso = chrono::Utc::now()
        .format("%Y-%m-%dT%H:%M:%SZ")
        .to_string();
    let writer_paths = WriterPaths::build(
        &base,
        PROJECT_HASH_PHASE1,
        BUS_PHASE1,
        role,
        &wall_clock_iso,
    );
    let final_path = writer_paths.final_path.clone();
    let mut w = match PluginDataWriter::create(
        writer_paths,
        installation_id,
        PROJECT_HASH_PHASE1.to_string(),
        role,
        BUS_PHASE1.to_string(),
        sample_rate,
    ) {
        Ok(w) => w,
        Err(e) => {
            log::warn!("[writer] create failed: {}", e);
            return None;
        }
    };
    if let Err(e) = w.flush() {
        log::warn!("[writer] initial flush failed: {}", e);
        return None;
    }
    log::info!(
        "[writer] created ({:?}): {} (started_at_ms={})",
        role,
        final_path.display(),
        started_at_ms
    );
    Some(RecordingCtx {
        writer: w,
        final_path,
        started_at_ms,
        next_frame_ms: 0,
        next_psb_ms: 0,
        next_flush: Instant::now() + FLUSH_INTERVAL,
        first_frame_logged: false,
    })
}

/// Record 終了: status=closed で最終 flush、ログ出力。
pub fn writer_close(ctx: RecordingCtx) {
    let final_path = ctx.final_path.clone();
    match ctx.writer.close() {
        Ok(()) => log::info!("[writer] released: {}", final_path.display()),
        Err(e) => log::warn!("[writer] close failed ({}): {}", final_path.display(), e),
    }
}

/// 必要な 5 フィールドが Some のときのみ 1 frame を追記。
/// Phase D warm-up 中はスキップ。戻り値: 追記=true / スキップ=false。
pub fn writer_append_frame(ctx: &mut RecordingCtx, t_ms: u64, m: &MeasureResult) -> bool {
    let (Some(n_prime), Some(sharpness), Some(lufs_m), Some(true_peak), Some(crest)) = (
        m.n_prime,
        m.sharpness,
        m.lufs_m,
        m.true_peak,
        m.crest,
    ) else {
        return false;
    };
    ctx.writer
        .append_frame(t_ms, n_prime, sharpness, lufs_m, true_peak, crest);
    if !ctx.first_frame_logged {
        log::info!(
            "[writer] frame written: t_ms={}, n_prime[0]={:.3}",
            t_ms, n_prime[0]
        );
        ctx.first_frame_logged = true;
    }
    true
}

/// PSB スナップショット追記。`psb_bark` が None ならスキップ。
pub fn writer_append_psb(ctx: &mut RecordingCtx, t_ms: u64, m: &MeasureResult) -> bool {
    let Some(psb_bark) = m.psb_bark else {
        return false;
    };
    ctx.writer.append_psb(t_ms, psb_bark, true);
    true
}

/// Record モード 1 ティック: Watch↔Record 遷移と Frame/PSB 追記を処理する。
///
/// - 毎ループ呼ばれる（100ms 間隔）
/// - (false → true) 遷移 → `writer_start`（started_at は record_signal から取得）
/// - (true → false) 遷移 → `writer_close`
/// - Record 継続 → `next_frame_ms` / `next_psb_ms` に沿って append
/// - 30 秒経過 → `heartbeat_now` + `flush`
///
/// `record_sm` は外部（GUI ボタン / record_signal poller）が駆動する。
/// 個々の append / flush 失敗は `Err` として戻し、呼出元が warn ログ出力。
/// writer を破棄せず継続することで transient な FS エラーに強くなる。
pub fn run_record_tick(
    record_sm: &Arc<RecordStateMachine>,
    role: Role,
    sample_rate: u32,
    measure_result: &Arc<Mutex<MeasureResult>>,
    recording: &mut Option<RecordingCtx>,
) -> Result<(), String> {
    let is_recording = record_sm.is_recording();
    match (is_recording, recording.is_some()) {
        (true, false) => {
            let started_at_ms = match StoragePaths::default_macos() {
                Ok(paths) => resolve_started_at_ms(
                    &paths.plugin_data_dir(),
                    PROJECT_HASH_PHASE1,
                    BUS_PHASE1,
                ),
                Err(_) => now_epoch_ms(),
            };
            if let Some(ctx) = writer_start(role, sample_rate, started_at_ms) {
                *recording = Some(ctx);
            }
        }
        (false, true) => {
            if let Some(ctx) = recording.take() {
                writer_close(ctx);
            }
        }
        (true, true) => {
            let ctx = recording.as_mut().expect("some because of match arm");
            let m = measure_result
                .lock()
                .map_err(|e| format!("measure Mutex poisoned: {e}"))?
                .clone();
            let now_ms = now_epoch_ms();
            let t_ms = now_ms.saturating_sub(ctx.started_at_ms).max(0) as u64;

            if t_ms >= ctx.next_frame_ms && writer_append_frame(ctx, t_ms, &m) {
                ctx.next_frame_ms = (t_ms / FRAME_INTERVAL_MS + 1) * FRAME_INTERVAL_MS;
            }
            if t_ms >= ctx.next_psb_ms && writer_append_psb(ctx, t_ms, &m) {
                ctx.next_psb_ms = (t_ms / PSB_INTERVAL_MS + 1) * PSB_INTERVAL_MS;
            }
            if Instant::now() >= ctx.next_flush {
                ctx.writer.heartbeat_now();
                if let Err(e) = ctx.writer.flush() {
                    log::warn!("[writer] flush failed: {}", e);
                }
                ctx.next_flush = Instant::now() + FLUSH_INTERVAL;
            }
        }
        (false, false) => {}
    }
    Ok(())
}

/// 別スレッドで Record 停止シグナルを受けたときに writer を即座に閉じる。
///
/// IO Thread 終了シーケンスで使用: shutdown=true になったあとに呼ぶ。
/// 無用な writer drop を避けるため `Option` を消費する。
pub fn drain_on_shutdown(
    shutdown: &Arc<AtomicBool>,
    recording: &mut Option<RecordingCtx>,
) -> bool {
    use std::sync::atomic::Ordering;
    if shutdown.load(Ordering::Relaxed) {
        if let Some(ctx) = recording.take() {
            writer_close(ctx);
        }
        return true;
    }
    false
}

// ── Tests ─────────────────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use crate::plugin_data::{PluginDataFile, PluginDataWriter, Role, WriterPaths};
    use crate::record::RecordStateMachine;
    use crate::{License, MeasureResult, PsbSummary};
    use std::fs;
    use std::sync::atomic::{AtomicU64, Ordering};

    fn isolated_base() -> PathBuf {
        static COUNTER: AtomicU64 = AtomicU64::new(0);
        let n = COUNTER.fetch_add(1, Ordering::Relaxed);
        let pid = std::process::id();
        let dir = std::env::temp_dir().join(format!("kirin_record_writer_test_{pid}_{n}"));
        let _ = fs::remove_dir_all(&dir);
        fs::create_dir_all(&dir).unwrap();
        dir
    }

    fn make_ctx(base: &std::path::Path, role: Role, started_at_ms: i64) -> RecordingCtx {
        let paths = WriterPaths::build(
            base,
            PROJECT_HASH_PHASE1,
            BUS_PHASE1,
            role,
            "2026-04-19T12:00:00Z",
        );
        let final_path = paths.final_path.clone();
        let mut writer = PluginDataWriter::create(
            paths,
            "test-installation-id".to_string(),
            PROJECT_HASH_PHASE1.to_string(),
            role,
            BUS_PHASE1.to_string(),
            48000,
        )
        .unwrap();
        writer.flush().unwrap();
        RecordingCtx {
            writer,
            final_path,
            started_at_ms,
            next_frame_ms: 0,
            next_psb_ms: 0,
            next_flush: Instant::now() + FLUSH_INTERVAL,
            first_frame_logged: false,
        }
    }

    fn full_measure_result() -> MeasureResult {
        MeasureResult {
            lufs_m: Some(-14.2),
            true_peak: Some(-1.1),
            crest: Some(12.3),
            psr: Some(8.0),
            n_prime_total: Some(5.0),
            sharpness: Some(1.2),
            psb_summary: Some(PsbSummary {
                low: -10.0,
                mid: -12.0,
                high: -14.0,
            }),
            n_prime: Some([0.5; 20]),
            psb_bark: Some([0.05; 20]),
        }
    }

    #[test]
    fn parse_iso8601_to_epoch_ms_basic() {
        let ms_a = parse_iso8601_to_epoch_ms("2026-04-19T12:00:00Z").unwrap();
        let ms_b = parse_iso8601_to_epoch_ms("2026-04-19T12:00:01Z").unwrap();
        // 1 秒差分が 1000 ms として正しく反映されること
        assert_eq!(ms_b - ms_a, 1000);
    }

    #[test]
    fn parse_iso8601_to_epoch_ms_empty_returns_none() {
        assert!(parse_iso8601_to_epoch_ms("").is_none());
    }

    #[test]
    fn parse_iso8601_to_epoch_ms_invalid_returns_none() {
        assert!(parse_iso8601_to_epoch_ms("not-iso-8601").is_none());
    }

    #[test]
    fn resolve_started_at_ms_reads_signal_file() {
        let tmp_base = isolated_base();
        record_signal::write_pending(
            &tmp_base,
            PROJECT_HASH_PHASE1,
            BUS_PHASE1,
            "post-1".into(),
            "pre-1".into(),
        )
        .unwrap();
        let ms = resolve_started_at_ms(&tmp_base, PROJECT_HASH_PHASE1, BUS_PHASE1);
        let now = now_epoch_ms();
        // started_at は now 付近に埋め込まれたはず
        assert!((now - ms).abs() < 5_000, "ms={}, now={}", ms, now);
    }

    #[test]
    fn resolve_started_at_ms_missing_falls_back_to_now() {
        let tmp_base = isolated_base();
        let ms = resolve_started_at_ms(&tmp_base, PROJECT_HASH_PHASE1, BUS_PHASE1);
        let now = now_epoch_ms();
        assert!((now - ms).abs() < 1_000);
    }

    // ── ログ capture: Q9-C 仕様フォーマット検証 ─────────────────────────────
    //
    // `log::set_logger` は 1 プロセス 1 回だけ成功するため、同じ binary 内の他テストと
    // 競合しないよう OnceLock で 1 回だけ install する（record_writer テスト専用）。

    use std::sync::OnceLock;

    struct CaptureLogger {
        buf: Mutex<Vec<String>>,
    }
    static CAPTURE: OnceLock<&'static CaptureLogger> = OnceLock::new();

    impl log::Log for CaptureLogger {
        fn enabled(&self, _: &log::Metadata) -> bool {
            true
        }
        fn log(&self, record: &log::Record) {
            if let Ok(mut g) = self.buf.lock() {
                g.push(format!("{} {}", record.level(), record.args()));
            }
        }
        fn flush(&self) {}
    }

    fn install_capture() -> &'static CaptureLogger {
        CAPTURE.get_or_init(|| {
            let boxed: &'static CaptureLogger = Box::leak(Box::new(CaptureLogger {
                buf: Mutex::new(Vec::new()),
            }));
            // 他のテストで既に別の logger が設定済なら set_logger は失敗するが
            // それは capture テスト専用の前提上問題なし（無視）
            let _ = log::set_logger(boxed);
            log::set_max_level(log::LevelFilter::Warn);
            boxed
        })
    }

    fn drain_logs(cap: &CaptureLogger) -> Vec<String> {
        let mut g = cap.buf.lock().unwrap();
        let out = g.clone();
        g.clear();
        out
    }

    #[test]
    fn fallback_emits_spec_log_line() {
        let cap = install_capture();
        let _ = drain_logs(cap);
        let tmp_base = isolated_base();
        let _ = resolve_started_at_ms(&tmp_base, PROJECT_HASH_PHASE1, BUS_PHASE1);
        let logs = drain_logs(cap);
        assert!(
            logs.iter().any(|l| l.contains("[signal] started_at missing, using now() as fallback")),
            "expected fallback log, got: {:?}",
            logs
        );
    }

    #[test]
    fn fallback_emits_log_when_started_at_unparseable() {
        let cap = install_capture();
        let _ = drain_logs(cap);
        // record_signal.json を started_at フィールド欠落状態で書き込む。
        // #[serde(default)] により started_at = "" で deserialize され、
        // parse_iso8601_to_epoch_ms("") が None を返して fallback path に入る。
        let tmp_base = isolated_base();
        let dir = tmp_base.join(PROJECT_HASH_PHASE1).join(BUS_PHASE1);
        fs::create_dir_all(&dir).unwrap();
        let minimal = r#"{"status":"pending","requested_by":"post-1","target_pre_instance_id":"pre-1","t":"2026-04-19T12:00:00.000Z"}"#;
        fs::write(dir.join("record_signal.json"), minimal).unwrap();

        let _ = resolve_started_at_ms(&tmp_base, PROJECT_HASH_PHASE1, BUS_PHASE1);
        let logs = drain_logs(cap);
        assert!(
            logs.iter().any(|l| l.contains("[signal] started_at missing, using now() as fallback")),
            "expected fallback log on parse failure, got: {:?}",
            logs
        );
    }

    #[test]
    fn append_frame_skips_when_crest_missing() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let mut m = full_measure_result();
        m.crest = None;
        assert!(!writer_append_frame(&mut ctx, 100, &m));
        assert_eq!(ctx.data().frames.len(), 0);
    }

    #[test]
    fn append_frame_skips_when_n_prime_missing() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let mut m = full_measure_result();
        m.n_prime = None;
        assert!(!writer_append_frame(&mut ctx, 100, &m));
        assert_eq!(ctx.data().frames.len(), 0);
        assert!(!ctx.first_frame_logged);
    }

    #[test]
    fn append_frame_writes_when_all_fields_present() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let m = full_measure_result();
        assert!(writer_append_frame(&mut ctx, 200, &m));
        let frames = &ctx.data().frames;
        assert_eq!(frames.len(), 1);
        assert_eq!(frames[0].t_ms, 200);
        // 48kHz make_ctx: Phase D 値 Some (guardian_100 S-2)
        assert_eq!(frames[0].n_prime.unwrap()[0], 0.5);
        assert_eq!(frames[0].lufs_m, -14.2);
        assert!(ctx.first_frame_logged);
    }

    #[test]
    fn append_psb_skips_when_missing() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let mut m = full_measure_result();
        m.psb_bark = None;
        assert!(!writer_append_psb(&mut ctx, 500, &m));
        // 48kHz make_ctx: psb_snapshots = Some(vec![]) なので空チェックは len==0
        assert_eq!(
            ctx.data().psb_snapshots.as_ref().map(|v| v.len()),
            Some(0)
        );
    }

    #[test]
    fn append_psb_writes_when_present() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let m = full_measure_result();
        assert!(writer_append_psb(&mut ctx, 500, &m));
        let snaps = ctx.data().psb_snapshots.as_ref().unwrap();
        assert_eq!(snaps.len(), 1);
        assert_eq!(snaps[0].t_ms, 500);
        assert!(snaps[0].interpolatable);
    }

    #[test]
    fn writer_close_writes_status_closed_and_verifies_checksum() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let m = full_measure_result();
        assert!(writer_append_frame(&mut ctx, 100, &m));
        let final_path = ctx.final_path.clone();
        writer_close(ctx);

        let bytes = fs::read(&final_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.status, crate::plugin_data::Status::Closed);
        assert_eq!(loaded.frames.len(), 1);
        assert!(crate::plugin_data::verify_checksum(&loaded));
    }

    #[test]
    fn run_record_tick_watch_keeps_none() {
        let sm = Arc::new(RecordStateMachine::new());
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = None;
        run_record_tick(&sm, Role::Post, 48000, &m, &mut rec).unwrap();
        assert!(rec.is_none());
    }

    #[test]
    fn run_record_tick_record_to_watch_closes_writer() {
        let base = isolated_base();
        let ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        let final_path = ctx.final_path.clone();
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        sm.exit_record();
        run_record_tick(&sm, Role::Post, 48000, &m, &mut rec).unwrap();

        assert!(rec.is_none());
        let bytes = fs::read(&final_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.status, crate::plugin_data::Status::Closed);
    }

    #[test]
    fn run_record_tick_record_active_appends_frame_and_psb() {
        let base = isolated_base();
        // started_at を 600ms 前にずらして t_ms ≥ 500 となるようにする
        let started = now_epoch_ms() - 600;
        let ctx = make_ctx(&base, Role::Post, started);
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        run_record_tick(&sm, Role::Post, 48000, &m, &mut rec).unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert_eq!(ctx_ref.data().frames.len(), 1);
        // 48kHz: psb_snapshots = Some(vec![one])
        assert_eq!(
            ctx_ref.data().psb_snapshots.as_ref().map(|v| v.len()),
            Some(1)
        );
        assert!(ctx_ref.next_frame_ms >= 600);
        assert!(ctx_ref.next_psb_ms >= 1000);
    }

    #[test]
    fn run_record_tick_record_warmup_skips_frame() {
        let base = isolated_base();
        let started = now_epoch_ms() - 200;
        let ctx = make_ctx(&base, Role::Post, started);
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let mut m0 = full_measure_result();
        m0.n_prime = None;
        m0.psb_bark = None;
        let m = Arc::new(Mutex::new(m0));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        run_record_tick(&sm, Role::Post, 48000, &m, &mut rec).unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert_eq!(ctx_ref.data().frames.len(), 0);
        // 48kHz: psb_snapshots = Some(vec![]) (warmup → no append)
        assert_eq!(
            ctx_ref.data().psb_snapshots.as_ref().map(|v| v.len()),
            Some(0)
        );
    }

    #[test]
    fn pre_and_post_share_t_ms_axis() {
        // PRE と POST が同じ started_at_ms で記録すると、
        // 同じ絶対時刻に生成された frame は同じ t_ms を持つ。
        let base = isolated_base();
        let started = now_epoch_ms() - 300;
        let mut pre = make_ctx(&base, Role::Pre, started);
        let mut post = make_ctx(&base, Role::Post, started);
        let m = full_measure_result();

        let t_ms = (now_epoch_ms() - started) as u64;
        assert!(writer_append_frame(&mut pre, t_ms, &m));
        assert!(writer_append_frame(&mut post, t_ms, &m));
        assert_eq!(pre.data().frames[0].t_ms, post.data().frames[0].t_ms);
    }

    // ── V1: POST → PRE ペアリング統合テスト（サブ3-B path mismatch 退行防止）─────
    //
    // Bug 再現条件:
    //   POST は record_signal.json を plugin_data_dir 配下に書く一方、PRE poller
    //   と resolve_started_at_ms は $TMPDIR/kirin/ 配下を読んでいた。
    //   両者が別ディレクトリを指していたため、PRE が pending を検出できず、
    //   resolve_started_at_ms も fallback を返し続けていた。
    //
    // 本テストは「POST が書いたのと同じ base path を PRE / resolve が読む」
    // 契約を end-to-end で検証する。path mismatch が再発すると pending 検出
    // または started_at 復元が失敗して fail する。

    #[test]
    fn post_write_pending_triggers_pre_record_entry() {
        let base = isolated_base();

        // POST 側: record_signal.json を pending で書き込む
        let written = record_signal::write_pending(
            &base,
            PROJECT_HASH_PHASE1,
            BUS_PHASE1,
            "post-instance-1".into(),
            "pre-instance-1".into(),
        )
        .expect("write_pending must succeed on isolated base");
        assert!(!written.started_at.is_empty(), "started_at must be populated");

        // PRE 側: 同じ base で read_signal → Pending を検出
        let signal = record_signal::read_signal(&base, PROJECT_HASH_PHASE1, BUS_PHASE1)
            .expect("PRE must find record_signal at the same base POST wrote to");
        assert_eq!(signal.status, record_signal::SignalStatus::Pending);

        // PRE 側: state machine 遷移が成立する（license gate 通過）
        let sm = RecordStateMachine::new();
        sm.try_enter_record(License::Os)
            .expect("Os license must allow entering Record");
        assert!(sm.is_recording());

        // resolve_started_at_ms: record_signal.json から started_at を epoch_ms で返す
        // （fallback now() ではない）
        let resolved = resolve_started_at_ms(&base, PROJECT_HASH_PHASE1, BUS_PHASE1);
        let expected = parse_iso8601_to_epoch_ms(&signal.started_at)
            .expect("started_at must be parseable ISO 8601");
        assert_eq!(
            resolved, expected,
            "resolve_started_at_ms must return started_at from file, not fallback"
        );
    }

    /// record_signal.json の base path は plugin_data_dir 配下であり、
    /// $TMPDIR/kirin/ 配下ではないこと（tmp_kirin_base 再導入の退行検出）。
    #[test]
    fn record_signal_base_is_plugin_data_not_tmp() {
        let Ok(paths) = StoragePaths::default_macos() else {
            return; // $HOME 未解決環境ではスキップ
        };
        let plugin_data = paths.plugin_data_dir();

        // $TMPDIR/kirin/ 配下ではない
        assert!(
            !plugin_data.starts_with(std::env::temp_dir().join("kirin")),
            "plugin_data_dir must not be under $TMPDIR/kirin/, got: {}",
            plugin_data.display()
        );
        // 末端ディレクトリ名は "plugin_data"
        assert_eq!(
            plugin_data.file_name().and_then(|n| n.to_str()),
            Some("plugin_data")
        );
    }
}
