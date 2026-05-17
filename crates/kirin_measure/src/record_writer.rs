//! Record 時の PluginDataWriter 共通ライフサイクル（PRE / POST 共有）。
//!
//! 責務:
//! - Watch↔Record 遷移に合わせた writer の生成・破棄
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

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::AtomicBool;
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant, SystemTime};

use crate::engine::SessionSummary;
use crate::plugin_data::{
    compute_checksum, verify_checksum, PluginDataFile, PluginDataWriter, Role, Status,
    WriterPaths,
};
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

/// B-025 Group B-2 / Gap-19 + Group B-3 / Gap-20: flush 連続失敗の許容回数。
/// 値は `FLUSH_INTERVAL` (= 30 秒) × 3 = 90 秒 grace period (B25-2 推奨根拠)。
/// ネットワーク FS / iCloud Drive の一時的失敗 (sync 中 / token refresh 中) を
/// 90 秒以内なら吸収しつつ、持続する真の障害は record exit に切替える。
pub const CONSECUTIVE_FAILURE_THRESHOLD: usize = 3;

/// B-025 Group B-2 / Gap-19 + Group B-3 / Gap-20: io_thread が record exit + UI
/// 通知を発火するための内部 sentinel。`run_record_tick` が flush 連続失敗を検知
/// したとき `RecordingCtx::exit_requested` に設定し、io_thread は次 tick で
/// `writer_close` + `record_sm.exit_record()` + `record_error_message` 書込を行う。
///
/// Display は GUI 表示用英語固定 (G-115-29 / 約束 5 原則 R-26 沈黙ゲート)。
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum RecordError {
    /// Gap-19: plugin_data 親 dir が `CONSECUTIVE_FAILURE_THRESHOLD` 回連続で不在。
    DirectoryMissing,
    /// Gap-20: flush の Io / Serde 失敗が `CONSECUTIVE_FAILURE_THRESHOLD` 回連続。
    /// disk full / 権限剥奪 / Serde 異常 等を包含。
    WriteFailureExceeded,
}

impl RecordError {
    /// GUI ステータス行に表示する英語固定文言 (G-115-29)。
    pub fn ui_message(self) -> &'static str {
        match self {
            Self::DirectoryMissing => "Record stopped: storage missing",
            Self::WriteFailureExceeded => "Record stopped: write failed",
        }
    }
}

impl std::fmt::Display for RecordError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "{}", self.ui_message())
    }
}

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
    /// B-025 Gap-19: flush() が `WriterError::DirectoryMissing` を返した連続回数。
    /// 成功時 0 にリセット (transient 失敗の吸収)。
    pub consecutive_dir_missing: usize,
    /// B-025 Gap-20: flush() が `Io` / `Serde` を返した連続回数 (DirectoryMissing 以外)。
    /// 成功時 0 にリセット。
    pub consecutive_write_error: usize,
    /// B-025 Gap-19/20: 連続失敗が閾値に達した時の sentinel。io_thread が次 tick で
    /// `writer_close` + `record_sm.exit_record()` + UI 通知文字列書込を実施する。
    pub exit_requested: Option<RecordError>,
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
        consecutive_dir_missing: 0,
        consecutive_write_error: 0,
        exit_requested: None,
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

/// Record 終了 (B-043 セッション集計注入版)。
///
/// `summary` が `Some` の場合、`set_session_aggregates()` で LUFS-I / LRA / PLR
/// を JSON に焼き込んでから close する。Measure Thread が Record 中に共有スロットに
/// 書いた最新値を呼出側が `take_session_summary` で読んで渡す。
pub fn writer_close_with_summary(mut ctx: RecordingCtx, summary: Option<SessionSummary>) {
    if let Some(s) = summary {
        ctx.writer.set_session_aggregates(s);
    }
    writer_close(ctx);
}

