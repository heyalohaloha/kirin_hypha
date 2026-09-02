use super::*;
use std::sync::atomic::AtomicU64;

fn isolated_dir(tag: &str) -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_{tag}_{pid}_{n}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn write_pre_latch(
    kirin_root: &Path,
    puid: &str,
    iid: &str,
    name: &str,
    signal_state: &str,
    t: &str,
) {
    let dir = kirin_root.join(puid).join(iid);
    fs::create_dir_all(&dir).unwrap();
    let host_process_id = crate::post_candidates::current_host_process_id();
    let json = format!(
        r#"{{"v":2,"role":"PRE","instance_id":"{iid}","name":"{name}","host_process_id":{host_process_id},"signal_state":"{signal_state}","t":"{t}","lufs_m":-14.0,"true_peak":-1.0,"crest":12.0,"psr":8.0}}"#
    );
    fs::write(dir.join("pre.json"), json).unwrap();
}

fn latch_now() -> String {
    chrono::Utc::now().format("%Y-%m-%dT%H:%M:%SZ").to_string()
}

fn latch_post() -> MeasureResult {
    MeasureResult {
        lufs_m: Some(-10.0),
        true_peak: Some(-1.0),
        crest: Some(12.0),
        ..Default::default()
    }
}

#[test]
fn latch_inactive_switches_to_post_absolute_without_releasing_pair() {
    let root = isolated_dir("latch_idle");
    write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
    let latched = std::sync::Mutex::new(None);
    let (active, stores_directly, _) = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert_eq!(active.mode, DeltaMode::Active);
    assert!(!stores_directly);
    assert!(latched.lock().unwrap().is_some());

    write_pre_latch(&root, "puid-1", "iid-A", "snare", "inactive", &latch_now());
    let (inactive, stores_directly, pre_state) = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert_eq!(inactive.mode, DeltaMode::PreInactive);
    assert_eq!(pre_state, Some(SignalState::Inactive));
    assert!(stores_directly);
    assert!(inactive.lufs.is_none() && inactive.last_active.is_none());
    assert!(latched.lock().unwrap().is_some());
}

#[test]
fn latch_pre_bypassed_keeps_pair_but_marks_bypassed() {
    let root = isolated_dir("latch_pre_bypassed");
    write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
    let latched = std::sync::Mutex::new(None);
    compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();

    write_pre_latch(&root, "puid-1", "iid-A", "snare", "bypassed", &latch_now());
    let (delta, stores_directly, pre_state) = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert_eq!(delta.mode, DeltaMode::Bypassed);
    assert!(stores_directly);
    assert_eq!(pre_state, Some(SignalState::Bypassed));
    assert!(latched.lock().unwrap().is_some());
}

#[test]
fn latch_inactive_then_active_yields_live_delta() {
    let root = isolated_dir("latch_inactive_to_active");
    write_pre_latch(&root, "puid-1", "iid-A", "snare", "inactive", &latch_now());
    let latched = std::sync::Mutex::new(None);
    let (inactive, stores_directly, _) = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert!(latched.lock().unwrap().is_some());
    assert_eq!(inactive.mode, DeltaMode::PreInactive);
    assert!(stores_directly);
    assert!(inactive.lufs.is_none());

    write_pre_latch(&root, "puid-1", "iid-A", "snare", "active", &latch_now());
    let (active, stores_directly, _) = compute_latched_display(
        &root,
        "snare",
        &latch_post(),
        Some("snare"),
        false,
        &latched,
    )
    .unwrap();
    assert_eq!(active.mode, DeltaMode::Active);
    assert!(!stores_directly);
    assert_eq!(active.lufs, Some(4.0));
    assert!(latched.lock().unwrap().is_some());
}
