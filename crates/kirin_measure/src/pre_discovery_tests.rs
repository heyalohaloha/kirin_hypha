
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
