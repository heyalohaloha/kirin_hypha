use super::*;

#[test]
fn lease_enables_exact_pre_and_close_disables_it() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let pre_runtime = SpectrumRuntime::new(48_000, 2);
    let post_runtime = SpectrumRuntime::new(48_000, 2);
    let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
    let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
    let target = SpectrumTarget::from_pre_json("pre".to_string(), &pre_json).unwrap();

    post.set_post_visible(true);
    post.post_tick("post", Some(target.clone()));
    assert!(post_runtime.is_enabled());
    pre.pre_tick("pre", &pre_dir);
    assert!(pre_runtime.is_enabled());

    post.set_post_visible(false);
    post.post_tick("post", Some(target));
    pre.pre_tick("pre", &pre_dir);
    assert!(!post_runtime.is_enabled());
    assert!(!pre_runtime.is_enabled());
    assert!(!request_path(&pre_dir).exists());

    pre.shutdown();
    post.shutdown();
    pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
}
