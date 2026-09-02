use super::*;
use crate::{
    active_post_project_uuids_for_broadcast_scope, active_post_project_uuids_for_daw_session,
    enumerate_active_post_pair_candidates_for_broadcast_scope,
    enumerate_active_post_pair_candidates_for_daw_session,
    host_scope_has_other_active_post_project,
};
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

fn write_post_json_with_daw_and_host(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    pair_pre_name: &str,
    daw_session_id: &str,
    host_process_id: u32,
) -> PathBuf {
    let post_file = write_post_json_with_daw(
        kirin_root,
        project_uuid,
        instance_id,
        pair_pre_name,
        daw_session_id,
    );
    let mut json: serde_json::Value =
        serde_json::from_slice(&fs::read(&post_file).unwrap()).unwrap();
    json["host_process_id"] = serde_json::json!(host_process_id);
    fs::write(&post_file, serde_json::to_vec(&json).unwrap()).unwrap();
    post_file
}

fn write_legacy_post_json_with_host(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    pair_pre_name: &str,
    host_process_id: u32,
) -> PathBuf {
    let post_file = write_post_json(
        kirin_root,
        project_uuid,
        instance_id,
        SignalState::Active,
        Some(SignalState::Active),
        pair_pre_name,
    );
    let mut json: serde_json::Value =
        serde_json::from_slice(&fs::read(&post_file).unwrap()).unwrap();
    json.as_object_mut().unwrap().remove("daw_session_id");
    json["host_process_id"] = serde_json::json!(host_process_id);
    fs::write(&post_file, serde_json::to_vec(&json).unwrap()).unwrap();
    post_file
}

fn write_post_json_with_daw(
    kirin_root: &Path,
    project_uuid: &str,
    instance_id: &str,
    pair_pre_name: &str,
    daw_session_id: &str,
) -> PathBuf {
    let dir = kirin_root.join(project_uuid).join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let post_file = dir.join("post.json");
    let json = serialize_post_json_with_daw(
        instance_id,
        SignalState::Active,
        Some(SignalState::Active),
        &MeasureResult::default(),
        pair_pre_name,
        0.0,
        daw_session_id,
    );
    fs::write(&post_file, json.as_bytes()).unwrap();
    post_file
}

/// enumerate_active_post_pair_candidates: 多 project_uuid dir flatten + 順序。
#[test]
fn enumerate_flattens_multiple_projects() {
    let root = unique_root("enum_flatten");
    // pj-AA: 2 candidates (post-a / post-b)
    let _ = write_post_json(
        &root,
        "pj-AA",
        "post-b",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-AA",
    );
    let _ = write_post_json(
        &root,
        "pj-AA",
        "post-a",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-AA",
    );
    // pj-BB: 1 candidate
    let _ = write_post_json(
        &root,
        "pj-BB",
        "post-c",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-BB",
    );

    let cands = enumerate_active_post_pair_candidates(&root);
    assert_eq!(cands.len(), 3);
    // 順序: pj-AA dir (file_name 辞書順) → 内 instance_id 辞書順 → pj-BB dir
    let order: Vec<(&str, &str)> = cands
        .iter()
        .map(|c| (c.project_uuid.as_str(), c.instance_id.as_str()))
        .collect();
    assert_eq!(
        order,
        vec![
            ("pj-AA", "post-a"),
            ("pj-AA", "post-b"),
            ("pj-BB", "post-c"),
        ]
    );
}

