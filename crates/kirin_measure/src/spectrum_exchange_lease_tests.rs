use super::*;

#[test]
fn lease_enables_exact_pre_and_close_disables_post_then_pre_at_expiry() {
    let temp = tempfile::tempdir().unwrap();
    let pre_dir = temp.path().join("project").join("pre");
    let pre_json = pre_dir.join("pre.json");
    crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
    let pre_runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let post_runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
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
    assert!(!post_runtime.is_enabled());
    assert_eq!(post.try_view().unwrap().status, SpectrumViewStatus::Hidden);
    let expires = read_request(&pre_dir).unwrap().expires_at_unix_ms;
    let remaining = expires.saturating_sub(unix_ms_now()).saturating_add(1);
    if remaining > 0 {
        std::thread::sleep(Duration::from_millis(remaining as u64));
    }
    assert!(validated_request(&pre_dir, "pre", 48_000, unix_ms_now()).is_none());
    assert!(!pre.pre_tick("pre", &pre_dir));
    assert!(!pre_runtime.is_enabled());

    pre.shutdown();
    post.shutdown();
    pre_runtime.shutdown_and_join();
    post_runtime.shutdown_and_join();
}
