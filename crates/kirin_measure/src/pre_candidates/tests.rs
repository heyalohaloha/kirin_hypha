use super::*;

use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};

fn isolated_dir() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_pre_candidates_test_{pid}_{n}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn write_pre_tmp(
    tmp_base: &Path,
    ph: &str,
    instance_id: &str,
    metrics: Option<(f64, f64, f64)>,
) -> PathBuf {
    let dir = tmp_base.join(ph).join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let path = dir.join("pre.json");
    let json = if let Some((l, t, c)) = metrics {
        format!(
            r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"active","t":"now","lufs_m":{l},"true_peak":{t},"crest":{c},"psr":8.0}}"#
        )
    } else {
        format!(
            r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"active","t":"now"}}"#
        )
    };
    fs::write(&path, json).unwrap();
    path
}

fn write_pre_tmp_with_state(tmp_base: &Path, ph: &str, instance_id: &str, state: &str) {
    let dir = tmp_base.join(ph).join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let json = format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"{state}","t":"now","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}}"#
    );
    fs::write(dir.join("pre.json"), json).unwrap();
}

fn write_pre_tmp_with_name(tmp_base: &Path, ph: &str, instance_id: &str, state: &str, name: &str) {
    let dir = tmp_base.join(ph).join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let json = format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"{state}","t":"now","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0,"name":"{name}"}}"#
    );
    fs::write(dir.join("pre.json"), json).unwrap();
}

fn write_pre_tmp_with_host_process_id(
    tmp_base: &Path,
    ph: &str,
    instance_id: &str,
    host_process_id: u32,
) {
    let dir = tmp_base.join(ph).join(instance_id);
    fs::create_dir_all(&dir).unwrap();
    let json = format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{instance_id}","signal_state":"active","host_process_id":{host_process_id},"t":"now","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0,"name":"Snare"}}"#
    );
    fs::write(dir.join("pre.json"), json).unwrap();
}

#[test]
fn scan_with_no_dir_returns_empty() {
    let base = isolated_dir();
    let v = scan_pre_candidates(&base, "ph");
    assert!(v.is_empty());
}

#[test]
fn scan_skips_record_signal_subdir() {
    let base = isolated_dir();
    fs::create_dir_all(base.join("ph").join(RECORD_SIGNAL_RESERVED_DIR)).unwrap();
    write_pre_tmp(&base, "ph", "good-pre", Some((-14.0, -1.0, 12.0)));
    let v = scan_pre_candidates(&base, "ph");
    assert_eq!(v.len(), 1, "record_signal/ must not be treated as instance");
    assert_eq!(v[0].instance_id, "good-pre");
}

#[test]
fn scan_skips_corrupt_pre_json() {
    let base = isolated_dir();
    let dir = base.join("ph").join("bad-pre");
    fs::create_dir_all(&dir).unwrap();
    fs::write(dir.join("pre.json"), b"{ not valid").unwrap();
    write_pre_tmp(&base, "ph", "ok-pre", Some((-14.0, -1.0, 12.0)));
    let v = scan_pre_candidates(&base, "ph");
    assert_eq!(v.len(), 1);
    assert_eq!(v[0].instance_id, "ok-pre");
}

#[test]
fn scan_pre_candidates_in_works_across_project_uuids() {
    let base = isolated_dir();
    write_pre_tmp(&base, "pre-proj", "pre-A", Some((-14.0, -1.0, 12.0)));

    let old_path_result = scan_pre_candidates(&base, "post-proj");
    assert!(
        old_path_result.is_empty(),
        "scan_pre_candidates(post_ph) must return empty when PRE is under different project_uuid"
    );

    let pre_project_dir = base.join("pre-proj");
    let new_path_result = scan_pre_candidates_in(&pre_project_dir);
    assert_eq!(
        new_path_result.len(),
        1,
        "scan_pre_candidates_in(pre_project_dir) must find PRE across project_uuid boundaries"
    );
    assert_eq!(new_path_result[0].instance_id, "pre-A");
    assert_eq!(new_path_result[0].lufs_m, Some(-14.0));
}

#[test]
fn pick_zero_returns_none() {
    let v: Vec<PreCandidate> = Vec::new();
    let r = pick_closest_pre(
        &v,
        PostMetrics {
            lufs_m: Some(-14.0),
            true_peak: Some(-1.0),
            crest: Some(12.0),
        },
    );
    assert!(r.is_none());
}

