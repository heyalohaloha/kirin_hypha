use super::render::render_table;
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
    assert_eq!(render_table(&["a"], Vec::new()), "  (none)\n");
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
