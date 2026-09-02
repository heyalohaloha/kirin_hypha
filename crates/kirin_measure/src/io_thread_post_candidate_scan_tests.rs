use super::*;
use crate::host_scope_has_other_active_post_project;
use std::sync::atomic::AtomicU64;

fn unique_root(label: &str) -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let now = std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let dir = std::env::temp_dir().join(format!("kirin_post_cand_{label}_{pid}_{n}_{now}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn write_post_json(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    signal_state: SignalState,
    pre_signal_state: Option<SignalState>,
    pair_pre_name: &str,
) -> PathBuf {
    let dir = kirin_root.join(project_uuid).join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let post_file = dir.join("post.json");
    let json = match signal_state {
        SignalState::Active => serialize_post_json(
            instance_id,
            signal_state,
            pre_signal_state,
            &MeasureResult::default(),
            pair_pre_name,
            0.0,
        ),
        _ => serialize_post_json_minimal(instance_id, signal_state, pair_pre_name, 0.0),
    };
    fs::write(&post_file, json.as_bytes()).unwrap();
    post_file
}

/// scan_post_candidates_in: 通常 case (Active 1 件) → instance_id / project_uuid /
/// pair_pre_name (空文字 → None / 非空 → Some) / path が正しく構築される。
#[test]
fn scan_in_active_with_pair_pre_name() {
    let root = unique_root("scan_active");
    let project_uuid = "pj-AAA";
    let _ = write_post_json(
        &root,
        project_uuid,
        "post-iid-1",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-Master",
    );
    let project_dir = root.join(project_uuid);
    let cands = scan_post_candidates_in(&project_dir);
    assert_eq!(cands.len(), 1);
    let c = &cands[0];
    assert_eq!(c.instance_id, "post-iid-1");
    assert_eq!(c.project_uuid, project_uuid);
    assert!(c.daw_session_id.is_none());
    assert_eq!(c.host_process_id, Some(crate::current_host_process_id()));
    assert_eq!(c.pair_pre_name.as_deref(), Some("PRE-Master"));
    assert!(c.path.ends_with("post.json"));
}

#[test]
fn released_post_runtime_cannot_block_pair_scope_for_thirty_seconds() {
    let root = unique_root("released_post_scope");
    let project_uuid = "pj-OLD";
    let post_file = write_post_json(
        &root,
        project_uuid,
        "post-old",
        SignalState::Active,
        Some(SignalState::Active),
        "2Mix",
    );
    let instance_dir = post_file.parent().unwrap();
    let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    lease.bind(instance_dir).unwrap();
    let mut json: serde_json::Value =
        serde_json::from_slice(&fs::read(&post_file).unwrap()).unwrap();
    json["watch_owner_id"] = serde_json::json!(lease.owner_id());
    fs::write(&post_file, serde_json::to_vec(&json).unwrap()).unwrap();
    let host_process_id = crate::current_host_process_id();
    assert!(host_scope_has_other_active_post_project(
        &root,
        "pj-CURRENT",
        host_process_id
    ));

    drop(lease);
    assert!(
        !host_scope_has_other_active_post_project(&root, "pj-CURRENT", host_process_id),
        "a normally removed POST must stop excluding every PRE immediately"
    );
}

/// B-131 (G-115-380) 感度確証: `"` / `\` を含む pair_pre_name の POST が
/// serialize → scan の往復で pairing 候補から **消えない**。
/// 旧: 生補間 → 不正 JSON → `scan_post_candidates_in` が無言 skip → pairing 消失
/// （PRE 選択済でも対 POST が候補から欠落し All Keep / pair が成立しない R-28 欠陥）。
#[test]
fn scan_in_survives_special_char_pair_pre_name() {
    let root = unique_root("scan_special");
    let project_uuid = "pj-SPECIAL";
    let name = "PRE\"x\\y";
    let _ = write_post_json(
        &root,
        project_uuid,
        "post-iid-1",
        SignalState::Active,
        Some(SignalState::Active),
        name,
    );
    let project_dir = root.join(project_uuid);
    let cands = scan_post_candidates_in(&project_dir);
    assert_eq!(
        cands.len(),
        1,
        "special-char pair_pre_name POST must survive scan (not silently skipped)"
    );
    assert_eq!(cands[0].pair_pre_name.as_deref(), Some(name));
}

/// B-131 (G-115-380): 真に壊れた post.json は無言 skip されず log surface され、かつ
/// 同 dir の valid POST 候補は返る（不正 1 件が sibling を巻き込まない / R-28 sweep 継続）。
/// PRE 側 pre_candidates scan (B-077) の log::warn surface と対称。
#[test]
fn scan_in_skips_corrupt_but_keeps_valid_sibling() {
    let root = unique_root("scan_corrupt");
    let project_uuid = "pj-CORRUPT";
    let _ = write_post_json(
        &root,
        project_uuid,
        "post-good",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-Good",
    );
    // 故意に壊した post.json（不正 JSON）。
    let bad_dir = root.join(project_uuid).join("post-bad");
    fs::create_dir_all(&bad_dir).unwrap();
    fs::write(bad_dir.join("post.json"), b"{ not valid json").unwrap();

    let project_dir = root.join(project_uuid);
    let cands = scan_post_candidates_in(&project_dir);
    assert_eq!(cands.len(), 1, "corrupt skipped, valid sibling kept");
    assert_eq!(cands[0].instance_id, "post-good");
}

/// pair_pre_name が空文字 → PostCandidate.pair_pre_name == None (PRE 版 name None
/// 対称)。
#[test]
fn scan_in_empty_pair_pre_name_to_none() {
    let root = unique_root("scan_empty");
    let project_uuid = "pj-BBB";
    let _ = write_post_json(
        &root,
        project_uuid,
        "post-iid-2",
        SignalState::Active,
        Some(SignalState::Active),
        "",
    );
    let cands = scan_post_candidates_in(&root.join(project_uuid));
    assert_eq!(cands.len(), 1);
    assert!(cands[0].pair_pre_name.is_none());
}

/// signal_state == "bypassed" の POST は候補から除外 (PRE 版 Bypass 防御対称)。
#[test]
fn scan_in_bypassed_excluded() {
    let root = unique_root("scan_bypass");
    let project_uuid = "pj-CCC";
    let _ = write_post_json(
        &root,
        project_uuid,
        "post-iid-active",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-X",
    );
    let _ = write_post_json(
        &root,
        project_uuid,
        "post-iid-bypassed",
        SignalState::Bypassed,
        None,
        "PRE-Y",
    );
    let cands = scan_post_candidates_in(&root.join(project_uuid));
    assert_eq!(cands.len(), 1, "only Active POST remains: {:?}", cands);
    assert_eq!(cands[0].instance_id, "post-iid-active");
}

/// signal_state == "inactive" の POST は候補化される (PRE 版対称)。
#[test]
fn scan_in_inactive_included() {
    let root = unique_root("scan_inactive");
    let project_uuid = "pj-DDD";
    let _ = write_post_json(
        &root,
        project_uuid,
        "post-iid-inactive",
        SignalState::Inactive,
        None,
        "PRE-Z",
    );
    let cands = scan_post_candidates_in(&root.join(project_uuid));
    assert_eq!(cands.len(), 1);
    assert_eq!(cands[0].instance_id, "post-iid-inactive");
}

/// 旧 schema (pair_pre_name field 不在) の post.json → pair_pre_name == None。
#[test]
fn scan_in_legacy_schema_no_pair_pre_name_field() {
    let root = unique_root("scan_legacy");
    let project_uuid = "pj-EEE";
    let dir = root.join(project_uuid).join("post-iid-legacy");
    fs::create_dir_all(&dir).unwrap();
    let legacy = r#"{"v":2,"role":"POST","instance_id":"post-iid-legacy","signal_state":"active","pre_signal_state":"active","t":"2026-05-04T10:00:00.000Z","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}"#;
    fs::write(dir.join("post.json"), legacy).unwrap();
    let cands = scan_post_candidates_in(&root.join(project_uuid));
    assert_eq!(cands.len(), 1);
    assert!(cands[0].pair_pre_name.is_none());
}

/// scan_post_candidates_in: SIGNALS_SUBDIR (record_signal/) は除外。
#[test]
fn scan_in_excludes_signals_subdir() {
    let root = unique_root("scan_signals");
    let project_uuid = "pj-FFF";
    let project_dir = root.join(project_uuid);
    fs::create_dir_all(project_dir.join(SIGNALS_SUBDIR)).unwrap();
    // SIGNALS_SUBDIR 内に post.json があっても候補化されないこと。
    fs::write(
        project_dir.join(SIGNALS_SUBDIR).join("post.json"),
        r#"{"instance_id":"x","signal_state":"active","t":"x"}"#,
    )
    .unwrap();
    let _ = write_post_json(
        &root,
        project_uuid,
        "post-iid-real",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-Q",
    );
    let cands = scan_post_candidates_in(&project_dir);
    assert_eq!(cands.len(), 1);
    assert_eq!(cands[0].instance_id, "post-iid-real");
}

/// scan_post_candidates_in: 戻り順は instance_id 辞書順 (PRE 版対称 / 再現性)。
#[test]
fn scan_in_sorted_by_instance_id() {
    let root = unique_root("scan_sort");
    let project_uuid = "pj-GGG";
    for iid in &["post-c", "post-a", "post-b"] {
        let _ = write_post_json(
            &root,
            project_uuid,
            iid,
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-S",
        );
    }
    let cands = scan_post_candidates_in(&root.join(project_uuid));
    let ids: Vec<&str> = cands.iter().map(|c| c.instance_id.as_str()).collect();
    assert_eq!(ids, vec!["post-a", "post-b", "post-c"]);
}

/// scan_post_candidates_in: 不在 / 非 dir 入力 → 空 Vec (silently skip)。
#[test]
fn scan_in_missing_dir_returns_empty() {
    let nonexistent = std::env::temp_dir().join("kirin_post_cand_does_not_exist");
    let _ = fs::remove_dir_all(&nonexistent);
    let cands = scan_post_candidates_in(&nonexistent);
    assert!(cands.is_empty());
}

/// discover_active_post_dirs: fresh post.json を持つ project_uuid dir のみ列挙。
#[test]
fn discover_returns_fresh_dirs() {
    let root = unique_root("discover_fresh");
    // 2 project_uuid dir / それぞれ Active POST 1 件
    let _ = write_post_json(
        &root,
        "pj-AA",
        "post-1",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-1",
    );
    let _ = write_post_json(
        &root,
        "pj-BB",
        "post-2",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-2",
    );
    let dirs = discover_active_post_dirs(&root);
    let names: Vec<String> = dirs
        .iter()
        .map(|d| d.file_name().unwrap().to_string_lossy().to_string())
        .collect();
    assert_eq!(names, vec!["pj-AA".to_string(), "pj-BB".to_string()]);
}

/// discover_active_post_dirs: 戻り順は file_name 辞書順固定 (G-115-53 対称)。
#[test]
fn discover_sorted_by_file_name() {
    let root = unique_root("discover_sort");
    for pj in &["pj-CC", "pj-AA", "pj-BB"] {
        let _ = write_post_json(
            &root,
            pj,
            "post-x",
            SignalState::Active,
            Some(SignalState::Active),
            "PRE-X",
        );
    }
    let dirs = discover_active_post_dirs(&root);
    let names: Vec<String> = dirs
        .iter()
        .map(|d| d.file_name().unwrap().to_string_lossy().to_string())
        .collect();
    assert_eq!(names, vec!["pj-AA", "pj-BB", "pj-CC"]);
}

/// discover_active_post_dirs: 空 root / 不在 → 空 Vec。
#[test]
fn discover_empty_root_returns_empty() {
    let nonexistent = std::env::temp_dir().join("kirin_post_cand_discover_does_not_exist");
    let _ = fs::remove_dir_all(&nonexistent);
    let dirs = discover_active_post_dirs(&nonexistent);
    assert!(dirs.is_empty());
    let empty_root = unique_root("discover_empty");
    let dirs2 = discover_active_post_dirs(&empty_root);
    assert!(dirs2.is_empty());
}

/// discover_active_post_dirs: stale (mtime > DISCOVERY_STALE_SECS) は除外。
/// post.json mtime を過去に書き戻してチェック。
#[test]
fn discover_excludes_stale_dirs() {
    use std::fs::{File, FileTimes};
    let root = unique_root("discover_stale");
    let fresh = write_post_json(
        &root,
        "pj-FRESH",
        "post-fresh",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-F",
    );
    let stale = write_post_json(
        &root,
        "pj-STALE",
        "post-stale",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-S",
    );
    // stale 側 mtime を threshold より古く設定。
    let old = SystemTime::now() - Duration::from_secs(DISCOVERY_STALE_SECS + 5);
    let times = FileTimes::new().set_modified(old).set_accessed(old);
    File::options()
        .write(true)
        .open(&stale)
        .unwrap()
        .set_times(times)
        .unwrap();
    // 不変監視: fresh 側は手を入れない。
    let _ = fresh;

    let dirs = discover_active_post_dirs(&root);
    let names: Vec<String> = dirs
        .iter()
        .map(|d| d.file_name().unwrap().to_string_lossy().to_string())
        .collect();
    assert_eq!(names, vec!["pj-FRESH".to_string()]);
}
