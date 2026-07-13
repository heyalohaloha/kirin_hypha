use super::*;

use chrono::Utc;
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::sync::Mutex;

fn isolated_dir() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_pairing_scope_test_{pid}_{n}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn write_pre_for_select(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    name: &str,
    signal_state: &str,
    t_rfc3339: &str,
) {
    write_pre_for_select_with_host_process_id(
        kirin_root,
        project_uuid,
        instance_id,
        name,
        signal_state,
        t_rfc3339,
        None,
    );
}

fn write_pre_for_select_with_host_process_id(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    name: &str,
    signal_state: &str,
    t_rfc3339: &str,
    host_process_id: Option<u32>,
) {
    write_pre_for_select_with_identity(
        kirin_root,
        project_uuid,
        instance_id,
        name,
        signal_state,
        t_rfc3339,
        host_process_id,
        None,
    );
}

#[allow(clippy::too_many_arguments)]
fn write_pre_for_select_with_identity(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    name: &str,
    signal_state: &str,
    t_rfc3339: &str,
    host_process_id: Option<u32>,
    daw_session_id: Option<&str>,
) {
    let dir = kirin_root.join(project_uuid).join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let mut json = serde_json::json!({
        "v": 2,
        "role": "PRE",
        "instance_id": instance_id,
        "name": name,
        "signal_state": signal_state,
        "t": t_rfc3339,
        "lufs_m": -14.0,
        "true_peak": -1.0,
        "crest": 12.0
    });
    if let Some(pid) = host_process_id {
        json["host_process_id"] = serde_json::json!(pid);
    }
    if let Some(daw) = daw_session_id {
        json["daw_session_id"] = serde_json::json!(daw);
    }
    fs::write(dir.join("pre.json"), json.to_string()).unwrap();
}

fn write_post_for_scope(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    pair_pre_name: &str,
) {
    write_post_for_scope_with_host_process_id(
        kirin_root,
        project_uuid,
        instance_id,
        pair_pre_name,
        None,
    );
}

fn write_post_for_scope_with_host_process_id(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    pair_pre_name: &str,
    host_process_id: Option<u32>,
) {
    write_post_for_scope_with_identity(
        kirin_root,
        project_uuid,
        instance_id,
        pair_pre_name,
        host_process_id,
        None,
    );
}

fn write_post_for_scope_with_identity(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    pair_pre_name: &str,
    host_process_id: Option<u32>,
    daw_session_id: Option<&str>,
) {
    let dir = kirin_root.join(project_uuid).join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let mut json = serde_json::json!({
        "v": 2,
        "role": "POST",
        "instance_id": instance_id,
        "signal_state": "inactive",
        "t": now_rfc3339(),
        "pair_pre_name": pair_pre_name,
        "pair_claimed_at": 1.0
    });
    if let Some(pid) = host_process_id {
        json["host_process_id"] = serde_json::json!(pid);
    }
    if let Some(daw) = daw_session_id {
        json["daw_session_id"] = serde_json::json!(daw);
    }
    fs::write(dir.join("post.json"), json.to_string()).unwrap();
}

fn now_rfc3339() -> String {
    Utc::now().format("%Y-%m-%dT%H:%M:%SZ").to_string()
}

fn old_rfc3339(secs_ago: i64) -> String {
    (Utc::now() - chrono::Duration::seconds(secs_ago))
        .format("%Y-%m-%dT%H:%M:%SZ")
        .to_string()
}

fn other_host_process_id() -> u32 {
    let current = std::process::id();
    if current == u32::MAX {
        current - 1
    } else {
        current + 1
    }
}

fn attach_live_owner(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
) -> crate::watch_snapshot_lease::WatchSnapshotLease {
    let instance_dir = kirin_root.join(project_uuid).join(instance_id);
    let mut lease = crate::watch_snapshot_lease::WatchSnapshotLease::new();
    lease.bind(&instance_dir).unwrap();
    let path = instance_dir.join("pre.json");
    let mut json: serde_json::Value = serde_json::from_slice(&fs::read(&path).unwrap()).unwrap();
    json["watch_owner_id"] = serde_json::json!(lease.owner_id());
    fs::write(path, json.to_string()).unwrap();
    lease
}

#[test]
fn select_target_pre_unique_active_fresh_returns_some() {
    let root = isolated_dir();
    write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now_rfc3339());
    let sel = select_target_pre(&root, "snare").expect("unique active fresh -> Some");
    assert_eq!(sel.instance_id, "iid-A");
    assert!(sel.pre_json.ends_with("pre.json"));
}

#[test]
fn select_target_pre_ambiguous_same_name_returns_none() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now);
    write_pre_for_select(&root, "puid-2", "iid-B", "snare", "active", &now);
    assert!(select_target_pre(&root, "snare").is_none());
}