/// `Arc<Mutex<Option<SessionSummary>>>` から最新値を取り出して `None` でクリアする (B-043)。
///
/// IO Thread が Record→Watch 遷移時に呼ぶ。次の Record セッションは
/// Measure Thread の Watch→Record 遷移ハンドラが上書き None するため、
/// ここでも一応クリアして冪等性を担保する。
pub fn take_session_summary(slot: &Arc<Mutex<Option<SessionSummary>>>) -> Option<SessionSummary> {
    match slot.lock() {
        Ok(mut g) => g.take(),
        Err(_) => None,
    }
}

/// 必要な 5 フィールドが Some のときのみ 1 frame を追記。
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
        .append_frame(t_ms, n_prime, sharpness, lufs_m, true_peak, crest, m.psr);
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
    session_summary: Option<&Arc<Mutex<Option<SessionSummary>>>>,
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
                // B-043: Record→Watch 遷移時に Measure Thread の最新セッション集計を取り出して注入。
                let summary = session_summary.and_then(take_session_summary);
                writer_close_with_summary(ctx, summary);
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
                match ctx.writer.flush() {
                    Ok(()) => {
                        // B-025 Gap-19/20: 成功で counter リセット (transient 失敗を吸収)。
                        ctx.consecutive_dir_missing = 0;
                        ctx.consecutive_write_error = 0;
                    }
                    Err(crate::plugin_data::WriterError::DirectoryMissing) => {
                        ctx.consecutive_dir_missing =
                            ctx.consecutive_dir_missing.saturating_add(1);
                        log::warn!(
                            "[writer] flush failed: parent directory missing (count={}/{})",
                            ctx.consecutive_dir_missing,
                            CONSECUTIVE_FAILURE_THRESHOLD
                        );
                        if ctx.consecutive_dir_missing >= CONSECUTIVE_FAILURE_THRESHOLD
                            && ctx.exit_requested.is_none()
                        {
                            ctx.exit_requested = Some(RecordError::DirectoryMissing);
                        }
                    }
                    Err(e) => {
                        ctx.consecutive_write_error =
                            ctx.consecutive_write_error.saturating_add(1);
                        log::warn!(
                            "[writer] flush failed: {} (count={}/{})",
                            e,
                            ctx.consecutive_write_error,
                            CONSECUTIVE_FAILURE_THRESHOLD
                        );
                        if ctx.consecutive_write_error >= CONSECUTIVE_FAILURE_THRESHOLD
                            && ctx.exit_requested.is_none()
                        {
                            ctx.exit_requested = Some(RecordError::WriteFailureExceeded);
                        }
                    }
                }
                ctx.next_flush = Instant::now() + FLUSH_INTERVAL;
            }
        }
        (false, false) => {}
    }
    Ok(())
}

// ── B-025 Group B-1 / Gap-8: Startup orphan .tmp recovery ───────────────────

/// `recover_orphan_tmps` の集計。GUI 通知不要 (起動時の 1 回処理 / R-28 機能的沈黙)
/// なので `log::info!` 経由でのみ可視化し、戻り値は呼出側のテスト/診断用。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct RecoveryReport {
    /// `.tmp` を `.json` に atomic rename して救出した件数。
    pub recovered: usize,
    /// HMAC-SHA256 / serde 整合性に失敗し放置した件数 (削除しない / 将来分析用)。
    pub orphaned: usize,
}

/// B-025 Group B-1 / Gap-8: 30 秒 flush 周期中の DAW crash で残った `*.json.tmp`
/// を起動時に拾い、整合 (`verify_checksum`) すれば対応 `*.json` に atomic rename する。
/// 不整合は warn ログのみで残置 (削除しない / 将来分析用 / 約束 5 原則)。
///
/// 走査対象: `plugin_data_root` 配下を再帰的に walk して
/// `*.json.tmp` 拡張子を持つ通常ファイル全件。`record_signal/` `preset/` 等の
/// 予約名以下に `.tmp` は出現しないが、再帰で拾っても害はない。
///
/// PRE / POST / IO Thread の `thread::spawn` 直後 1 回だけ呼ぶ (Group A
/// `clear_stale_self_acks_at_startup` と同位相)。loop 内で繰り返さない。
///
/// R-28 機能的沈黙: storage path 解決失敗 / read_dir 失敗 / ファイル read 失敗
/// は当該 dir/file のみ skip。UI エラー出さず log のみ。
pub fn recover_orphan_tmps(plugin_data_root: &Path) -> RecoveryReport {
    let mut report = RecoveryReport::default();
    if !plugin_data_root.is_dir() {
        return report;
    }
    walk_tmp_recover(plugin_data_root, &mut report);
    if report.recovered > 0 || report.orphaned > 0 {
        log::info!(
            "[recover] startup .tmp recovery: recovered={} orphaned={} root={}",
            report.recovered,
            report.orphaned,
            plugin_data_root.display()
        );
    }
    report
}

