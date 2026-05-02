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
pub const DISCOVERY_STALE_SECS: u64 = 10;

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

/// `kirin_root` (= `$TMPDIR/kirin/`) 配下を scan して active な PRE dir を返す。
///
/// 戻り値は `{kirin_root}/{project_uuid}/` path (instance_id レベルでなく、その
/// 親 dir)。POST IO Thread はこれを `compute_delta_with_state(project_dir, ...)`
/// に渡す。
///
/// 詳細は module doc 参照。
pub fn discover_active_pre_dir(kirin_root: &Path) -> Option<PathBuf> {
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
    candidates.sort_by(|a, b| b.1.cmp(&a.1));
    candidates.into_iter().next().map(|(p, _)| p)
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::{File, FileTimes};
    use std::time::SystemTime;

    fn unique_tmp_root(label: &str) -> PathBuf {
        let pid = std::process::id();
        let n = std::time::SystemTime::now()
            .duration_since(std::time::UNIX_EPOCH)
            .unwrap()
            .as_nanos();
        let root = std::env::temp_dir().join(format!("kirin_b021_{label}_{pid}_{n}"));
        let _ = fs::remove_dir_all(&root);
        fs::create_dir_all(&root).unwrap();
        root
    }

    fn touch_pre(kirin_root: &Path, project_uuid: &str, instance_id: &str) -> PathBuf {
        let dir = kirin_root.join(project_uuid).join(instance_id);
        fs::create_dir_all(&dir).unwrap();
        let pre = dir.join("pre.json");
        fs::write(&pre, "{}").unwrap();
        pre
    }

    fn set_mtime(path: &Path, t: SystemTime) {
        let times = FileTimes::new().set_modified(t).set_accessed(t);
        let f = File::options().write(true).open(path).unwrap();
        f.set_times(times).unwrap();
    }

    #[test]
    fn discover_returns_none_for_missing_root() {
        let root = std::env::temp_dir()
            .join(format!("kirin_b021_missing_{}_{}",
                std::process::id(),
                std::time::SystemTime::now()
                    .duration_since(std::time::UNIX_EPOCH).unwrap().as_nanos()));
        // root 作らない → 不在
        assert!(!root.exists());
        assert!(discover_active_pre_dir(&root).is_none());
    }

    #[test]
    fn discover_returns_none_for_empty_root() {
        let root = unique_tmp_root("empty");
        assert!(discover_active_pre_dir(&root).is_none());
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn discover_finds_single_pre() {
        let root = unique_tmp_root("single");
        touch_pre(&root, "uuid_p1", "iid_p1");
        let result = discover_active_pre_dir(&root);
        assert_eq!(result, Some(root.join("uuid_p1")));
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn discover_picks_latest_mtime_across_projects() {
        let root = unique_tmp_root("latest");
        let old_pre = touch_pre(&root, "uuid_old", "iid_old");
        let new_pre = touch_pre(&root, "uuid_new", "iid_new");
        let now = SystemTime::now();
        set_mtime(&old_pre, now - Duration::from_secs(5));
        set_mtime(&new_pre, now - Duration::from_secs(1));

        let result = discover_active_pre_dir(&root);
        assert_eq!(result, Some(root.join("uuid_new")));
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn discover_excludes_stale_pre() {
        let root = unique_tmp_root("stale");
        let stale_pre = touch_pre(&root, "uuid_stale", "iid_stale");
        let stale_time = SystemTime::now() - Duration::from_secs(DISCOVERY_STALE_SECS + 1);
        set_mtime(&stale_pre, stale_time);

        let result = discover_active_pre_dir(&root);
        assert!(
            result.is_none(),
            "stale pre.json (>{}s old) must be excluded, got {:?}",
            DISCOVERY_STALE_SECS,
            result
        );
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn discover_skips_dirs_without_pre_json() {
        let root = unique_tmp_root("skip_no_pre");
        // pre.json なしの空 instance dir
        let empty_iid = root.join("uuid_empty").join("iid_empty");
        fs::create_dir_all(&empty_iid).unwrap();
        // pre.json 持ち
        touch_pre(&root, "uuid_real", "iid_real");

        let result = discover_active_pre_dir(&root);
        assert_eq!(result, Some(root.join("uuid_real")));
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn discover_skips_non_dir_entries() {
        let root = unique_tmp_root("skip_files");
        // file が直接置かれているケース (record_signal/ などの将来拡張で起きうる)
        fs::write(root.join("a_file.txt"), "x").unwrap();
        touch_pre(&root, "uuid_real", "iid_real");

        let result = discover_active_pre_dir(&root);
        assert_eq!(result, Some(root.join("uuid_real")));
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn discover_handles_multiple_instances_in_same_project() {
        let root = unique_tmp_root("multi_inst");
        let old_pre = touch_pre(&root, "uuid_p", "iid_old");
        let new_pre = touch_pre(&root, "uuid_p", "iid_new");
        let now = SystemTime::now();
        set_mtime(&old_pre, now - Duration::from_secs(5));
        set_mtime(&new_pre, now - Duration::from_secs(1));

        // どちらの instance も同じ project 配下なので、project_dir は 1 つだけ
        // 候補に入る。mtime は project 内最新 (= new_pre) で評価される。
        let result = discover_active_pre_dir(&root);
        assert_eq!(result, Some(root.join("uuid_p")));
        let _ = fs::remove_dir_all(&root);
    }

    // ── PostDiscoveryState の throttle 動作 ──────────────────────────────

    #[test]
    fn discovery_state_should_rescan_initially() {
        let s = PostDiscoveryState::new();
        let now = Instant::now();
        assert!(s.should_rescan(now));
    }

    #[test]
    fn discovery_state_throttles_within_one_second() {
        let mut s = PostDiscoveryState::new();
        let t0 = Instant::now();
        s.record_scan(t0, None);
        // 即座 (0ms 後) は rescan 不要
        assert!(!s.should_rescan(t0));
        // 999ms 後 でも rescan 不要
        assert!(!s.should_rescan(t0 + Duration::from_millis(999)));
        // 1000ms 後 から rescan 必要
        assert!(s.should_rescan(t0 + Duration::from_millis(1000)));
    }

    #[test]
    fn discovery_state_caches_result() {
        let mut s = PostDiscoveryState::new();
        let t0 = Instant::now();
        let p = PathBuf::from("/some/cached/dir");
        s.record_scan(t0, Some(p.clone()));
        assert_eq!(s.cached_pre_dir(), Some(p.as_path()));

        s.record_scan(t0 + Duration::from_secs(2), None);
        assert_eq!(s.cached_pre_dir(), None);
    }
}