#[test]
fn select_target_pre_empty_name_returns_none() {
    let root = isolated_dir();
    write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now_rfc3339());
    assert!(select_target_pre(&root, "").is_none());
}

#[test]
fn select_target_pre_inactive_excluded() {
    let root = isolated_dir();
    write_pre_for_select(
        &root,
        "puid-1",
        "iid-A",
        "snare",
        "inactive",
        &now_rfc3339(),
    );
    assert!(select_target_pre(&root, "snare").is_none());
}

#[test]
fn select_target_pre_stale_t_excluded() {
    let root = isolated_dir();
    write_pre_for_select(
        &root,
        "puid-1",
        "iid-A",
        "snare",
        "active",
        &old_rfc3339(20),
    );
    assert!(select_target_pre(&root, "snare").is_none());
}

#[test]
fn select_target_pre_display_equals_commit() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select(&root, "puid-1", "iid-snare", "snare", "active", &now);
    write_pre_for_select(&root, "puid-2", "iid-kick", "kick", "active", &now);
    assert_eq!(
        select_target_pre(&root, "snare").unwrap().instance_id,
        "iid-snare"
    );
    assert_eq!(
        select_target_pre(&root, "kick").unwrap().instance_id,
        "iid-kick"
    );
    assert!(select_target_pre(&root, "vocal").is_none());
}

#[test]
fn enumerate_active_pre_pair_candidates_for_post_project_lists_all_fresh_pre_dirs() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select(&root, "pre-old", "iid-old-snare", "Snare", "inactive", &now);
    write_pre_for_select(&root, "pre-old", "iid-old-kick", "Kick", "inactive", &now);
    write_pre_for_select(
        &root,
        "pre-current",
        "iid-current-vocal",
        "Vocal",
        "inactive",
        &now,
    );
    write_pre_for_select(
        &root,
        "pre-current",
        "iid-current-bass",
        "Bass",
        "inactive",
        &now,
    );
    write_post_for_scope(&root, "post-current", "post-vocal", "Vocal");
    write_post_for_scope(&root, "post-current", "post-bass", "Bass");

    let v = enumerate_active_pre_pair_candidates_for_post_project(&root, "post-current");
    let ids: Vec<&str> = v.iter().map(|c| c.instance_id.as_str()).collect();
    assert_eq!(
        ids,
        vec![
            "iid-current-bass",
            "iid-current-vocal",
            "iid-old-kick",
            "iid-old-snare"
        ]
    );
}

#[test]
fn post_project_with_existing_drum_still_lists_and_selects_second_mix() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select_with_identity(
        &root,
        "pre-drum",
        "iid-drum",
        "Drum",
        "inactive",
        &now,
        None,
        Some("daw-current"),
    );
    write_pre_for_select_with_identity(
        &root,
        "pre-mix",
        "iid-mix",
        "Mix",
        "inactive",
        &now,
        None,
        Some("daw-current"),
    );
    write_post_for_scope(&root, "post-current", "post-drum", "Drum");

    let v = enumerate_active_pre_pair_candidates_for_post_project_in_session(
        &root,
        "post-current",
        "daw-current",
    );
    let names: Vec<&str> = v.iter().filter_map(|c| c.name.as_deref()).collect();
    assert!(names.contains(&"Mix"));

    let sel = select_target_pre_for_arm_for_post_project_in_session(
        &root,
        "Mix",
        "post-current",
        "daw-current",
    )
    .expect("unique Mix should remain armable when Drum is already paired");
    assert_eq!(sel.instance_id, "iid-mix");
}

#[test]
fn enumerate_active_pre_pair_candidates_for_post_project_in_session_hides_foreign_daw_when_other_post_project_is_active(
) {
    let root = isolated_dir();
    let now = now_rfc3339();
    let current_host = std::process::id();
    write_pre_for_select_with_identity(
        &root,
        "pre-old",
        "iid-old-mix",
        "Mix",
        "inactive",
        &now,
        Some(current_host),
        Some("daw-old"),
    );
    write_pre_for_select_with_identity(
        &root,
        "post-current",
        "iid-current-mix",
        "Mix",
        "inactive",
        &now,
        Some(current_host),
        Some("daw-current"),
    );
    write_post_for_scope_with_identity(
        &root,
        "post-current",
        "post-current",
        "Mix",
        Some(current_host),
        Some("daw-current"),
    );
    write_post_for_scope_with_identity(
        &root,
        "post-old",
        "post-old",
        "Mix",
        Some(current_host),
        Some("daw-old"),
    );

    let v = enumerate_active_pre_pair_candidates_for_post_project_in_session(
        &root,
        "post-current",
        "daw-current",
    );
    let ids: Vec<&str> = v.iter().map(|c| c.instance_id.as_str()).collect();
    assert_eq!(
        ids,
        vec!["iid-current-mix"],
        "foreign non-empty DAW sessions must not appear in the selectable PRE dropdown"
    );
}