/// `recover_orphan_tmps` の実走査。`fs::read_dir` 失敗は当該 dir のみ skip。
fn walk_tmp_recover(dir: &Path, report: &mut RecoveryReport) {
    let entries = match fs::read_dir(dir) {
        Ok(e) => e,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let ftype = match entry.file_type() {
            Ok(t) => t,
            Err(_) => continue,
        };
        if ftype.is_dir() {
            walk_tmp_recover(&path, report);
            continue;
        }
        if !ftype.is_file() {
            continue;
        }
        // `.json.tmp` で終わるファイルのみ対象 (`{compact}.json.tmp` と同形)。
        let Some(name) = path.file_name().and_then(|n| n.to_str()) else {
            continue;
        };
        if !name.ends_with(".json.tmp") {
            continue;
        }
        recover_one_tmp(&path, report);
    }
}

/// `.json.tmp` 1 件を試行する。integrity OK なら `.json` に rename。
/// 不整合は `report.orphaned += 1` で記録 (削除しない)。
fn recover_one_tmp(tmp_path: &Path, report: &mut RecoveryReport) {
    let bytes = match fs::read(tmp_path) {
        Ok(b) => b,
        Err(e) => {
            log::warn!(
                "[recover] read failed (skip): {} ({})",
                tmp_path.display(),
                e
            );
            report.orphaned += 1;
            return;
        }
    };
    let data: PluginDataFile = match serde_json::from_slice(&bytes) {
        Ok(d) => d,
        Err(e) => {
            log::warn!(
                "[recover] parse failed (orphan kept): {} ({})",
                tmp_path.display(),
                e
            );
            report.orphaned += 1;
            return;
        }
    };
    if !verify_checksum(&data) {
        log::warn!(
            "[recover] checksum mismatch (orphan kept): {}",
            tmp_path.display()
        );
        report.orphaned += 1;
        return;
    }
    // `{compact}.json.tmp` → `{compact}.json` に atomic rename。
    let final_path = strip_tmp_suffix(tmp_path);
    // 既に `.json` 側がある場合 (= 直前 flush が成功してから DAW が落ちた稀ケース)
    // は `.tmp` 側の方が新しい/古いの判定が困難なので、安全側で `.tmp` を残置 +
    // orphaned 計上。GUI 通知不要 (起動時 R-28 機能的沈黙)。
    if final_path.exists() {
        log::warn!(
            "[recover] final .json already exists (orphan kept): {}",
            tmp_path.display()
        );
        report.orphaned += 1;
        return;
    }
    if let Err(e) = fs::rename(tmp_path, &final_path) {
        log::warn!(
            "[recover] rename failed: {} → {} ({})",
            tmp_path.display(),
            final_path.display(),
            e
        );
        report.orphaned += 1;
        return;
    }
    log::info!(
        "[recover] recovered: {} → {}",
        tmp_path.display(),
        final_path.display()
    );
    report.recovered += 1;
}

/// `{path}.json.tmp` から末尾 `.tmp` を取り除いた `{path}.json` を返す。
fn strip_tmp_suffix(tmp_path: &Path) -> PathBuf {
    // `.json.tmp` 不変の前提 (caller が ends_with 検査済)。
    let s = tmp_path.to_string_lossy();
    let trimmed = s.strip_suffix(".tmp").unwrap_or(&s);
    PathBuf::from(trimmed.to_string())
}

