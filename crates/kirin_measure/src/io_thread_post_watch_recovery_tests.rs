use super::*;
use std::sync::atomic::AtomicU64;

fn isolated_root() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let root = std::env::temp_dir().join(format!(
        "kirin_post_watch_recovery_{}_{}",
        std::process::id(),
        n
    ));
    let _ = fs::remove_dir_all(&root);
    fs::create_dir_all(&root).unwrap();
    root
}

#[test]
fn active_watch_tick_recovers_poisoned_result_locks() {
    let root = isolated_root();
    let project_dir = root.join("project");
    let instance_dir = project_dir.join("post-instance");
    let post_file = instance_dir.join("post.json");
    let post_result = Arc::new(Mutex::new(MeasureResult {
        lufs_m: Some(-11.0),
        ..Default::default()
    }));
    let delta_result = Arc::new(Mutex::new(DeltaResult::default()));

    let post_poison_target = Arc::clone(&post_result);
    let _ = std::thread::spawn(move || {
        let _guard = post_poison_target.lock().unwrap();
        panic!("poison fixture");
    })
    .join();
    let delta_poison_target = Arc::clone(&delta_result);
    let _ = std::thread::spawn(move || {
        let _guard = delta_poison_target.lock().unwrap();
        panic!("poison fixture");
    })
    .join();
    assert!(post_result.is_poisoned());
    assert!(delta_result.is_poisoned());

    let state = Arc::new(AtomicU8::new(SignalState::Active as u8));
    let latched = Mutex::new(None);
    run_tick(
        &project_dir,
        &root,
        &mut PostDiscoveryState::new(),
        &instance_dir,
        &post_file,
        "post-instance",
        "owner",
        &post_result,
        &delta_result,
        &state,
        "",
        0.0,
        "project",
        "daw",
        false,
        &latched,
        false,
    )
    .expect("poisoned result locks must not stop Watch JSON");

    assert!(!post_result.is_poisoned());
    assert!(!delta_result.is_poisoned());

    let parsed: PostTmpJson = serde_json::from_slice(&fs::read(&post_file).unwrap()).unwrap();
    assert_eq!(parsed.instance_id, "post-instance");
    assert_eq!(parsed.lufs_m, Some(-11.0));
    assert!(!parsed.t.is_empty());
}
