use super::render::{render_snapshot, render_table};
use super::*;
use std::sync::atomic::{AtomicUsize, Ordering};

static NEXT_ID: AtomicUsize = AtomicUsize::new(0);

fn temp_root(label: &str) -> PathBuf {
    let n = NEXT_ID.fetch_add(1, Ordering::Relaxed);
    let root = std::env::temp_dir().join(format!(
        "kirin_diagnose_watch_{label}_{}_{}",
        std::process::id(),
        n
    ));
    let _ = fs::remove_dir_all(&root);
    fs::create_dir_all(&root).unwrap();
    root
}

fn watch_pre(project: &str, instance: &str, name: &str, state: &str, age_secs: u64) -> WatchRow {
    WatchRow {
        role: "PRE".to_string(),
        project: project.to_string(),
        instance: instance.to_string(),
        name: name.to_string(),
        signal_state: state.to_string(),
        peer_state: "-".to_string(),
        pair_pre_name: "-".to_string(),
        age_secs: Some(age_secs),
        age_s: format!("{age_secs}s"),
        path: PathBuf::from(format!("{project}/{instance}/pre.json")),
    }
}

fn watch_post(project: &str, instance: &str, pair_pre_name: &str, age_secs: u64) -> WatchRow {
    WatchRow {
        role: "POST".to_string(),
        project: project.to_string(),
        instance: instance.to_string(),
        name: "-".to_string(),
        signal_state: "active".to_string(),
        peer_state: "active".to_string(),
        pair_pre_name: pair_pre_name.to_string(),
        age_secs: Some(age_secs),
        age_s: format!("{age_secs}s"),
        path: PathBuf::from(format!("{project}/{instance}/post.json")),
    }
}

#[test]
fn collect_snapshot_reports_watch_pre_and_post() {
    let root = temp_root("watch");
    let project = root.join("project-a");
    fs::create_dir_all(project.join("pre-iid")).unwrap();
    fs::create_dir_all(project.join("post-iid")).unwrap();
    fs::write(
        project.join("pre-iid/pre.json"),
        r#"{"instance_id":"pre-iid","signal_state":"inactive","name":"Mix"}"#,
    )
    .unwrap();
    fs::write(
        project.join("post-iid/post.json"),
        r#"{"instance_id":"post-iid","signal_state":"active","pre_signal_state":"inactive","pair_pre_name":"Mix"}"#,
    )
    .unwrap();

    let snapshot = collect_snapshot(root.clone(), None);

    assert_eq!(snapshot.watch_rows.len(), 2);
    assert!(snapshot.watch_rows.iter().any(|r| {
        r.role == "PRE"
            && r.project == "project-a"
            && r.instance == "pre-iid"
            && r.name == "Mix"
            && r.signal_state == "inactive"
    }));
    assert!(snapshot.watch_rows.iter().any(|r| {
        r.role == "POST"
            && r.instance == "post-iid"
            && r.peer_state == "inactive"
            && r.pair_pre_name == "Mix"
    }));

    let _ = fs::remove_dir_all(root);
}

#[test]
fn collect_snapshot_reports_plugin_data_signals_and_records() {
    let kirin_root = temp_root("empty_watch");
    let plugin_root = temp_root("plugin");
    let project = plugin_root.join("project-a");
    fs::create_dir_all(project.join("record_signal")).unwrap();
    fs::create_dir_all(project.join("post-iid/post")).unwrap();
    fs::write(
        project.join("record_signal/post-iid.json"),
        r#"{"status":"acknowledged","requested_by":"post-iid","target_pre_instance_id":"pre-iid","paired_pre_name":"Mix","daw_session_id":"daw-a","t":"2026-06-26T00:00:00Z"}"#,
    )
    .unwrap();
    fs::write(
        project.join("post-iid/post/20260626T000000.json"),
        r#"{"status":"closed"}"#,
    )
    .unwrap();

    let snapshot = collect_snapshot(kirin_root.clone(), Some(plugin_root.clone()));

    assert_eq!(snapshot.signal_rows.len(), 1);
    assert_eq!(snapshot.signal_rows[0].status, "acknowledged");
    assert_eq!(snapshot.signal_rows[0].target_pre, "pre-iid");
    assert_eq!(snapshot.signal_rows[0].pair_name, "Mix");
    assert_eq!(snapshot.record_rows.len(), 1);
    assert_eq!(snapshot.record_rows[0].post_files, 1);
    assert_eq!(snapshot.record_rows[0].closed_files, 1);

    let _ = fs::remove_dir_all(kirin_root);
    let _ = fs::remove_dir_all(plugin_root);
}

#[test]
fn render_table_handles_empty_rows() {
    assert_eq!(render_table(&["a"], Vec::new(), Some(40)), "  (none)\n");
}

#[test]
fn render_table_truncates_with_omitted_count() {
    let out = render_table(
        &["a"],
        vec![
            vec!["one".to_string()],
            vec!["two".to_string()],
            vec!["three".to_string()],
        ],
        Some(2),
    );

    assert!(out.contains("one"));
    assert!(out.contains("two"));
    assert!(!out.contains("three"));
    assert!(out.contains("1 more rows hidden"));
}