#[test]
fn enumerate_active_pre_pair_candidates_for_post_project_in_session_merges_exact_daw_and_single_shelf_host_fallback(
) {
    let root = isolated_dir();
    let now = now_rfc3339();
    let current_host = std::process::id();
    write_pre_for_select_with_identity(
        &root,
        "pre-2mix",
        "iid-2mix",
        "2Mix",
        "inactive",
        &now,
        Some(current_host),
        Some("daw-current"),
    );
    write_pre_for_select_with_identity(
        &root,
        "pre-drum",
        "iid-drum",
        "Drum",
        "inactive",
        &now,
        Some(current_host),
        Some("daw-instance-drum"),
    );
    write_post_for_scope_with_identity(
        &root,
        "post-current",
        "post-2mix",
        "2Mix",
        Some(current_host),
        Some("daw-current"),
    );

    let names = enumerate_active_pre_pair_candidates_for_post_project_in_session(
        &root,
        "post-current",
        "daw-current",
    )
    .into_iter()
    .filter_map(|c| c.name)
    .collect::<std::collections::BTreeSet<_>>();
    assert_eq!(
        names,
        ["2Mix", "Drum"].into_iter().map(str::to_string).collect()
    );
}

#[test]
fn enumerate_active_pre_pair_candidates_for_post_project_in_session_allows_host_fallback_when_daw_ids_are_instance_scoped(
) {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select_with_identity(
        &root,
        "post-current",
        "iid-old-mix",
        "Mix",
        "inactive",
        &now,
        Some(std::process::id()),
        Some("daw-old"),
    );

    let v = enumerate_active_pre_pair_candidates_for_post_project_in_session(
        &root,
        "post-current",
        "daw-current",
    );
    assert_eq!(
        v.iter().map(|c| c.instance_id.as_str()).collect::<Vec<_>>(),
        vec!["iid-old-mix"],
        "single POST-project host fallback keeps Studio One instance-scoped daw IDs pairable"
    );
}

#[test]
fn select_target_pre_for_arm_for_post_project_avoids_other_session_same_name() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select_with_identity(
        &root,
        "pre-old",
        "iid-old-vocal",
        "Vocal",
        "inactive",
        &now,
        None,
        Some("daw-old"),
    );
    write_pre_for_select_with_identity(
        &root,
        "pre-current",
        "iid-current-vocal",
        "Vocal",
        "inactive",
        &now,
        None,
        Some("daw-current"),
    );
    write_pre_for_select_with_identity(
        &root,
        "pre-current",
        "iid-current-bass",
        "Bass",
        "inactive",
        &now,
        None,
        Some("daw-current"),
    );
    write_post_for_scope(&root, "post-current", "post-vocal", "Vocal");
    write_post_for_scope(&root, "post-current", "post-bass", "Bass");

    assert!(select_target_pre_for_arm(&root, "Vocal").is_none());
    let sel = select_target_pre_for_arm_for_post_project_in_session(
        &root,
        "Vocal",
        "post-current",
        "daw-current",
    )
    .expect("scoped Arm should choose the current session Vocal");
    assert_eq!(sel.instance_id, "iid-current-vocal");
}

#[test]
fn select_target_pre_for_arm_for_post_project_allows_single_host_project_daw_mismatch() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select_with_identity(
        &root,
        "post-current",
        "iid-old-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(std::process::id()),
        Some("daw-old"),
    );

    let sel = select_target_pre_for_arm_for_post_project_in_session(
        &root,
        "Vocal",
        "post-current",
        "daw-current",
    )
    .expect("single POST-project host fallback should arm instance-scoped daw IDs");
    assert_eq!(sel.instance_id, "iid-old-vocal");
}

#[test]
fn select_target_pre_for_arm_for_post_project_prefers_matching_daw_when_names_are_identical() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select_with_identity(
        &root,
        "pre-old",
        "iid-old-vocal",
        "Vocal",
        "inactive",
        &now,
        None,
        Some("daw-old"),
    );
    write_pre_for_select_with_identity(
        &root,
        "pre-current",
        "iid-current-vocal",
        "Vocal",
        "inactive",
        &now,
        None,
        Some("daw-current"),
    );
    write_post_for_scope(&root, "post-current", "post-vocal", "Vocal");

    assert!(select_target_pre_for_arm(&root, "Vocal").is_none());
    let sel = select_target_pre_for_arm_for_post_project_in_session(
        &root,
        "Vocal",
        "post-current",
        "daw-current",
    )
    .expect("matching DAW session should win when an older open document has the same PRE name");
    assert_eq!(sel.instance_id, "iid-current-vocal");
}