// ── B-026 / Gap-9: Startup stale active sweep ───────────────────────────────

/// `sweep_stale_active_at_startup` の stale 判定閾値 (秒)。
/// `io_thread_post::PRE_LIVENESS_STALE_SECS` (per-tick mtime 判定 / G-50-33) と
/// 同値。crate 跨ぎ重複定義を避ける独立宣言だが、両者は同じ意味論
/// ("PRE/POST が 60s 以上 mtime を更新できなかった = crash 残骸") を共有する。
pub const STALE_ACTIVE_SWEEP_SECS: u64 = 60;

/// `sweep_stale_active_at_startup` の集計。GUI 通知不要 (起動時 R-28 機能的沈黙)。
#[derive(Debug, Clone, Copy, PartialEq, Eq, Default)]
pub struct StaleSweepReport {
    /// `status=Active && mtime>60s` の `.json` を `status=Closed` に書換成功した件数。
    pub closed: usize,
    /// 判定対象だが metadata / read / parse / write / rename いずれかに失敗して
    /// 残置した件数 (R-28 機能的沈黙でログのみ)。
    pub skipped: usize,
}

/// B-026 / Gap-9: Hypha 起動時に plugin_data 配下の crash 残骸 PluginDataFile
/// (`status=Active && mtime > STALE_ACTIVE_SWEEP_SECS`) を `status=Closed` に
/// 書き換える。Lens 側が「進行中 Record」と誤認するのを startup 時点で構造的に
/// 解消する (Daisuke Gap-9 仕様)。
///
/// # 対象スコープ
/// `plugin_data_root` を再帰 walk し、以下を全て満たすファイルのみ対象:
/// 1. 親ディレクトリ名が `pre` または `post` (= `Role::dir_name()`)
/// 2. ファイル拡張子が `.json` (`.tmp` は除外 / Gap-8 `recover_orphan_tmps`
///    と非重複)
/// 3. mtime 経過 > `STALE_ACTIVE_SWEEP_SECS`
/// 4. parse 成功かつ `status == Status::Active`
///
/// # 動作
/// `status` を `Closed` に書換 → `compute_checksum` で HMAC-SHA256 再計算 →
/// `{path}.tmp` に書込 → atomic `rename` で `{path}` 上書き。
/// 既存の append / flush 経路と同じ atomic rename パターン。
///
/// # 呼出位置
/// PRE / POST 両 IO Thread の `thread::spawn` 直後 1 回のみ
/// (`recover_orphan_tmps` と同位相)。loop 内で繰り返さない。
///
/// # 触れない領域 (B-026 Pass 15)
/// - `record_signal` / `all_keep_signal` / `all_stop_signal`: 別スコープ
/// - `*.json.tmp`: `recover_orphan_tmps` (Gap-8) の対象
/// - per-tick PRE liveness 判定 (`PRE_LIVENESS_STALE_SECS`): loop 内専用
/// - `cleanup::exit_record_full`: per-Record cleanup 専用
/// - `PluginDataFile.heartbeat` フィールド: 読まず mtime のみ使用
///
/// # R-28 機能的沈黙
/// `read_dir` / `metadata` / `read` / `parse` / `write` / `rename` 失敗は
/// `report.skipped += 1` + warn ログのみ。UI エラーは出さない。
pub fn sweep_stale_active_at_startup(plugin_data_root: &Path) -> StaleSweepReport {
    let mut report = StaleSweepReport::default();
    if !plugin_data_root.is_dir() {
        return report;
    }
    walk_stale_sweep(plugin_data_root, &mut report);
    if report.closed > 0 || report.skipped > 0 {
        log::info!(
            "[Gap-9] startup stale sweep: closed={} skipped={} root={}",
            report.closed,
            report.skipped,
            plugin_data_root.display()
        );
    }
    report
}