#[test]
fn render_snapshot_includes_operational_summary() {
    let snapshot = Snapshot {
        watch_rows: vec![
            watch_pre("p", "pre", "Mix", "active", 1),
            watch_post("p", "post", "Mix", 1),
        ],
        signal_rows: vec![SignalRow {
            kind: "record_signal".to_string(),
            project: "p".to_string(),
            file: "post.json".to_string(),
            status: "pending".to_string(),
            requested_by: "post".to_string(),
            target_pre: "pre".to_string(),
            pair_name: "Mix".to_string(),
            daw_session: "daw".to_string(),
            t: "t".to_string(),
            age_secs: Some(1),
            age_s: "1s".to_string(),
            path: PathBuf::from("signal.json"),
        }],
        record_rows: vec![RecordRow {
            project: "p".to_string(),
            instance: "post".to_string(),
            pre_files: 0,
            post_files: 1,
            active_files: 1,
            closed_files: 0,
            latest_status: "active".to_string(),
            latest_age_secs: Some(1),
            latest_path: "record.json".to_string(),
        }],
        ..Snapshot::default()
    };

    let out = render_snapshot(&snapshot, Some(40));

    assert!(out.contains("Summary"));
    assert!(out.contains("live_pre:        1"));
    assert!(out.contains("live_post:       1"));
    assert!(out.contains("eligible_pre:    1"));
    assert!(out.contains("paired_post:     1"));
    assert!(out.contains("pending_signals: 1"));
    assert!(out.contains("active_records:  1"));
}

#[test]
fn findings_explain_stale_pre_hidden_from_pair_candidates() {
    let mut snapshot = Snapshot {
        watch_rows: vec![
            watch_pre(
                "pre-project",
                "pre-mix",
                "Mix",
                "active",
                kirin_measure::DISCOVERY_STALE_SECS + 1,
            ),
            watch_post("post-project", "post-mix", "Mix", 1),
        ],
        ..Snapshot::default()
    };

    refresh_findings(&mut snapshot);
    let out = render_snapshot(&snapshot, Some(40));

    assert!(out.contains("eligible_pre:    0"));
    assert!(out.contains("Findings"));
    assert!(out.contains("STALE_PRE"));
    assert!(out.contains("POST_TARGET_UNAVAILABLE"));
    assert!(out.contains("pair_pre_name \"Mix\" exists only as stale or bypassed PRE"));
}

#[test]
fn findings_explain_ambiguous_duplicate_pre_names() {
    let mut snapshot = Snapshot {
        watch_rows: vec![
            watch_pre("p1", "pre-a", "Mix", "active", 1),
            watch_pre("p2", "pre-b", "Mix", "inactive", 1),
            watch_post("post-project", "post-mix", "Mix", 1),
        ],
        ..Snapshot::default()
    };

    refresh_findings(&mut snapshot);
    let out = render_snapshot(&snapshot, Some(40));

    assert!(out.contains("eligible_pre:    2"));
    assert!(out.contains("DUPLICATE_PRE_NAME"));
    assert!(out.contains("POST_TARGET_AMBIGUOUS"));
    assert!(out.contains("fresh non-bypassed PRE rows share this name"));
}

#[test]
fn history_filter_keeps_pending_but_hides_old_released() {
    let mut snapshot = Snapshot::default();
    snapshot.signal_rows.push(SignalRow {
        kind: "record_signal".to_string(),
        project: "p".to_string(),
        file: "old.json".to_string(),
        status: "released".to_string(),
        requested_by: "post".to_string(),
        target_pre: "pre".to_string(),
        pair_name: "-".to_string(),
        daw_session: "daw".to_string(),
        t: "t".to_string(),
        age_secs: Some(120),
        age_s: "120s".to_string(),
        path: PathBuf::from("old.json"),
    });
    snapshot.signal_rows.push(SignalRow {
        kind: "record_signal".to_string(),
        project: "p".to_string(),
        file: "pending.json".to_string(),
        status: "pending".to_string(),
        requested_by: "post".to_string(),
        target_pre: "pre".to_string(),
        pair_name: "-".to_string(),
        daw_session: "daw".to_string(),
        t: "t".to_string(),
        age_secs: Some(120),
        age_s: "120s".to_string(),
        path: PathBuf::from("pending.json"),
    });

    apply_history_filter(&mut snapshot, 60);

    assert_eq!(snapshot.signal_rows.len(), 1);
    assert_eq!(snapshot.signal_rows[0].status, "pending");
}

#[test]
fn sort_snapshot_orders_records_by_latest_age() {
    let mut snapshot = Snapshot {
        record_rows: vec![
            RecordRow {
                project: "p".to_string(),
                instance: "old".to_string(),
                pre_files: 0,
                post_files: 1,
                active_files: 0,
                closed_files: 1,
                latest_status: "closed".to_string(),
                latest_age_secs: Some(20),
                latest_path: "old.json".to_string(),
            },
            RecordRow {
                project: "p".to_string(),
                instance: "new".to_string(),
                pre_files: 0,
                post_files: 1,
                active_files: 0,
                closed_files: 1,
                latest_status: "closed".to_string(),
                latest_age_secs: Some(1),
                latest_path: "new.json".to_string(),
            },
        ],
        ..Snapshot::default()
    };

    apply_history_filter(&mut snapshot, 60);
    sort_snapshot(&mut snapshot);

    assert_eq!(snapshot.record_rows[0].instance, "new");
}
