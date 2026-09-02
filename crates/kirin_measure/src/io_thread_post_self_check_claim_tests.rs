use super::*;
use std::sync::atomic::AtomicU64;

fn unique_root(label: &str) -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let now = SystemTime::now()
        .duration_since(SystemTime::UNIX_EPOCH)
        .unwrap()
        .as_nanos();
    let root = std::env::temp_dir().join(format!("kirin_post_{label}_{pid}_{now}_{n}"));
    let _ = fs::remove_dir_all(&root);
    fs::create_dir_all(&root).unwrap();
    root
}

// ── W-281 / G-115-249 / C-5: self_check_pair_claim テスト 5 件 ─────────

/// write_post_json の拡張版: pair_claimed_at 値を指定して post.json を書く。
fn write_post_json_with_claim(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    pair_pre_name: &str,
    pair_claimed_at: f64,
) -> PathBuf {
    let dir = kirin_root.join(project_uuid).join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let post_file = dir.join("post.json");
    let json = serialize_post_json(
        instance_id,
        SignalState::Active,
        Some(SignalState::Active),
        &MeasureResult::default(),
        pair_pre_name,
        pair_claimed_at,
    );
    fs::write(&post_file, json.as_bytes()).unwrap();
    post_file
}

fn write_post_json_with_exact_claim(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    pair_pre_name: &str,
    paired_pre_instance_id: &str,
    pair_claimed_at: f64,
) -> PathBuf {
    let dir = kirin_root.join(project_uuid).join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let post_file = dir.join("post.json");
    let json = serialize_post_json_with_daw_owner_and_pair_instance(
        instance_id,
        SignalState::Active,
        Some(SignalState::Active),
        &MeasureResult::default(),
        pair_pre_name,
        pair_claimed_at,
        "",
        "",
        paired_pre_instance_id,
    );
    fs::write(&post_file, json.as_bytes()).unwrap();
    post_file
}

/// (C-5 i) 他 POST が自分より新しい claim → release 必要 (true)。
#[test]
fn self_check_returns_true_when_other_post_has_newer_claim() {
    let root = unique_root("self_check_newer");
    let project_uuid = "pj-X";
    write_post_json_with_claim(&root, project_uuid, "post-self", "PRE-A", 100.0);
    write_post_json_with_claim(&root, project_uuid, "post-other", "PRE-A", 200.0);
    let project_dir = root.join(project_uuid);
    assert!(self_check_pair_claim(
        &project_dir,
        "post-self",
        "PRE-A",
        100.0
    ));
}

/// (C-5 ii) 他 POST が別 PRE / 該当なし → release 不要 (false)。
#[test]
fn self_check_returns_false_when_no_overlap() {
    let root = unique_root("self_check_no_overlap");
    let project_uuid = "pj-X";
    write_post_json_with_claim(&root, project_uuid, "post-self", "PRE-A", 100.0);
    write_post_json_with_claim(&root, project_uuid, "post-other", "PRE-B", 200.0);
    let project_dir = root.join(project_uuid);
    assert!(!self_check_pair_claim(
        &project_dir,
        "post-self",
        "PRE-A",
        100.0
    ));
}

/// (C-5 iii) tie-break: pair_claimed_at 同値 + 自 id 大 → release 必要 (true)。
#[test]
fn self_check_returns_true_on_tiebreak_when_self_id_is_larger() {
    let root = unique_root("self_check_tie_larger");
    let project_uuid = "pj-X";
    // 自 id = "post-Z" (lex 大) / other id = "post-A" (lex 小)
    write_post_json_with_claim(&root, project_uuid, "post-Z", "PRE-A", 100.0);
    write_post_json_with_claim(&root, project_uuid, "post-A", "PRE-A", 100.0);
    let project_dir = root.join(project_uuid);
    assert!(self_check_pair_claim(
        &project_dir,
        "post-Z",
        "PRE-A",
        100.0
    ));
}

/// (C-5 iv) tie-break: pair_claimed_at 同値 + 自 id 小 → release 不要 (false)。
#[test]
fn self_check_returns_false_on_tiebreak_when_self_id_is_smaller() {
    let root = unique_root("self_check_tie_smaller");
    let project_uuid = "pj-X";
    write_post_json_with_claim(&root, project_uuid, "post-A", "PRE-A", 100.0);
    write_post_json_with_claim(&root, project_uuid, "post-Z", "PRE-A", 100.0);
    let project_dir = root.join(project_uuid);
    assert!(!self_check_pair_claim(
        &project_dir,
        "post-A",
        "PRE-A",
        100.0
    ));
}

/// (C-5 v) 自 pair_pre_name 空 → release 不要 (false / 即 return)。
#[test]
fn self_check_returns_false_when_self_pair_pre_name_is_empty() {
    let root = unique_root("self_check_empty");
    let project_uuid = "pj-X";
    // 他 POST が pair claim 中でも自身が pair 未設定なら不要。
    write_post_json_with_claim(&root, project_uuid, "post-other", "PRE-A", 200.0);
    let project_dir = root.join(project_uuid);
    assert!(!self_check_pair_claim(&project_dir, "post-self", "", 0.0));
}

#[test]
fn self_check_distinguishes_exact_instances_with_the_same_name() {
    let root = unique_root("self_check_exact_same_name");
    let project_uuid = "pj-X";
    write_post_json_with_exact_claim(
        &root,
        project_uuid,
        "post-self",
        "PRE-A",
        "pre-instance-a",
        100.0,
    );
    write_post_json_with_exact_claim(
        &root,
        project_uuid,
        "post-other",
        "PRE-A",
        "pre-instance-b",
        200.0,
    );
    assert!(!self_check_pair_claim_exact(
        &root.join(project_uuid),
        "post-self",
        "PRE-A",
        "pre-instance-a",
        100.0,
    ));
}

#[test]
fn self_check_matches_exact_instance_even_if_display_name_changed() {
    let root = unique_root("self_check_exact_renamed");
    let project_uuid = "pj-X";
    write_post_json_with_exact_claim(
        &root,
        project_uuid,
        "post-other",
        "RENAMED",
        "pre-instance-a",
        200.0,
    );
    assert!(self_check_pair_claim_exact(
        &root.join(project_uuid),
        "post-self",
        "PRE-A",
        "pre-instance-a",
        100.0,
    ));
}

#[test]
fn self_check_keeps_bypassed_post_claim_exclusive() {
    let root = unique_root("self_check_exact_bypassed");
    let project_uuid = "pj-X";
    let post_file = write_post_json_with_exact_claim(
        &root,
        project_uuid,
        "post-other",
        "PRE-A",
        "pre-instance-a",
        200.0,
    );
    let mut json: serde_json::Value =
        serde_json::from_slice(&fs::read(&post_file).unwrap()).unwrap();
    json["signal_state"] = serde_json::json!("bypassed");
    fs::write(post_file, json.to_string()).unwrap();

    assert!(self_check_pair_claim_exact(
        &root.join(project_uuid),
        "post-self",
        "PRE-A",
        "pre-instance-a",
        100.0,
    ));
}
