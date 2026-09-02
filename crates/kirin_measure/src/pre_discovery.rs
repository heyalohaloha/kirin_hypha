//! POST 側 filesystem-discovery (B-021 Phase 1A)。
//!
//! cdylib 隔離 (PRE.vst3 / POST.vst3 で `static OnceLock` が別実体になる) 対策と
//! して、POST IO Thread が `$TMPDIR/kirin/` 配下を scan し、active な
//! `{project_uuid}/` dir を動的に検出する。POST 自身の `project_uuid` は path 構築
//! のフォールバックとしてのみ使う。
//!
//! Phase 1B で kirin_measure の dylib 化 + PRE adopt logic が成立した後も、
//! deep defense として残す (host 互換性の保険 / multi-PRE の保険)。
//!
//! # 選択基準
//! - 各 `{project_uuid}/` 配下に `*/pre.json` が 1 件以上存在する dir のみ候補
//! - `pre.json` の mtime が `DISCOVERY_STALE_SECS` 以上古い候補は除外
//! - 残った候補のうち mtime 最新のものを返す
//!
//! # fail mode (R-28 機能的沈黙)
//! - `kirin_root` 不在 / 権限なし → `None`
//! - 個別 sub dir の `read_dir` 失敗 → その dir のみ skip
//! - `metadata` / `modified()` 失敗 → その file のみ skip
//!
//! # 公式 doc 参照 (R-11)
//! - `std::fs::read_dir` <https://doc.rust-lang.org/std/fs/fn.read_dir.html>
//!   「path 不在 / 権限なし / non-directory で `Err` を返す」
//! - `std::fs::Metadata::modified`
//!   <https://doc.rust-lang.org/std/fs/struct.Metadata.html#method.modified>
//!   「Unix では stat の mtime を返す。サポートされない platform で `Err`」

use std::fs;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant, SystemTime};

/// `pre.json` の mtime が `now` から見て本値以上古ければ stale 扱いで除外する閾値 (秒)。
///
/// `io_thread_post::NO_PRE_SECS` (= 10) と同じ意味論: PRE が本値以上更新されて
/// いなければ inactive とみなす。`io_thread_post` の freshness_mode と整合させる
/// ことで「discovery で見つけた dir が compute_delta で即座に NoPre 判定」になる
/// 不整合を避ける。
pub const DISCOVERY_STALE_SECS: u64 = 30; // B-046: 10→30 (▼ 候補保持時間延長 / G-115-246)

/// `discover_active_pre_dir` を呼ぶ最低間隔。100ms tick で毎回 fs::read_dir すると
/// I/O コストが上がるため、1 秒に 1 回まで制限する。
const RESCAN_INTERVAL: Duration = Duration::from_secs(1);

/// POST 側の動的 PRE 検出キャッシュ。
///
/// `run_tick` 内で `should_rescan(now)` が true のときだけ `record_scan` を
/// 呼ぶ。それ以外のループでは `cached_pre_dir()` で前回結果を再利用する。
#[derive(Debug, Default)]
pub struct PostDiscoveryState {
    cached_pre_dir: Option<PathBuf>,
    last_scan: Option<Instant>,
}

impl PostDiscoveryState {
    pub fn new() -> Self {
        Self::default()
    }

    /// 前回 scan 結果 (cache)。`run_tick` の毎ループで参照される。
    pub fn cached_pre_dir(&self) -> Option<&Path> {
        self.cached_pre_dir.as_deref()
    }

    /// 1 秒以上経過しているか、まだ一度も scan していない場合のみ true。
    pub fn should_rescan(&self, now: Instant) -> bool {
        match self.last_scan {
            None => true,
            Some(t) => now.duration_since(t) >= RESCAN_INTERVAL,
        }
    }

    /// scan 結果を記録する。`should_rescan(now)` が true だった直後に呼ぶ。
    pub fn record_scan(&mut self, now: Instant, result: Option<PathBuf>) {
        self.last_scan = Some(now);
        self.cached_pre_dir = result;
    }
}