#[test]
fn select_target_pre_for_post_project_prefers_matching_daw_when_names_are_identical() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select_with_identity(
        &root,
        "pre-old",
        "iid-old-vocal",
        "Vocal",
        "active",
        &now,
        None,
        Some("daw-old"),
    );
    write_pre_for_select_with_identity(
        &root,
        "pre-current",
        "iid-current-vocal",
        "Vocal",
        "active",
        &now,
        None,
        Some("daw-current"),
    );
    write_post_for_scope(&root, "post-current", "post-vocal", "Vocal");

    assert!(select_target_pre_for_post_project(&root, "Vocal", "post-current").is_none());
    let sel = select_target_pre_for_post_project_in_session(
        &root,
        "Vocal",
        "post-current",
        "daw-current",
    )
    .expect("display selection should prefer the matching DAW session");
    assert_eq!(sel.instance_id, "iid-current-vocal");
}

#[test]
fn select_target_pre_for_arm_for_post_project_prefers_current_host_process_same_name() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select_with_host_process_id(
        &root,
        "pre-other-host",
        "iid-other-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(other_host_process_id()),
    );
    write_pre_for_select_with_host_process_id(
        &root,
        "pre-current-host",
        "iid-current-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(std::process::id()),
    );

    assert!(select_target_pre_for_arm(&root, "Vocal").is_none());
    let sel = select_target_pre_for_arm_for_post_project(&root, "Vocal", "post-current")
        .expect("same-host PRE should win over another host process with the same name");
    assert_eq!(sel.instance_id, "iid-current-vocal");
}

#[test]
fn select_target_pre_for_arm_for_post_project_rejects_foreign_unique_pre_when_other_post_project_is_active(
) {
    let root = isolated_dir();
    let now = now_rfc3339();
    let current_host = std::process::id();
    write_pre_for_select_with_host_process_id(
        &root,
        "pre-mastering-project",
        "iid-master-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(current_host),
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-current-song",
        "post-current",
        "",
        Some(current_host),
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-mastering-project",
        "post-master",
        "Vocal",
        Some(current_host),
    );

    assert!(
        select_target_pre_for_arm_for_post_project(&root, "Vocal", "post-current-song").is_none(),
        "a globally unique PRE in another active Studio One project must not be auto-armed"
    );
}

#[test]
fn select_target_pre_for_post_project_rejects_active_foreign_unique_pre_when_other_post_project_is_active(
) {
    let root = isolated_dir();
    let now = now_rfc3339();
    let current_host = std::process::id();
    write_pre_for_select_with_host_process_id(
        &root,
        "pre-mastering-project",
        "iid-master-vocal",
        "Vocal",
        "active",
        &now,
        Some(current_host),
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-current-song",
        "post-current",
        "",
        Some(current_host),
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-mastering-project",
        "post-master",
        "Vocal",
        Some(current_host),
    );

    assert!(
        select_target_pre_for_post_project(&root, "Vocal", "post-current-song").is_none(),
        "display selection must not latch a globally unique active PRE from another open project"
    );
}

#[test]
fn select_target_pre_for_arm_for_post_project_allows_unique_same_host_pre_without_daw_when_single_post_project(
) {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select_with_identity(
        &root,
        "pre-foreign-project",
        "iid-foreign-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(std::process::id()),
        None,
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-current-song",
        "post-current",
        "",
        Some(std::process::id()),
    );

    let sel = select_target_pre_for_arm_for_post_project_in_session(
        &root,
        "Vocal",
        "post-current-song",
        "daw-current",
    )
    .expect("single POST-project host fallback should bridge legacy PREs");
    assert_eq!(sel.instance_id, "iid-foreign-vocal");
}

#[test]
fn select_target_pre_for_arm_for_post_project_rejects_daw_mismatch_when_other_post_project_is_active(
) {
    let root = isolated_dir();
    let now = now_rfc3339();
    let current_host = std::process::id();
    write_pre_for_select_with_identity(
        &root,
        "pre-foreign-project",
        "iid-foreign-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(current_host),
        Some("daw-foreign"),
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-current-song",
        "post-current",
        "",
        Some(current_host),
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-mastering-project",
        "post-master",
        "Vocal",
        Some(current_host),
    );

    assert!(
        select_target_pre_for_arm_for_post_project_in_session(
            &root,
            "Vocal",
            "post-current-song",
            "daw-current",
        )
        .is_none(),
        "host fallback must close when another Studio One project has an active POST shelf"
    );
}

#[test]
fn select_target_pre_for_arm_for_post_project_rejects_external_exact_daw_when_other_post_project_outside_daw_is_active(
) {
    let root = isolated_dir();
    let now = now_rfc3339();
    let current_host = std::process::id();
    write_pre_for_select_with_identity(
        &root,
        "pre-foreign-project",
        "iid-foreign-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(current_host),
        Some("daw-current"),
    );
    write_post_for_scope_with_identity(
        &root,
        "post-current-song",
        "post-current",
        "",
        Some(current_host),
        Some("daw-current"),
    );
    write_post_for_scope_with_identity(
        &root,
        "post-mastering-project",
        "post-master",
        "Vocal",
        Some(current_host),
        Some("daw-other"),
    );

    assert!(
        select_target_pre_for_arm_for_post_project_in_session(
            &root,
            "Vocal",
            "post-current-song",
            "daw-current",
        )
        .is_none(),
        "an exact DAW id is not enough when another same-host POST shelf has a different DAW scope"
    );
}