/// enumerate_active_post_pair_candidates: pair_pre_name の None / Some 混在。
#[test]
fn enumerate_preserves_pair_pre_name_option() {
    let root = unique_root("enum_pair");
    let _ = write_post_json(
        &root,
        "pj-X",
        "post-with-name",
        SignalState::Active,
        Some(SignalState::Active),
        "PRE-Hello",
    );
    let _ = write_post_json(
        &root,
        "pj-X",
        "post-no-name",
        SignalState::Active,
        Some(SignalState::Active),
        "",
    );
    let cands = enumerate_active_post_pair_candidates(&root);
    assert_eq!(cands.len(), 2);
    let with_name = cands
        .iter()
        .find(|c| c.instance_id == "post-with-name")
        .unwrap();
    let no_name = cands
        .iter()
        .find(|c| c.instance_id == "post-no-name")
        .unwrap();
    assert_eq!(with_name.pair_pre_name.as_deref(), Some("PRE-Hello"));
    assert!(no_name.pair_pre_name.is_none());
}

#[test]
fn enumerate_for_daw_session_spans_projects_and_filters_other_daw() {
    let root = unique_root("enum_daw");
    let _ = write_post_json_with_daw(&root, "pj-AU", "post-2mix", "2Mix", "daw-main");
    let _ = write_post_json_with_daw(&root, "pj-VST3", "post-drum", "Drum", "daw-main");
    let _ = write_post_json_with_daw(&root, "pj-VST3", "post-music", "Music", "daw-main");
    let _ = write_post_json_with_daw(&root, "pj-OTHER", "post-other", "Vocal", "daw-other");

    let cands = enumerate_active_post_pair_candidates_for_daw_session(&root, "daw-main");
    let names: Vec<_> = cands
        .iter()
        .filter_map(|c| c.pair_pre_name.as_deref())
        .collect();
    assert_eq!(names, vec!["2Mix", "Drum", "Music"]);

    let projects = active_post_project_uuids_for_daw_session(&root, "daw-main");
    assert_eq!(projects, vec!["pj-AU".to_string(), "pj-VST3".to_string()]);
}

#[test]
fn enumerate_for_broadcast_scope_does_not_span_distinct_nonempty_daw_same_process() {
    let root = unique_root("enum_broadcast_scope");
    let host_pid = 42_4242;
    let other_pid = 77_7777;
    let _ =
        write_post_json_with_daw_and_host(&root, "pj-AU", "post-2mix", "2Mix", "daw-au", host_pid);
    let _ = write_post_json_with_daw_and_host(
        &root,
        "pj-VST3",
        "post-drum",
        "Drum",
        "daw-vst3",
        host_pid,
    );
    let _ = write_post_json_with_daw_and_host(
        &root,
        "pj-OTHER",
        "post-other",
        "Vocal",
        "daw-other",
        other_pid,
    );

    let cands =
        enumerate_active_post_pair_candidates_for_broadcast_scope(&root, "daw-au", host_pid);
    let names: Vec<_> = cands
        .iter()
        .filter_map(|c| c.pair_pre_name.as_deref())
        .collect();
    assert_eq!(names, vec!["2Mix"]);

    let projects = active_post_project_uuids_for_broadcast_scope(&root, "daw-au", host_pid);
    assert_eq!(projects, vec!["pj-AU".to_string()]);

    let daw_only = enumerate_active_post_pair_candidates_for_daw_session(&root, "daw-au");
    assert_eq!(daw_only.len(), 1);
    assert_eq!(daw_only[0].pair_pre_name.as_deref(), Some("2Mix"));
}

#[test]
fn enumerate_for_broadcast_scope_keeps_single_post_project_when_daw_ids_are_instance_scoped() {
    let root = unique_root("enum_broadcast_scope_single_project_instance_daw");
    let host_pid = 42_4242;
    let _ = write_post_json_with_daw_and_host(
        &root,
        "pj-current",
        "post-2mix",
        "2Mix",
        "daw-post-2mix",
        host_pid,
    );
    let _ = write_post_json_with_daw_and_host(
        &root,
        "pj-current",
        "post-drum",
        "Drum",
        "daw-post-drum",
        host_pid,
    );
    let _ = write_post_json_with_daw_and_host(
        &root,
        "pj-current",
        "post-music",
        "Music",
        "daw-post-music",
        host_pid,
    );

    let cands =
        enumerate_active_post_pair_candidates_for_broadcast_scope(&root, "daw-post-2mix", host_pid);
    let names: Vec<_> = cands
        .iter()
        .filter_map(|c| c.pair_pre_name.as_deref())
        .collect();
    assert_eq!(names, vec!["2Mix", "Drum", "Music"]);

    let projects = active_post_project_uuids_for_broadcast_scope(&root, "daw-post-2mix", host_pid);
    assert_eq!(projects, vec!["pj-current".to_string()]);
}