/// `kirin_root` (= `$TMPDIR/kirin/`) 配下を scan して active な PRE dir を返す
/// (W-280 / G-115-248: pair-aware 版)。
///
/// 戻り値は `{kirin_root}/{project_uuid}/` path (instance_id レベルでなく、その
/// 親 dir)。POST IO Thread はこれを `compute_delta_with_state(project_dir, ...,
/// pair_pre_name)` に渡す。
///
/// # pair_pre_name セマンティクス (W-280)
/// - `Some(target)` (非空文字): 各 project_dir 内で **name 一致 `pre.json`** の
///   mtime のみを採用する。pre.json の `name` field を読み出して `target` と
///   照合し、不一致は候補から除外する。これにより 2 セット環境 (PRE A + PRE B)
///   で他 PRE の mtime に引っ張られて誤 dir 採用される事を防ぐ。
/// - `None` または `Some("")`: 既存挙動 (mtime fresh + project 内 mtime 最新)。
///   後方互換 (single PRE 環境 / pair_pre_name 未設定時)。
///
/// 複数 project_uuid 候補がある場合は **mtime 最新 1 件のみ返す** (単一 PRE 前提 /
///
/// 多 PRE 環境 (複数 project_uuid に PRE が分散する) で全候補が必要な場合は
/// [`discover_active_pre_dirs`] を使う (B-027 段階 2 fix)。
///
/// 詳細は module doc 参照。
pub fn discover_active_pre_dir_for_pair(
    kirin_root: &Path,
    pair_pre_name: Option<&str>,
) -> Option<PathBuf> {
    // W-280: 空文字は filter 適用なし扱い (B-027 段階 1 pass-through セマンティクス継承)。
    let pair_target = pair_pre_name.filter(|s| !s.is_empty());

    let now = SystemTime::now();
    let stale_threshold = Duration::from_secs(DISCOVERY_STALE_SECS);

    let project_entries = fs::read_dir(kirin_root).ok()?;

    let mut candidates: Vec<(PathBuf, SystemTime)> = Vec::new();

    for project_entry in project_entries.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }

        let instance_entries = match fs::read_dir(&project_dir) {
            Ok(e) => e,
            Err(_) => continue,
        };

        let mut latest_in_project: Option<SystemTime> = None;
        for instance_entry in instance_entries.flatten() {
            let instance_dir = instance_entry.path();
            if !instance_dir.is_dir() {
                continue;
            }

            let pre_json = instance_dir.join("pre.json");
            let meta = match fs::metadata(&pre_json) {
                Ok(m) => m,
                Err(_) => continue,
            };
            if !meta.is_file() {
                continue;
            }

            if !crate::watch_snapshot_lease::snapshot_file_has_live_owner(&pre_json) {
                continue;
            }

            let mtime = match meta.modified() {
                Ok(t) => t,
                Err(_) => continue,
            };

            // stale 判定: now - mtime > threshold なら除外。
            // future mtime (clock skew で now < mtime) のときは duration_since が
            // Err。安全側で「fresh」扱いとして候補に残す。
            if let Ok(age) = now.duration_since(mtime) {
                if age > stale_threshold {
                    continue;
                }
            }

            // W-280: pair filter — Some(target) のときは pre.json の name field を
            // 読み出して一致のみ採用する。read / parse 失敗は機能的沈黙 (R-28) で skip。
            if let Some(target) = pair_target {
                let content = match fs::read_to_string(&pre_json) {
                    Ok(s) => s,
                    Err(_) => continue,
                };
                let parsed: serde_json::Value = match serde_json::from_str(&content) {
                    Ok(v) => v,
                    Err(_) => continue,
                };
                if parsed.get("name").and_then(|v| v.as_str()) != Some(target) {
                    continue;
                }
            }

            // この project_dir 内では mtime 最新を保持する。
            latest_in_project = Some(match latest_in_project {
                Some(prev) if prev > mtime => prev,
                _ => mtime,
            });
        }

        if let Some(t) = latest_in_project {
            candidates.push((project_dir, t));
        }
    }

    // mtime 降順。先頭が最新。
    candidates.sort_by_key(|candidate| std::cmp::Reverse(candidate.1));
    candidates.into_iter().next().map(|(p, _)| p)
}