#[test]
fn select_target_pre_for_arm_for_post_project_allows_external_exact_daw_when_other_post_project_shares_daw(
) {
    let root = isolated_dir();
    let now = now_rfc3339();
    let current_host = std::process::id();
    write_pre_for_select_with_identity(
        &root,
        "pre-vst3-project",
        "iid-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(current_host),
        Some("daw-shared"),
    );
    write_post_for_scope_with_identity(
        &root,
        "post-au-project",
        "post-au",
        "",
        Some(current_host),
        Some("daw-shared"),
    );
    write_post_for_scope_with_identity(
        &root,
        "post-vst3-project",
        "post-vst3",
        "Vocal",
        Some(current_host),
        Some("daw-shared"),
    );

    let sel = select_target_pre_for_arm_for_post_project_in_session(
        &root,
        "Vocal",
        "post-au-project",
        "daw-shared",
    )
    .expect("matching DAW scope can bridge split AU/VST3 POST shelves when all shelves agree");
    assert_eq!(sel.instance_id, "iid-vocal");
}

#[test]
fn enumerate_active_pre_pair_candidates_for_post_project_in_session_matches_current_live_shape() {
    let root = isolated_dir();
    let now = now_rfc3339();
    let current_host = std::process::id();
    write_pre_for_select_with_identity(
        &root,
        "pre-drum-project",
        "iid-drum",
        "Drum",
        "inactive",
        &now,
        Some(current_host),
        Some("daw-pre-drum"),
    );
    write_pre_for_select_with_identity(
        &root,
        "pre-music-project",
        "iid-music",
        "Music",
        "inactive",
        &now,
        Some(current_host),
        Some("daw-pre-music"),
    );
    write_pre_for_select_with_identity(
        &root,
        "pre-2mix-project",
        "iid-2mix",
        "2Mix",
        "inactive",
        &now,
        Some(current_host),
        Some("daw-pre-2mix"),
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-shared-project",
        "post-drum",
        "Drum",
        Some(current_host),
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-shared-project",
        "post-music",
        "Music",
        Some(current_host),
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-shared-project",
        "post-2mix",
        "2Mix",
        Some(current_host),
    );

    let names = enumerate_active_pre_pair_candidates_for_post_project_in_session(
        &root,
        "post-shared-project",
        "daw-post-2mix",
    )
    .into_iter()
    .filter_map(|c| c.name)
    .collect::<std::collections::BTreeSet<_>>();
    assert_eq!(
        names,
        ["2Mix", "Drum", "Music"]
            .into_iter()
            .map(str::to_string)
            .collect()
    );
}

#[test]
fn explicit_pair_choice_survives_stale_measurement_and_instance_scoped_ids() {
    let root = isolated_dir();
    let current_host = std::process::id();
    write_pre_for_select_with_identity(
        &root,
        "pre-au-project",
        "pre-2mix",
        "2Mix",
        "inactive",
        &old_rfc3339(120),
        Some(current_host),
        Some("daw-pre-instance"),
    );
    let _lease = attach_live_owner(&root, "pre-au-project", "pre-2mix");

    let choices = enumerate_live_pre_pair_choices_for_post_project_in_session(
        &root,
        "post-au-project",
        "daw-post-instance",
    );
    assert_eq!(choices.len(), 1);
    assert_eq!(choices[0].instance_id, "pre-2mix");

    let (selected, name) = select_live_pre_pair_choice_by_instance_for_post_project_in_session(
        &root,
        "pre-2mix",
        "post-au-project",
        "daw-post-instance",
    )
    .expect("live same-host PRE remains explicitly selectable");
    assert_eq!(selected.instance_id, "pre-2mix");
    assert_eq!(name, "2Mix");

    write_post_for_scope_with_host_process_id(
        &root,
        "post-other-project",
        "post-other",
        "Other",
        Some(current_host),
    );
    let latched = Mutex::new(Some(latch_selected_pre(name, selected)));
    let resolved = resolve_arm_target_for_post_project_in_session(
        &root,
        "2Mix",
        "post-au-project",
        "daw-post-instance",
        &latched,
    )
    .expect("exact same-host latch remains authoritative after explicit selection");
    assert_eq!(resolved.instance_id, "pre-2mix");
}

