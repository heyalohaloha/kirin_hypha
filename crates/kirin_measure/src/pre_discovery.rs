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
        let root = std::env::temp_dir().join(format!(
            "kirin_b021_missing_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        ));
        // root 作らない → 不在
        assert!(!root.exists());
        assert!(discover_active_pre_dir_for_pair(&root, None).is_none());
    }

    #[test]
    fn discover_returns_none_for_empty_root() {
        let root = unique_tmp_root("empty");
        assert!(discover_active_pre_dir_for_pair(&root, None).is_none());
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn discover_finds_single_pre() {
        let root = unique_tmp_root("single");
        touch_pre(&root, "uuid_p1", "iid_p1");
        let result = discover_active_pre_dir_for_pair(&root, None);
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

        let result = discover_active_pre_dir_for_pair(&root, None);
        assert_eq!(result, Some(root.join("uuid_new")));
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn discover_excludes_stale_pre() {
        let root = unique_tmp_root("stale");
        let stale_pre = touch_pre(&root, "uuid_stale", "iid_stale");
        let stale_time = SystemTime::now() - Duration::from_secs(DISCOVERY_STALE_SECS + 1);
        set_mtime(&stale_pre, stale_time);

        let result = discover_active_pre_dir_for_pair(&root, None);
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

        let result = discover_active_pre_dir_for_pair(&root, None);
        assert_eq!(result, Some(root.join("uuid_real")));
        let _ = fs::remove_dir_all(&root);
    }

    #[test]
    fn discover_skips_non_dir_entries() {
        let root = unique_tmp_root("skip_files");
        // file が直接置かれているケース (record_signal/ などの将来拡張で起きうる)
        fs::write(root.join("a_file.txt"), "x").unwrap();
        touch_pre(&root, "uuid_real", "iid_real");

        let result = discover_active_pre_dir_for_pair(&root, None);
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
        let result = discover_active_pre_dir_for_pair(&root, None);
        assert_eq!(result, Some(root.join("uuid_p")));
        let _ = fs::remove_dir_all(&root);
    }

    // ── B-027 段階 2 fix: discover_active_pre_dirs (NG-1 + NG-2) ─────────

    /// 複数 project_uuid 配下の PRE が全件 **project_uuid 辞書順 Vec** で返る。
    /// B-027 段階 3 (a) 仮説 1 (G-115-53): 旧版 mtime 降順 assert を辞書順に更新。
    #[test]
    fn discover_active_pre_dirs_returns_all_fresh() {
        let root = unique_tmp_root("multi_dirs_fresh");
        let pre_a = touch_pre(&root, "uuid_a", "iid_a");
        let pre_b = touch_pre(&root, "uuid_b", "iid_b");
        let now = SystemTime::now();
        set_mtime(&pre_a, now - Duration::from_secs(3)); // 旧 mtime (sort 結果に影響しない)
        set_mtime(&pre_b, now - Duration::from_secs(1)); // 新 mtime (sort 結果に影響しない)

        let result = discover_active_pre_dirs(&root);
        assert_eq!(result.len(), 2, "fresh な 2 project_uuid 両方返却");
        // project_uuid 辞書順 ("uuid_a" < "uuid_b") / mtime 非依存
        assert_eq!(result[0], root.join("uuid_a"));
        assert_eq!(result[1], root.join("uuid_b"));
        let _ = fs::remove_dir_all(&root);
    }

    /// B-027 段階 3 (a) 仮説 1 (G-115-53): mtime jitter 下でも sort 結果が決定論的。
    /// 同じ kirin_root を mtime を交互に更新した上で複数回 discover を呼び、
    /// 全回で結果が完全一致することを assert (project_uuid 辞書順固定の構造保証)。
    #[test]
    fn discover_active_pre_dirs_sort_is_deterministic_under_mtime_jitter() {
        let root = unique_tmp_root("deterministic");
        let pre_a = touch_pre(&root, "uuid_aaaa", "iid_a");
        let pre_b = touch_pre(&root, "uuid_bbbb", "iid_b");
        let pre_c = touch_pre(&root, "uuid_cccc", "iid_c");

        let expected = vec![
            root.join("uuid_aaaa"),
            root.join("uuid_bbbb"),
            root.join("uuid_cccc"),
        ];

        // 1 回目: A → B → C 順で mtime 更新 (C が最新)
        let now = SystemTime::now();
        set_mtime(&pre_a, now - Duration::from_secs(3));
        set_mtime(&pre_b, now - Duration::from_secs(2));
        set_mtime(&pre_c, now - Duration::from_secs(1));
        let r1 = discover_active_pre_dirs(&root);

        // 2 回目: 逆順に mtime 更新 (A が最新)
        set_mtime(&pre_c, now - Duration::from_secs(3));
        set_mtime(&pre_b, now - Duration::from_secs(2));
        set_mtime(&pre_a, now - Duration::from_secs(1));
        let r2 = discover_active_pre_dirs(&root);

        // 3 回目: B が最新
        set_mtime(&pre_a, now - Duration::from_secs(3));
        set_mtime(&pre_c, now - Duration::from_secs(2));
        set_mtime(&pre_b, now - Duration::from_secs(1));
        let r3 = discover_active_pre_dirs(&root);

        assert_eq!(r1, expected, "1st call: project_uuid 辞書順");
        assert_eq!(r2, expected, "2nd call: mtime 逆転しても順序不変");
        assert_eq!(r3, expected, "3rd call: mtime jitter 中も順序不変");
        let _ = fs::remove_dir_all(&root);
    }

    /// stale (`> DISCOVERY_STALE_SECS`) は除外され、fresh のみ返る。
    #[test]
    fn discover_active_pre_dirs_excludes_stale() {
        let root = unique_tmp_root("multi_dirs_stale");
        let fresh = touch_pre(&root, "uuid_fresh", "iid_f");
        let stale = touch_pre(&root, "uuid_stale", "iid_s");
        let now = SystemTime::now();
        set_mtime(&fresh, now - Duration::from_secs(2));
        set_mtime(&stale, now - Duration::from_secs(DISCOVERY_STALE_SECS + 1));

        let result = discover_active_pre_dirs(&root);
        assert_eq!(result.len(), 1, "stale は除外され fresh のみ");
        assert_eq!(result[0], root.join("uuid_fresh"));
        let _ = fs::remove_dir_all(&root);
    }

    /// kirin_root が空 / 不在 → 空 Vec。
    #[test]
    fn discover_active_pre_dirs_empty_when_no_pre() {
        let root = unique_tmp_root("multi_dirs_empty");
        // pre.json なしの空 instance dir のみ
        fs::create_dir_all(root.join("uuid_x").join("iid_x")).unwrap();

        let result = discover_active_pre_dirs(&root);
        assert!(result.is_empty(), "pre.json 不在で空 Vec");
        let _ = fs::remove_dir_all(&root);
    }

    /// flatten 経路の単体テスト (B-027 段階 2 fix / cdylib 外検証):
    /// 2 project_uuid 配下に PRE 1 ずつ配置 → discover_active_pre_dirs +
    /// scan_pre_candidates_in flatten で 2 候補返る → filter_candidates_by_name
    /// で目的 Name 1 件絞れる。NG-2 構造の修正経路を担保。
    #[test]
    fn discover_active_pre_dirs_then_scan_flatten() {
        use crate::pre_candidates::{filter_candidates_by_name, scan_pre_candidates_in};
        let root = unique_tmp_root("flatten");
        // 2 つの project_uuid 配下にそれぞれ PRE 1 つ
        let pre_a_dir = root.join("uuid_a").join("iid_a");
        fs::create_dir_all(&pre_a_dir).unwrap();
        let json_a = r#"{"v":2,"role":"PRE","instance_id":"iid_a","name":"snare","signal_state":"active","t":"2026-05-04T00:00:00.000Z","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}"#;
        fs::write(pre_a_dir.join("pre.json"), json_a).unwrap();

        let pre_b_dir = root.join("uuid_b").join("iid_b");
        fs::create_dir_all(&pre_b_dir).unwrap();
        let json_b = r#"{"v":2,"role":"PRE","instance_id":"iid_b","name":"kick","signal_state":"active","t":"2026-05-04T00:00:00.000Z","lufs_m":-15.0,"true_peak":-2.0,"crest":11.0,"psr":7.0}"#;
        fs::write(pre_b_dir.join("pre.json"), json_b).unwrap();

        // discover で 2 dir 取得
        let dirs = discover_active_pre_dirs(&root);
        assert_eq!(dirs.len(), 2, "2 project_uuid 両方候補化");

        // flatten で 2 候補統合
        let candidates: Vec<_> = dirs
            .iter()
            .flat_map(|d| scan_pre_candidates_in(d))
            .collect();
        assert_eq!(candidates.len(), 2, "flatten で 2 PRE 候補");

        // Name filter で目的 1 件
        let filtered = filter_candidates_by_name(candidates, "snare");
        assert_eq!(filtered.len(), 1);
        assert_eq!(filtered[0].instance_id, "iid_a");
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