#[test]
fn pick_single_auto_selects() {
    let base = isolated_dir();
    write_pre_tmp(&base, "ph", "solo", Some((-14.0, -1.0, 12.0)));
    let v = scan_pre_candidates(&base, "ph");
    let r = pick_closest_pre(
        &v,
        PostMetrics {
            lufs_m: Some(-14.0),
            true_peak: Some(-1.0),
            crest: Some(12.0),
        },
    );
    assert_eq!(r.unwrap().instance_id, "solo");
}

#[test]
fn pick_single_with_no_metrics_still_auto_selects() {
    let base = isolated_dir();
    write_pre_tmp(&base, "ph", "silent", None);
    let v = scan_pre_candidates(&base, "ph");
    let r = pick_closest_pre(
        &v,
        PostMetrics {
            lufs_m: None,
            true_peak: None,
            crest: None,
        },
    );
    assert_eq!(r.unwrap().instance_id, "silent");
}

#[test]
fn pick_multi_picks_minimum_distance() {
    let base = isolated_dir();
    write_pre_tmp(&base, "ph", "a", Some((-20.0, -5.0, 15.0)));
    write_pre_tmp(&base, "ph", "b", Some((-13.0, -1.5, 12.0)));
    write_pre_tmp(&base, "ph", "c", Some((-14.0, -2.0, 14.0)));
    let v = scan_pre_candidates(&base, "ph");
    let r = pick_closest_pre(
        &v,
        PostMetrics {
            lufs_m: Some(-14.0),
            true_peak: Some(-1.0),
            crest: Some(12.0),
        },
    );
    assert_eq!(r.unwrap().instance_id, "b");
}

#[test]
fn scan_pre_candidates_in_keeps_active() {
    let base = isolated_dir();
    write_pre_tmp_with_state(&base, "ph", "active-a", "active");
    write_pre_tmp_with_state(&base, "ph", "active-b", "active");
    let v = scan_pre_candidates(&base, "ph");
    assert_eq!(v.len(), 2, "Active PRE instances should be returned");
    let ids: Vec<&str> = v.iter().map(|c| c.instance_id.as_str()).collect();
    assert!(ids.contains(&"active-a"));
    assert!(ids.contains(&"active-b"));
}

#[test]
fn scan_pre_candidates_in_filters_only_bypassed() {
    let base = isolated_dir();
    write_pre_tmp_with_state(&base, "ph", "alive", "active");
    write_pre_tmp_with_state(&base, "ph", "off-bypass", "bypassed");
    write_pre_tmp_with_state(&base, "ph", "off-inactive", "inactive");
    let v = scan_pre_candidates(&base, "ph");
    assert_eq!(v.len(), 2, "Only Bypassed PRE instances are filtered out");
    let ids: Vec<&str> = v.iter().map(|c| c.instance_id.as_str()).collect();
    assert!(ids.contains(&"alive"));
    assert!(ids.contains(&"off-inactive"));
}

#[test]
fn scan_pre_candidates_in_keeps_legacy_no_signal_state() {
    let base = isolated_dir();
    let dir = base.join("ph").join("legacy");
    fs::create_dir_all(&dir).unwrap();
    let legacy_json = r#"{"v":2,"role":"PRE","instance_id":"legacy","t":"now","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}"#;
    fs::write(dir.join("pre.json"), legacy_json).unwrap();
    write_pre_tmp_with_state(&base, "ph", "new-active", "active");

    let v = scan_pre_candidates(&base, "ph");
    assert_eq!(v.len(), 2, "Legacy schema without signal_state is kept");
}

#[test]
fn pre_candidate_includes_name_field() {
    let base = isolated_dir();
    write_pre_tmp_with_name(&base, "ph", "iid-1", "active", "Snare");
    let v = scan_pre_candidates(&base, "ph");
    assert_eq!(v.len(), 1);
    assert_eq!(v[0].name.as_deref(), Some("Snare"));
}

#[test]
fn pre_candidate_name_legacy_schema_is_none() {
    let base = isolated_dir();
    write_pre_tmp_with_state(&base, "ph", "legacy-no-name", "active");
    let v = scan_pre_candidates(&base, "ph");
    assert_eq!(v.len(), 1);
    assert!(v[0].name.is_none(), "missing name field is None");
    assert!(
        v[0].host_process_id.is_none(),
        "missing host_process_id field is None"
    );
}