#[test]
fn explicit_pair_choice_selects_one_exact_instance_among_duplicate_names() {
    let root = isolated_dir();
    let current_host = std::process::id();
    for (project, instance) in [("pre-a-project", "pre-a"), ("pre-b-project", "pre-b")] {
        write_pre_for_select_with_identity(
            &root,
            project,
            instance,
            "2Mix",
            "inactive",
            &old_rfc3339(120),
            Some(current_host),
            None,
        );
    }
    let _lease_a = attach_live_owner(&root, "pre-a-project", "pre-a");
    let _lease_b = attach_live_owner(&root, "pre-b-project", "pre-b");

    let (selected, name) = select_live_pre_pair_choice_by_instance_for_post_project_in_session(
        &root,
        "pre-b",
        "post-project",
        "daw-post-instance",
    )
    .expect("instance-id click must not be ambiguous by display name");
    assert_eq!(selected.instance_id, "pre-b");
    assert_eq!(name, "2Mix");
}

#[test]
fn explicit_pair_choice_keeps_bypassed_runtime_visible() {
    let root = isolated_dir();
    write_pre_for_select_with_identity(
        &root,
        "pre-project",
        "pre-bypassed",
        "2Mix",
        "bypassed",
        &old_rfc3339(120),
        Some(std::process::id()),
        None,
    );
    let _lease = attach_live_owner(&root, "pre-project", "pre-bypassed");

    let choices = enumerate_live_pre_pair_choices_for_post_project_in_session(
        &root,
        "post-project",
        "daw-post",
    );

    assert_eq!(choices.len(), 1);
    assert_eq!(choices[0].instance_id, "pre-bypassed");
}

#[test]
fn published_exact_unnamed_claim_is_all_keep_ready_while_runtime_is_live() {
    let root = isolated_dir();
    write_pre_for_select_with_identity(
        &root,
        "pre-project",
        "pre-unnamed",
        "",
        "inactive",
        &old_rfc3339(120),
        Some(std::process::id()),
        None,
    );
    let _lease = attach_live_owner(&root, "pre-project", "pre-unnamed");

    let selected = resolve_published_pair_claim_for_arm(
        &root,
        None,
        Some("pre-unnamed"),
        "post-project",
        "daw-post",
    )
    .expect("exact live claim must not depend on a name or measurement timestamp");

    assert_eq!(selected.instance_id, "pre-unnamed");
}

#[test]
fn published_exact_claim_reconnects_to_one_recreated_same_host_pre_only() {
    let root = isolated_dir();
    let current_host = std::process::id();
    write_pre_for_select_with_identity(
        &root,
        "pre-vst-project",
        "pre-drum-recreated",
        "Drum",
        "inactive",
        &old_rfc3339(120),
        Some(current_host),
        Some("daw-vst-instance"),
    );
    let _lease = attach_live_owner(&root, "pre-vst-project", "pre-drum-recreated");

    let selected = resolve_published_pair_claim_for_arm(
        &root,
        Some("Drum"),
        Some("pre-drum-deleted"),
        "post-au-project",
        "daw-au-instance",
    )
    .expect("one same-host replacement preserves the prior explicit pair intent");
    assert_eq!(selected.instance_id, "pre-drum-recreated");

    write_pre_for_select_with_identity(
        &root,
        "pre-other-host-project",
        "pre-drum-deleted",
        "Drum",
        "inactive",
        &old_rfc3339(120),
        Some(other_host_process_id()),
        Some("daw-other-host"),
    );
    let exact_elsewhere_lease =
        attach_live_owner(&root, "pre-other-host-project", "pre-drum-deleted");
    assert!(resolve_published_pair_claim_for_arm(
        &root,
        Some("Drum"),
        Some("pre-drum-deleted"),
        "post-au-project",
        "daw-au-instance",
    )
    .is_none());
    drop(exact_elsewhere_lease);

    write_pre_for_select_with_identity(
        &root,
        "pre-vst-project-b",
        "pre-drum-duplicate",
        "Drum",
        "inactive",
        &old_rfc3339(120),
        Some(current_host),
        Some("daw-vst-instance-b"),
    );
    let _duplicate_lease = attach_live_owner(&root, "pre-vst-project-b", "pre-drum-duplicate");
    assert!(resolve_published_pair_claim_for_arm(
        &root,
        Some("Drum"),
        Some("pre-drum-deleted"),
        "post-au-project",
        "daw-au-instance",
    )
    .is_none());
}

#[test]
fn select_target_pre_for_arm_for_post_project_allows_current_project_pre_when_other_post_project_is_active(
) {
    let root = isolated_dir();
    let now = now_rfc3339();
    let current_host = std::process::id();
    write_pre_for_select_with_host_process_id(
        &root,
        "post-current-song",
        "iid-current-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(current_host),
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-current-song",
        "post-current",
        "",
        Some(current_host),
    );
    write_post_for_scope_with_host_process_id(
        &root,
        "post-mastering-project",
        "post-master",
        "Vocal",
        Some(current_host),
    );

    let sel = select_target_pre_for_arm_for_post_project(&root, "Vocal", "post-current-song")
        .expect("current-project PRE must remain armable");
    assert_eq!(sel.instance_id, "iid-current-vocal");
}