#[test]
fn broadcast_receive_gate_bridges_instance_scoped_daw_inside_same_project_host() {
    assert!(broadcast_scope_or_same_project_host_matches(
        "daw-local",
        42,
        "daw-remote",
        42
    ));
    assert!(!broadcast_scope_or_same_project_host_matches(
        "daw-local",
        42,
        "daw-remote",
        77
    ));
}

#[test]
fn enumerate_for_broadcast_scope_rejects_legacy_no_daw_when_local_has_explicit_daw() {
    let root = unique_root("enum_broadcast_scope_legacy");
    let host_pid = 42_4242;
    let _ = write_post_json_with_daw_and_host(
        &root,
        "pj-current",
        "post-2mix",
        "2Mix",
        "daw-current",
        host_pid,
    );
    let _ = write_legacy_post_json_with_host(&root, "pj-legacy", "post-legacy", "Drum", host_pid);

    let cands =
        enumerate_active_post_pair_candidates_for_broadcast_scope(&root, "daw-current", host_pid);
    let names: Vec<_> = cands
        .iter()
        .filter_map(|c| c.pair_pre_name.as_deref())
        .collect();
    assert_eq!(
        names,
        vec!["2Mix"],
        "explicit local daw_session_id must not bridge to a legacy no-daw POST by host alone"
    );
}

#[test]
fn enumerate_for_broadcast_scope_keeps_same_host_legacy_when_both_daw_absent() {
    let root = unique_root("enum_broadcast_scope_legacy_empty");
    let host_pid = 42_4242;
    let _ = write_legacy_post_json_with_host(&root, "pj-legacy-a", "post-2mix", "2Mix", host_pid);
    let _ = write_legacy_post_json_with_host(&root, "pj-legacy-b", "post-drum", "Drum", host_pid);
    let _ = write_post_json_with_daw_and_host(
        &root,
        "pj-current",
        "post-current",
        "Music",
        "daw-current",
        host_pid,
    );

    let cands = enumerate_active_post_pair_candidates_for_broadcast_scope(&root, "", host_pid);
    let names: Vec<_> = cands
        .iter()
        .filter_map(|c| c.pair_pre_name.as_deref())
        .collect();
    assert_eq!(
        names,
        vec!["2Mix", "Drum"],
        "legacy host fallback is only valid when both sides lack explicit daw_session_id"
    );
}

#[test]
fn host_scope_has_other_active_post_project_detects_same_host_foreign_project() {
    let root = unique_root("host_scope_other_project");
    let host_pid = 42_4242;
    let other_pid = 77_7777;
    let unused_pid = 88_8888;
    let _ = write_post_json_with_daw_and_host(
        &root,
        "pj-current",
        "post-current",
        "2Mix",
        "daw-current",
        host_pid,
    );
    let _ = write_post_json_with_daw_and_host(
        &root,
        "pj-other-song",
        "post-other-song",
        "Drum",
        "daw-other",
        host_pid,
    );
    let _ = write_post_json_with_daw_and_host(
        &root,
        "pj-other-host",
        "post-other-host",
        "Vocal",
        "daw-foreign",
        other_pid,
    );

    assert!(host_scope_has_other_active_post_project(
        &root,
        "pj-current",
        host_pid
    ));
    assert!(!host_scope_has_other_active_post_project(
        &root,
        "pj-current",
        unused_pid
    ));
}