/// `sweep_stale_active_at_startup` の実走査。`fs::read_dir` 失敗は当該 dir のみ skip。
fn walk_stale_sweep(dir: &Path, report: &mut StaleSweepReport) {
    let entries = match fs::read_dir(dir) {
        Ok(e) => e,
        Err(_) => return,
    };
    for entry in entries.flatten() {
        let path = entry.path();
        let ftype = match entry.file_type() {
            Ok(t) => t,
            Err(_) => continue,
        };
        if ftype.is_dir() {
            walk_stale_sweep(&path, report);
            continue;
        }
        if !ftype.is_file() {
            continue;
        }
        // 拡張子 .json (`.tmp` は対象外 / Gap-8 と非重複)
        let Some(ext) = path.extension().and_then(|s| s.to_str()) else {
            continue;
        };
        if ext != "json" {
            continue;
        }
        // 親ディレクトリ名が "pre" または "post" (= `Role::dir_name()`)
        let Some(parent_name) = path
            .parent()
            .and_then(|p| p.file_name())
            .and_then(|s| s.to_str())
        else {
            continue;
        };
        if parent_name != "pre" && parent_name != "post" {
            continue;
        }
        try_close_one_stale(&path, report);
    }
}

/// 1 ファイルの sweep 試行。stale 判定 (mtime / status) → atomic rewrite。
///
/// fresh / Closed / mtime 取得不能 (将来時刻含む) は **skipped にも数えず** 静かに
/// スキップする (touch 不要の対象は report に現れないことが「正常」)。
/// metadata / read / parse / write / rename 失敗は warn ログ + `skipped += 1`。
fn try_close_one_stale(path: &Path, report: &mut StaleSweepReport) {
    let metadata = match fs::metadata(path) {
        Ok(m) => m,
        Err(e) => {
            log::warn!("[Gap-9] metadata failed (skip): {} ({})", path.display(), e);
            report.skipped += 1;
            return;
        }
    };
    let mtime = match metadata.modified() {
        Ok(t) => t,
        Err(e) => {
            log::warn!(
                "[Gap-9] mtime unavailable (skip): {} ({})",
                path.display(),
                e
            );
            report.skipped += 1;
            return;
        }
    };
    let elapsed = match SystemTime::now().duration_since(mtime) {
        Ok(d) => d,
        // mtime が future (時計巻戻し / NFS 時刻ズレ) → fresh とみなし対象外。
        Err(_) => return,
    };
    if elapsed <= Duration::from_secs(STALE_ACTIVE_SWEEP_SECS) {
        return;
    }
    let bytes = match fs::read(path) {
        Ok(b) => b,
        Err(e) => {
            log::warn!("[Gap-9] read failed (skip): {} ({})", path.display(), e);
            report.skipped += 1;
            return;
        }
    };
    let mut data: PluginDataFile = match serde_json::from_slice(&bytes) {
        Ok(d) => d,
        Err(e) => {
            log::warn!("[Gap-9] parse failed (skip): {} ({})", path.display(), e);
            report.skipped += 1;
            return;
        }
    };
    if data.status != Status::Active {
        // Closed は対象外 (二重 close を回避)。
        return;
    }
    data.status = Status::Closed;
    data.checksum = match compute_checksum(&data) {
        Ok(c) => c,
        Err(e) => {
            log::warn!(
                "[Gap-9] checksum recompute failed (skip): {} ({})",
                path.display(),
                e
            );
            report.skipped += 1;
            return;
        }
    };
    let json = match serde_json::to_vec(&data) {
        Ok(v) => v,
        Err(e) => {
            log::warn!(
                "[Gap-9] serialize failed (skip): {} ({})",
                path.display(),
                e
            );
            report.skipped += 1;
            return;
        }
    };
    let mut tmp_os = path.as_os_str().to_os_string();
    tmp_os.push(".tmp");
    let tmp = PathBuf::from(tmp_os);
    if let Err(e) = fs::write(&tmp, &json) {
        log::warn!(
            "[Gap-9] tmp write failed (skip): {} ({})",
            path.display(),
            e
        );
        report.skipped += 1;
        return;
    }
    if let Err(e) = fs::rename(&tmp, path) {
        log::warn!(
            "[Gap-9] rename failed (skip): {} ({})",
            path.display(),
            e
        );
        report.skipped += 1;
        return;
    }
    log::info!("[Gap-9] closed stale Active: {}", path.display());
    report.closed += 1;
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
            consecutive_dir_missing: 0,
            consecutive_write_error: 0,
            exit_requested: None,
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
            None,
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
            None,
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
            None,
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
            None,
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

    // ── B-025 Group B-1 / Gap-8 (recover_orphan_tmps) ─────────────────────

    /// 有効な .tmp (整合 HMAC-SHA256) は .json に atomic rename される。
    /// recovered=1, orphaned=0, .tmp 消滅, .json 出現。
    #[test]
    fn recover_orphan_tmps_recovers_valid_tmp() {
        let base = isolated_base();
        // 1. flush して .json を作る → bytes を採取。
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        let valid_bytes = fs::read(&final_path).unwrap();
        // 2. .json を消し、同 dir に .json.tmp として配置 (DAW crash 直前 = .tmp 残置)。
        fs::remove_file(&final_path).unwrap();
        let tmp_path = final_path.with_extension("json.tmp");
        fs::write(&tmp_path, &valid_bytes).unwrap();

        let report = recover_orphan_tmps(&base);
        assert_eq!(report.recovered, 1);
        assert_eq!(report.orphaned, 0);
        assert!(final_path.exists(), ".json must exist after recover");
        assert!(!tmp_path.exists(), ".tmp must be renamed away");
    }

    /// checksum 不整合の .tmp は orphaned として残置 (削除しない)。
    #[test]
    fn recover_orphan_tmps_keeps_invalid_tmp() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        let mut bytes = fs::read(&final_path).unwrap();
        // checksum 値を破壊 (HMAC mismatch)。
        // checksum field の hex string 末尾を別文字に置換。
        let s = String::from_utf8(bytes.clone()).unwrap();
        let mutated = s.replace(r#""validity":true"#, r#""validity":false"#);
        bytes = mutated.into_bytes();

        fs::remove_file(&final_path).unwrap();
        let tmp_path = final_path.with_extension("json.tmp");
        fs::write(&tmp_path, &bytes).unwrap();

        let report = recover_orphan_tmps(&base);
        assert_eq!(report.recovered, 0);
        assert_eq!(report.orphaned, 1);
        assert!(!final_path.exists(), ".json must NOT exist for orphan");
        assert!(tmp_path.exists(), ".tmp must be kept (warn-only)");
    }

    /// .tmp が 1 件もないとき no-op (recovered=0, orphaned=0)。
    #[test]
    fn recover_orphan_tmps_no_tmps_no_op() {
        let base = isolated_base();
        // 通常の .json だけ存在させる。
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        assert!(final_path.exists());

        let report = recover_orphan_tmps(&base);
        assert_eq!(report.recovered, 0);
        assert_eq!(report.orphaned, 0);
        assert!(final_path.exists());
    }

    /// plugin_data root 自体が不在の場合は no-op で安全に終わる (R-28 機能的沈黙)。
    #[test]
    fn recover_orphan_tmps_missing_root_no_op() {
        let nonexistent = std::env::temp_dir()
            .join(format!("kirin_recover_missing_{}", std::process::id()));
        let _ = fs::remove_dir_all(&nonexistent);
        let report = recover_orphan_tmps(&nonexistent);
        assert_eq!(report.recovered, 0);
        assert_eq!(report.orphaned, 0);
    }

    // ── B-026 / Gap-9 (sweep_stale_active_at_startup) ─────────────────────

    /// status=Active かつ mtime > 60s (= STALE_ACTIVE_SWEEP_SECS) のファイルは
    /// status=Closed に書き換えられ checksum が再計算される (verify_checksum 通過)。
    #[test]
    fn sweep_stale_active_closes_stale_file() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Pre, now_epoch_ms());
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        drop(ctx);

        // mtime を 61 秒過去に巻き戻す。
        let stale = SystemTime::now() - Duration::from_secs(STALE_ACTIVE_SWEEP_SECS + 1);
        let f = std::fs::File::open(&final_path).unwrap();
        f.set_modified(stale).unwrap();
        drop(f);

        let report = sweep_stale_active_at_startup(&base);
        assert_eq!(report.closed, 1, "stale Active file must be closed");
        assert_eq!(report.skipped, 0);

        let bytes = fs::read(&final_path).unwrap();
        let data: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(
            data.status,
            Status::Closed,
            "status must flip Active → Closed"
        );
        assert!(
            verify_checksum(&data),
            "checksum must be recomputed and verify successfully"
        );
    }

    /// fresh (mtime=now) は touch されない (closed=0, skipped=0)。
    /// fresh は per-tick 経路 (PRE_LIVENESS_STALE_SECS) の範囲なので startup
    /// sweep が干渉しないことを構造的に保証する。
    #[test]
    fn sweep_stale_active_ignores_fresh_file() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Pre, now_epoch_ms());
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        drop(ctx);

        let report = sweep_stale_active_at_startup(&base);
        assert_eq!(report.closed, 0, "fresh file must not be closed");
        assert_eq!(
            report.skipped, 0,
            "fresh file must be silently passed (not counted as skipped)"
        );

        let bytes = fs::read(&final_path).unwrap();
        let data: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(
            data.status,
            Status::Active,
            "fresh file must remain Active"
        );
    }

    /// 既に status=Closed のファイルは mtime > 60s でも対象外 (二重 close 回避)。
    #[test]
    fn sweep_stale_active_ignores_closed_file() {
        let base = isolated_base();
        let mut ctx = make_ctx(&base, Role::Post, now_epoch_ms());
        ctx.writer.flush().unwrap();
        let final_path = ctx.final_path.clone();
        drop(ctx);

        // 直接 status を Closed に書換 + checksum 再計算 (close() consumption 回避)。
        let bytes = fs::read(&final_path).unwrap();
        let mut data: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        data.status = Status::Closed;
        data.checksum = compute_checksum(&data).unwrap();
        let json = serde_json::to_vec(&data).unwrap();
        fs::write(&final_path, &json).unwrap();

        // mtime を 61 秒過去に巻き戻す。
        let stale = SystemTime::now() - Duration::from_secs(STALE_ACTIVE_SWEEP_SECS + 1);
        let f = std::fs::File::open(&final_path).unwrap();
        f.set_modified(stale).unwrap();
        drop(f);

        let report = sweep_stale_active_at_startup(&base);
        assert_eq!(report.closed, 0, "Closed file must not be re-closed");
        assert_eq!(report.skipped, 0);

        let bytes = fs::read(&final_path).unwrap();
        let data2: PluginDataFile = serde_json::from_slice(&bytes).unwrap();
        assert_eq!(data2.status, Status::Closed);
    }

    // ── B-025 Group B-2 / Gap-19 + Group B-3 / Gap-20 ────────────────────

    /// flush() が DirectoryMissing を 3 回連続で返したら exit_requested に
    /// `RecordError::DirectoryMissing` がセットされ、ui_message が固定文言。
    #[test]
    fn run_record_tick_dir_missing_threshold_sets_exit_requested() {
        let base = isolated_base();
        let started = now_epoch_ms() - 100;
        let mut ctx = make_ctx(&base, Role::Post, started);
        // role dir を消去 → flush で DirectoryMissing。
        let parent = ctx.final_path.parent().unwrap().to_path_buf();
        fs::remove_dir_all(&parent).unwrap();
        // 即時 flush 実行のため next_flush を過去に。
        ctx.next_flush = Instant::now() - Duration::from_secs(1);

        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        for i in 1..=CONSECUTIVE_FAILURE_THRESHOLD {
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
                None,
            )
            .unwrap();
            let ctx_ref = rec.as_ref().unwrap();
            assert_eq!(
                ctx_ref.consecutive_dir_missing, i,
                "tick {} should bump dir_missing counter to {}",
                i, i
            );
            // 次の tick で flush 再実行できるよう next_flush を巻き戻す。
            rec.as_mut().unwrap().next_flush = Instant::now() - Duration::from_secs(1);
        }
        let exit = rec.as_ref().unwrap().exit_requested;
        assert_eq!(exit, Some(RecordError::DirectoryMissing));
        assert_eq!(
            RecordError::DirectoryMissing.ui_message(),
            "Record stopped: storage missing"
        );
    }

    /// flush() が `Io` を 3 回連続で返したら exit_requested に
    /// `RecordError::WriteFailureExceeded` がセットされ、ui_message が固定文言。
    /// `tmp_path` の場所をディレクトリにすると `fs::write` が Io error を返す。
    #[test]
    fn run_record_tick_write_error_threshold_sets_exit_requested() {
        let base = isolated_base();
        let started = now_epoch_ms() - 100;
        let mut ctx = make_ctx(&base, Role::Post, started);
        // tmp_path に同名 dir を作成 → fs::write が EISDIR で失敗。
        let tmp_path = ctx.final_path.with_extension("json.tmp");
        fs::create_dir_all(&tmp_path).unwrap();
        ctx.next_flush = Instant::now() - Duration::from_secs(1);

        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        for i in 1..=CONSECUTIVE_FAILURE_THRESHOLD {
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
                None,
            )
            .unwrap();
            let ctx_ref = rec.as_ref().unwrap();
            assert_eq!(
                ctx_ref.consecutive_write_error, i,
                "tick {} should bump write_error counter to {}",
                i, i
            );
            assert_eq!(ctx_ref.consecutive_dir_missing, 0);
            rec.as_mut().unwrap().next_flush = Instant::now() - Duration::from_secs(1);
        }
        let exit = rec.as_ref().unwrap().exit_requested;
        assert_eq!(exit, Some(RecordError::WriteFailureExceeded));
        assert_eq!(
            RecordError::WriteFailureExceeded.ui_message(),
            "Record stopped: write failed"
        );
    }

    /// 失敗 → 成功で counter が 0 に戻る (transient 失敗の吸収)。
    /// dir 消去 → 1 回失敗 → dir 復旧 → 1 回成功 で 0 リセット。
    #[test]
    fn run_record_tick_counter_resets_on_success() {
        let base = isolated_base();
        let started = now_epoch_ms() - 100;
        let mut ctx = make_ctx(&base, Role::Post, started);
        let parent = ctx.final_path.parent().unwrap().to_path_buf();
        fs::remove_dir_all(&parent).unwrap();
        ctx.next_flush = Instant::now() - Duration::from_secs(1);

        let sm = Arc::new(RecordStateMachine::new());
        sm.try_enter_record(License::Os).unwrap();
        let m = Arc::new(Mutex::new(full_measure_result()));
        let mut rec: Option<RecordingCtx> = Some(ctx);

        // 1 回失敗 (DirectoryMissing) → counter=1。
        run_record_tick(
            &sm, Role::Post, 48000, TEST_PH, TEST_IID, now_epoch_ms,
            || None, || None, &m, &mut rec,
            None,
        )
        .unwrap();
        assert_eq!(rec.as_ref().unwrap().consecutive_dir_missing, 1);
        assert!(rec.as_ref().unwrap().exit_requested.is_none());

        // dir 復旧 + flush 即時化 → 成功で 0 リセット。
        fs::create_dir_all(&parent).unwrap();
        rec.as_mut().unwrap().next_flush = Instant::now() - Duration::from_secs(1);
        run_record_tick(
            &sm, Role::Post, 48000, TEST_PH, TEST_IID, now_epoch_ms,
            || None, || None, &m, &mut rec,
            None,
        )
        .unwrap();
        assert_eq!(rec.as_ref().unwrap().consecutive_dir_missing, 0);
        assert_eq!(rec.as_ref().unwrap().consecutive_write_error, 0);
        assert!(rec.as_ref().unwrap().exit_requested.is_none());
    }
}