#[test]
fn pre_candidate_includes_host_process_id_field() {
    let base = isolated_dir();
    write_pre_tmp_with_host_process_id(&base, "ph", "iid-1", 42);
    let v = scan_pre_candidates(&base, "ph");
    assert_eq!(v.len(), 1);
    assert_eq!(v[0].host_process_id, Some(42));
}

#[test]
fn filter_candidates_by_name_keeps_match() {
    let base = isolated_dir();
    write_pre_tmp_with_name(&base, "ph", "iid-a", "active", "Snare");
    write_pre_tmp_with_name(&base, "ph", "iid-b", "active", "Kick");
    let v = scan_pre_candidates(&base, "ph");
    let filtered = filter_candidates_by_name(v, "Snare");
    assert_eq!(filtered.len(), 1);
    assert_eq!(filtered[0].instance_id, "iid-a");
}

#[test]
fn filter_candidates_by_name_matches_japanese() {
    let base = isolated_dir();
    write_pre_tmp_with_name(&base, "ph", "iid-jp", "active", "日本語Snare");
    write_pre_tmp_with_name(&base, "ph", "iid-other", "active", "Kick");
    let v = scan_pre_candidates(&base, "ph");
    let filtered = filter_candidates_by_name(v, "日本語Snare");
    assert_eq!(filtered.len(), 1, "UTF-8 name should match exactly");
    assert_eq!(filtered[0].instance_id, "iid-jp");
}

#[test]
fn filter_candidates_by_name_drops_mismatch() {
    let base = isolated_dir();
    write_pre_tmp_with_name(&base, "ph", "iid-a", "active", "Snare");
    write_pre_tmp_with_name(&base, "ph", "iid-b", "active", "Kick");
    let v = scan_pre_candidates(&base, "ph");
    let filtered = filter_candidates_by_name(v, "Hat");
    assert!(
        filtered.is_empty(),
        "unknown name should return no candidates"
    );
}

#[test]
fn filter_candidates_by_name_empty_passes_through() {
    let base = isolated_dir();
    write_pre_tmp_with_name(&base, "ph", "iid-a", "active", "Snare");
    write_pre_tmp_with_state(&base, "ph", "iid-legacy", "active");
    let v = scan_pre_candidates(&base, "ph");
    assert_eq!(v.len(), 2);
    let filtered = filter_candidates_by_name(v, "");
    assert_eq!(filtered.len(), 2, "empty name does not filter candidates");
}

#[test]
fn filter_candidates_by_name_keeps_multiple_matches() {
    let base = isolated_dir();
    write_pre_tmp_with_name(&base, "ph", "iid-a", "active", "Snare");
    write_pre_tmp_with_name(&base, "ph", "iid-b", "active", "Snare");
    write_pre_tmp_with_name(&base, "ph", "iid-c", "active", "Kick");
    let v = scan_pre_candidates(&base, "ph");
    let filtered = filter_candidates_by_name(v, "Snare");
    assert_eq!(
        filtered.len(),
        2,
        "same-name PRE instances all pass through"
    );
    let ids: Vec<&str> = filtered.iter().map(|c| c.instance_id.as_str()).collect();
    assert!(ids.contains(&"iid-a"));
    assert!(ids.contains(&"iid-b"));
}

#[test]
fn enumerate_active_pre_pair_candidates_flattens_all_active_pre_dirs() {
    let base = isolated_dir();
    write_pre_tmp_with_name(&base, "uuid_a", "iid-a1", "active", "Snare");
    write_pre_tmp_with_name(&base, "uuid_a", "iid-a2", "active", "Kick");
    write_pre_tmp_with_name(&base, "uuid_b", "iid-b1", "active", "Hat");

    let v = enumerate_active_pre_pair_candidates(&base);
    let ids: Vec<&str> = v.iter().map(|c| c.instance_id.as_str()).collect();
    assert_eq!(
        v.len(),
        3,
        "all project_uuid dirs should be flattened: {ids:?}"
    );
    assert!(ids.contains(&"iid-a1"));
    assert!(ids.contains(&"iid-a2"));
    assert!(ids.contains(&"iid-b1"));
}

#[test]
fn enumerate_active_pre_pair_candidates_returns_empty_when_no_active_pre_dir() {
    let base = isolated_dir();
    fs::create_dir_all(base.join("uuid_x").join("iid_x")).unwrap();

    let v = enumerate_active_pre_pair_candidates(&base);
    assert!(v.is_empty(), "no pre.json means no active PRE candidates");
}