/// B-027 段階 2 fix (NG-1 + NG-2): `kirin_root` 配下を scan して **mtime fresh の
/// 全 active PRE dir** を返す。
///
/// [`discover_active_pre_dir`] が単一 project_uuid 前提で「mtime 最新 1 件」のみ
/// 返すのに対し、本関数は複数 project_uuid に PRE が分散する多 PRE 環境向けに
/// **全候補を Vec で返す**。
///
/// 用途: POST `trigger_keep` で全 PRE dir を flatten scan して `pair_pre_name`
/// filter で目的 PRE を見つける (B-027 段階 2)。POST GUI ComboBox dropdown で
/// 候補順序を安定表示する (B-027 段階 3 (a) 仮説 1 / G-115-53)。
///
/// stale 判定 (`now - mtime > DISCOVERY_STALE_SECS`) と非 dir スキップは
/// `discover_active_pre_dir` と同一ロジック。空入力 / 全 stale なら空 Vec。
///
/// # B-027 段階 3 (a) 仮説 1 修正 (G-115-53)
/// 戻り値の順序を **mtime 降順 → project_uuid (= file_name) 辞書順** に変更。
/// 100ms 周期 mtime 書込 + 10 Hz 描画フレームレートで sort 結果が反転して
/// ComboBox 候補順序が高速で入れ替わる構造的問題 (#5-A-3 異常 1) を排除する。
/// project_uuid は UUID v4 文字列で session 不変・一意のため決定論的。
/// 単数版 `discover_active_pre_dir` の Δ 経路 (`io_thread_post`) は「mtime 最新 1 件」
/// セマンティクス保持のため本修正対象外。
pub fn discover_active_pre_dirs(kirin_root: &Path) -> Vec<PathBuf> {
    let now = SystemTime::now();
    let stale_threshold = Duration::from_secs(DISCOVERY_STALE_SECS);

    let project_entries = match fs::read_dir(kirin_root) {
        Ok(e) => e,
        Err(_) => return Vec::new(),
    };

    let mut candidates: Vec<(PathBuf, SystemTime)> = Vec::new();

    for project_entry in project_entries.flatten() {
        let project_dir = project_entry.path();
        if !project_dir.is_dir() {
            continue;
        }

        let instance_entries = match fs::read_dir(&project_dir) {
            Ok(e) => e,
            Err(_) => continue,
        };

        let mut latest_in_project: Option<SystemTime> = None;
        for instance_entry in instance_entries.flatten() {
            let instance_dir = instance_entry.path();
            if !instance_dir.is_dir() {
                continue;
            }

            let pre_json = instance_dir.join("pre.json");
            let meta = match fs::metadata(&pre_json) {
                Ok(m) => m,
                Err(_) => continue,
            };
            if !meta.is_file() {
                continue;
            }

            if !crate::watch_snapshot_lease::snapshot_file_has_live_owner(&pre_json) {
                continue;
            }

            let mtime = match meta.modified() {
                Ok(t) => t,
                Err(_) => continue,
            };

            if let Ok(age) = now.duration_since(mtime) {
                if age > stale_threshold {
                    continue;
                }
            }

            latest_in_project = Some(match latest_in_project {
                Some(prev) if prev > mtime => prev,
                _ => mtime,
            });
        }

        if let Some(t) = latest_in_project {
            candidates.push((project_dir, t));
        }
    }

    // B-027 段階 3 (a) 仮説 1 (G-115-53): project_uuid (= file_name) 辞書順固定。
    // mtime 降順は 100ms tick 書込 / 10 Hz draw との race で順序反転する構造的
    // 問題があった。file_name は UUID v4 文字列で session 不変・一意のため
    // 決定論的順序を保証する。
    candidates.sort_by(|a, b| a.0.file_name().cmp(&b.0.file_name()));
    candidates.into_iter().map(|(p, _)| p).collect()
}

#[cfg(test)]
#[path = "pre_discovery_tests.rs"]
mod tests;
