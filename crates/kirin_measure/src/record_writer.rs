//! Record 時の PluginDataWriter 共通ライフサイクル（PRE / POST 共有）。
//!
//! 責務:
//! - Watch↔Record 遷移に合わせた writer の生成・破棄
//! -  計測結果を frame (10 fps) / PSB (2 fps) で追記
//! - 30 秒間隔で heartbeat + atomic flush
//!
//! PRE / POST どちらも同じロジックで動く。違いは Role の指定と、
//! state machine を駆動するトリガ（POST=GUI ボタン、PRE=record_signal poll）のみ。
//!
//! # A-3 修正後
//! `project_hash` と `instance_id` は plugin から引数として渡る。旧
//! `BUS_PHASE1="MIX"` / `PROJECT_HASH_PHASE1="default"` 定数依存は廃止。
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
use crate::record_signal;
use crate::storage::{load_installation_id_safe, StoragePaths};
use crate::MeasureResult;

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

/// 指定 `post_instance_id` の record_signal.json から `started_at` を epoch ms 化。
///
/// 失敗時（ファイル不在 / `started_at` 空 / パース不能）は現在時刻 + 警告ログ。
/// t_ms = 0 起点にフォールバックすることで Record 続行可能。
pub fn resolve_started_at_ms(
    base: &std::path::Path,
    project_hash: &str,
    post_instance_id: &str,
) -> i64 {
    let signal_path = record_signal::signal_path(base, project_hash, post_instance_id);
    match record_signal::read_signal(base, project_hash, post_instance_id) {
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
/// - `project_hash`, `instance_id`: 新構造 path 構築用
/// - `paired_pre_instance_id`: POST 側でのみ Some。trigger_keep の target_id
///   （v1.2 (a) cross-instance pair 復元キー）
/// - `paired_post_instance_id`: PRE 側でのみ Some。record_signal の requested_by
///   （v1.2 (a) cross-instance pair 復元キー）
///
/// # 失敗要因（いずれも `None` を返してログ記録）
/// - `$HOME` 未解決
/// - identity.json 不在 or `installation_id` フィールド欠落
/// - `PluginDataWriter::create` が IO エラー
/// - 初回 flush 失敗
#[allow(clippy::too_many_arguments)]
pub fn writer_start(
    role: Role,
    sample_rate: u32,
    started_at_ms: i64,
    project_hash: &str,
    instance_id: &str,
    paired_pre_instance_id: Option<String>,
    paired_post_instance_id: Option<String>,
) -> Option<RecordingCtx> {
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
        project_hash,
        instance_id,
        role,
        &wall_clock_iso,
    );
    let final_path = writer_paths.final_path.clone();
    let mut w = match PluginDataWriter::create(
        writer_paths,
        installation_id,
        project_hash.to_string(),
        instance_id.to_string(),
        role,
        None,
        sample_rate,
        paired_pre_instance_id,
        paired_post_instance_id,
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
///  warm-up 中はスキップ。戻り値: 追記=true / スキップ=false。
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
/// - (false → true) 遷移 → `started_at_resolver` を呼び `writer_start`
/// - (true → false) 遷移 → `writer_close`
/// - Record 継続 → `next_frame_ms` / `next_psb_ms` に沿って append
/// - 30 秒経過 → `heartbeat_now` + `flush`
///
/// `started_at_resolver` は Watch→Record 遷移時にだけ呼ばれる（lazy）。
/// PRE は `partner_post_instance_id` の signal.started_at を、POST は自身の
/// signal.started_at を解決する用途で使う。
///
/// `paired_pre_resolver` / `paired_post_resolver` も Watch→Record 遷移時に
/// 1 度だけ呼ばれる。PRE は `paired_post` を返し、POST は `paired_pre` を返す
/// （v1.2 (a) cross-instance pair 復元キー）。
#[allow(clippy::too_many_arguments)]
pub fn run_record_tick(
    record_sm: &Arc<RecordStateMachine>,
    role: Role,
    sample_rate: u32,
    project_hash: &str,
    instance_id: &str,
    started_at_resolver: impl FnOnce() -> i64,
    paired_pre_resolver: impl FnOnce() -> Option<String>,
    paired_post_resolver: impl FnOnce() -> Option<String>,
    measure_result: &Arc<Mutex<MeasureResult>>,
    recording: &mut Option<RecordingCtx>,
) -> Result<(), String> {
    let is_recording = record_sm.is_recording();
    match (is_recording, recording.is_some()) {
        (true, false) => {
            let started_at_ms = started_at_resolver();
            let paired_pre = paired_pre_resolver();
            let paired_post = paired_post_resolver();
            if let Some(ctx) = writer_start(
                role,
                sample_rate,
                started_at_ms,
                project_hash,
                instance_id,
                paired_pre,
                paired_post,
            ) {
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

    const TEST_PH: &str = "ph";
    const TEST_IID: &str = "iid-test";

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
        let paths = WriterPaths::build(base, TEST_PH, TEST_IID, role, "2026-04-19T12:00:00Z");
        let final_path = paths.final_path.clone();
        let mut writer = PluginDataWriter::create(
            paths,
            "test-installation-id".to_string(),
            TEST_PH.to_string(),
            TEST_IID.to_string(),
            role,
            None,
            48000,
            None,
            None,
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
        let base = isolated_base();
        record_signal::write_pending(
            &base,
            TEST_PH,
            "post-1",
            "pre-1".into(),
            "daw-1".into(),
        )
        .unwrap();
        let ms = resolve_started_at_ms(&base, TEST_PH, "post-1");
        let now = now_epoch_ms();
        assert!((now - ms).abs() < 5_000, "ms={}, now={}", ms, now);
    }

    #[test]
    fn resolve_started_at_ms_missing_falls_back_to_now() {
        let base = isolated_base();
        let ms = resolve_started_at_ms(&base, TEST_PH, "post-x");
        let now = now_epoch_ms();
        assert!((now - ms).abs() < 1_000);
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
        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
        )
        .unwrap();
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
        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
        )
        .unwrap();

        assert!(rec.is_none());
        let bytes = fs::read(&final_path).unwrap();
        let loaded: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(loaded.status, crate::plugin_data::Status::Closed);
    }

    #[test]
    fn run_record_tick_record_active_appends_frame_and_psb() {
        let base = isolated_base();
        let started = now_epoch_ms() - 600;
        let ctx = make_ctx(&base, Role::Post, started);
        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
        )
        .unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert_eq!(ctx_ref.data().frames.len(), 1);
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

        run_record_tick(
            &sm,
            Role::Post,
            48000,
            TEST_PH,
            TEST_IID,
            now_epoch_ms,
            || None,
            || None,
            &m,
            &mut rec,
        )
        .unwrap();

        let ctx_ref = rec.as_ref().unwrap();
        assert_eq!(ctx_ref.data().frames.len(), 0);
        assert_eq!(
            ctx_ref.data().psb_snapshots.as_ref().map(|v| v.len()),
            Some(0)
        );
    }

    #[test]
    fn pre_and_post_share_t_ms_axis() {
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

    /// POST → PRE ペアリング統合テスト (path mismatch 退行防止 / 新構造版)。
    #[test]
    fn post_write_pending_triggers_pre_record_entry() {
        let base = isolated_base();
        let post_iid = "post-iid-1";

        let written = record_signal::write_pending(
            &base,
            TEST_PH,
            post_iid,
            "pre-iid-1".into(),
            "daw-1".into(),
        )
        .expect("write_pending must succeed");
        assert!(!written.started_at.is_empty());
        assert_eq!(written.daw_session_id, "daw-1");

        let signal = record_signal::read_signal(&base, TEST_PH, post_iid)
            .expect("PRE must find signal at the same base POST wrote to");
        assert_eq!(signal.status, record_signal::SignalStatus::Pending);

        let sm = RecordStateMachine::new();
        sm.try_enter_record(License::Os)
            .expect("Os license must allow entering Record");
        assert!(sm.is_recording());

        let resolved = resolve_started_at_ms(&base, TEST_PH, post_iid);
        let expected = parse_iso8601_to_epoch_ms(&signal.started_at)
            .expect("started_at must be parseable ISO 8601");
        assert_eq!(resolved, expected);
    }
}