#[test]
fn resolve_arm_target_for_post_project_rejects_foreign_latch_without_same_daw_identity() {
    let root = isolated_dir();
    let latched = Mutex::new(Some(LatchedPre {
        name: "snare".to_string(),
        instance_id: "iid-A".to_string(),
        project_dir: root.join("old-pre-project"),
        pre_json: root.join("old-pre-project").join("iid-A").join("pre.json"),
        daw_session_id: None,
        host_process_id: None,
    }));

    assert!(
        resolve_arm_target_for_post_project_in_session(
            &root,
            "snare",
            "post-current",
            "daw-current",
            &latched,
        )
        .is_none(),
        "project switch must not keep a foreign latch when no matching daw_session_id exists"
    );
}

#[test]
fn resolve_arm_target_for_post_project_rejects_same_project_latch_with_foreign_daw_identity() {
    let root = isolated_dir();
    let latched = Mutex::new(Some(LatchedPre {
        name: "snare".to_string(),
        instance_id: "iid-A".to_string(),
        project_dir: root.join("post-current"),
        pre_json: root.join("post-current").join("iid-A").join("pre.json"),
        daw_session_id: Some("daw-old".to_string()),
        host_process_id: None,
    }));

    assert!(
        resolve_arm_target_for_post_project_in_session(
            &root,
            "snare",
            "post-current",
            "daw-current",
            &latched,
        )
        .is_none(),
        "same project_hash must not keep a latched PRE from a different explicit daw_session_id"
    );
}

#[test]
fn select_target_pre_for_arm_for_post_project_tie_remains_ambiguous() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select(&root, "pre-a", "iid-a-vocal", "Vocal", "inactive", &now);
    write_pre_for_select(&root, "pre-a", "iid-a-bass", "Bass", "inactive", &now);
    write_pre_for_select(&root, "pre-b", "iid-b-vocal", "Vocal", "inactive", &now);
    write_pre_for_select(&root, "pre-b", "iid-b-bass", "Bass", "inactive", &now);
    write_post_for_scope(&root, "post-current", "post-vocal", "Vocal");
    write_post_for_scope(&root, "post-current", "post-bass", "Bass");

    assert!(select_target_pre_for_arm_for_post_project(&root, "Vocal", "post-current").is_none());
}

#[test]
fn select_target_pre_for_arm_for_post_project_same_host_tie_remains_ambiguous() {
    let root = isolated_dir();
    let now = now_rfc3339();
    let current_host = std::process::id();
    write_pre_for_select_with_host_process_id(
        &root,
        "pre-current-a",
        "iid-current-a-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(current_host),
    );
    write_pre_for_select_with_host_process_id(
        &root,
        "pre-current-b",
        "iid-current-b-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(current_host),
    );
    write_pre_for_select_with_host_process_id(
        &root,
        "pre-other-host",
        "iid-other-vocal",
        "Vocal",
        "inactive",
        &now,
        Some(other_host_process_id()),
    );

    assert!(select_target_pre_for_arm_for_post_project(&root, "Vocal", "post-current").is_none());
}

#[test]
fn discover_pre_dirs_for_post_project_does_not_fallback_when_names_disagree() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select(&root, "pre-old", "iid-old-snare", "Snare", "inactive", &now);
    write_post_for_scope(&root, "post-current", "post-vocal", "Vocal");

    assert!(discover_pre_dirs_for_post_project(&root, "post-current").is_empty());
}

#[test]
fn select_target_pre_for_arm_inactive_fresh_returns_some() {
    let root = isolated_dir();
    write_pre_for_select(
        &root,
        "puid-1",
        "iid-A",
        "snare",
        "inactive",
        &now_rfc3339(),
    );
    assert!(select_target_pre(&root, "snare").is_none());
    let sel = select_target_pre_for_arm(&root, "snare").expect("Arm allows inactive fresh PRE");
    assert_eq!(sel.instance_id, "iid-A");
}

#[test]
fn read_pre_at_fresh_active() {
    let root = isolated_dir();
    write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now_rfc3339());
    let path = root.join("puid-1").join("iid-A").join("pre.json");
    let state = read_pre_at(&path).expect("fresh active -> Some");
    assert_eq!(state.name.as_deref(), Some("snare"));
    assert!(state.active && state.fresh);
}

#[test]
fn read_pre_at_idle_stale_gone() {
    let root = isolated_dir();
    let path = root.join("puid-1").join("iid-A").join("pre.json");
    write_pre_for_select(
        &root,
        "puid-1",
        "iid-A",
        "snare",
        "inactive",
        &now_rfc3339(),
    );
    let state = read_pre_at(&path).unwrap();
    assert!(!state.active && state.fresh);

    write_pre_for_select(
        &root,
        "puid-1",
        "iid-A",
        "snare",
        "active",
        &old_rfc3339(20),
    );
    assert!(!read_pre_at(&path).unwrap().fresh);

    fs::remove_file(&path).unwrap();
    assert!(read_pre_at(&path).is_none());
}

#[test]
fn resolve_arm_target_uses_latch_over_ambiguous() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now);
    let pre_json = root.join("puid-1").join("iid-A").join("pre.json");
    let latched = Mutex::new(Some(LatchedPre {
        name: "snare".to_string(),
        instance_id: "iid-A".to_string(),
        project_dir: root.join("puid-1"),
        pre_json,
        daw_session_id: None,
        host_process_id: None,
    }));
    write_pre_for_select(&root, "puid-2", "iid-B", "snare", "active", &now);

    assert!(select_target_pre_for_arm(&root, "snare").is_none());
    let sel = resolve_arm_target(&root, "snare", &latched).expect("latched target wins");
    assert_eq!(sel.instance_id, "iid-A");
}

#[test]
fn resolve_arm_target_uses_latch_even_when_pre_json_is_stale() {
    let root = isolated_dir();
    let old = old_rfc3339(20);
    write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &old);
    let pre_json = root.join("puid-1").join("iid-A").join("pre.json");
    let latched = Mutex::new(Some(LatchedPre {
        name: "snare".to_string(),
        instance_id: "iid-A".to_string(),
        project_dir: root.join("puid-1"),
        pre_json,
        daw_session_id: None,
        host_process_id: None,
    }));

    assert!(select_target_pre_for_arm(&root, "snare").is_none());
    let sel = resolve_arm_target(&root, "snare", &latched).expect("latched target wins");
    assert_eq!(sel.instance_id, "iid-A");
}

#[test]
fn resolve_arm_target_uses_latch_even_when_pre_json_is_missing() {
    let root = isolated_dir();
    let latched = Mutex::new(Some(LatchedPre {
        name: "snare".to_string(),
        instance_id: "iid-A".to_string(),
        project_dir: root.join("puid-1"),
        pre_json: root.join("puid-1").join("iid-A").join("pre.json"),
        daw_session_id: None,
        host_process_id: None,
    }));

    let sel = resolve_arm_target(&root, "snare", &latched).expect("latched target wins");
    assert_eq!(sel.instance_id, "iid-A");
}

#[test]
fn resolve_arm_target_for_post_project_uses_latch_even_when_project_dir_not_discovered() {
    let root = isolated_dir();
    let latched = Mutex::new(Some(LatchedPre {
        name: "snare".to_string(),
        instance_id: "iid-A".to_string(),
        project_dir: root.join("old-pre-project"),
        pre_json: root.join("old-pre-project").join("iid-A").join("pre.json"),
        daw_session_id: Some("daw-current".to_string()),
        host_process_id: None,
    }));

    let sel = resolve_arm_target_for_post_project_in_session(
        &root,
        "snare",
        "post-current",
        "daw-current",
        &latched,
    )
    .expect("same-daw foreign latch wins");
    assert_eq!(sel.instance_id, "iid-A");
}

#[test]
fn resolve_arm_target_falls_back_when_unlatched() {
    let root = isolated_dir();
    write_pre_for_select(&root, "puid-1", "iid-A", "snare", "active", &now_rfc3339());
    let latched = Mutex::new(None);
    let sel = resolve_arm_target(&root, "snare", &latched).expect("unlatched fallback");
    assert_eq!(sel.instance_id, "iid-A");
}

#[test]
fn select_target_pre_for_arm_bypassed_excluded() {
    let root = isolated_dir();
    write_pre_for_select(
        &root,
        "puid-1",
        "iid-A",
        "snare",
        "bypassed",
        &now_rfc3339(),
    );
    assert!(select_target_pre_for_arm(&root, "snare").is_none());
}

#[test]
fn select_target_pre_for_arm_stale_t_excluded() {
    let root = isolated_dir();
    write_pre_for_select(
        &root,
        "puid-1",
        "iid-A",
        "snare",
        "inactive",
        &old_rfc3339(20),
    );
    assert!(select_target_pre_for_arm(&root, "snare").is_none());
}

#[test]
fn select_target_pre_for_arm_ambiguous_returns_none() {
    let root = isolated_dir();
    let now = now_rfc3339();
    write_pre_for_select(&root, "puid-1", "iid-A", "snare", "inactive", &now);
    write_pre_for_select(&root, "puid-2", "iid-B", "snare", "inactive", &now);
    assert!(select_target_pre_for_arm(&root, "snare").is_none());
}

#[test]
fn select_target_pre_for_arm_empty_name_returns_none() {
    let root = isolated_dir();
    write_pre_for_select(
        &root,
        "puid-1",
        "iid-A",
        "snare",
        "inactive",
        &now_rfc3339(),
    );
    assert!(select_target_pre_for_arm(&root, "").is_none());
}
