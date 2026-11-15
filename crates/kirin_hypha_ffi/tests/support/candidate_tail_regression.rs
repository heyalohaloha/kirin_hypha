use kirin_hypha_ffi::KirinHyphaEngine;

pub fn wait_and_resume(origin_post: &KirinHyphaEngine, tail_post: &KirinHyphaEngine) {
    unsafe extern "C" {
        fn kirin_hypha_keep_all_resume(handle: *mut std::ffi::c_void) -> bool;
        fn kirin_hypha_keep_all_stage(handle: *mut std::ffi::c_void) -> bool;
        fn kirin_hypha_all_keep_tail_pending(handle: *mut std::ffi::c_void) -> bool;
    }
    let origin = (origin_post as *const KirinHyphaEngine).cast_mut().cast();
    assert!(
        !unsafe { kirin_hypha_keep_all_resume(origin) },
        "a resume without the pressed roster must not start a new All Keep"
    );
    assert_eq!(
        origin_post.drain_keep_action_notice().as_deref(),
        Some("All Keep targets changed or unavailable. Try again.")
    );
    // A different POST still has downstream comparison audio. The held lease models the shell's
    // tail authority, including when the two POSTs come from different plug-in formats.
    let tail_path = kirin_measure::storage::StoragePaths::default_platform()
        .unwrap()
        .plugin_data_dir()
        .join("candidate_tail")
        .join("puid-post-vst")
        .join("iid-post-music.lock");
    std::fs::create_dir_all(tail_path.parent().unwrap()).unwrap();
    let tail = std::fs::OpenOptions::new()
        .read(true)
        .write(true)
        .create(true)
        .truncate(false)
        .open(tail_path)
        .unwrap();
    tail.try_lock().unwrap();
    assert!(
        unsafe { kirin_hypha_keep_all_stage(origin) },
        "the local wait can retain the pressed roster"
    );
    assert!(unsafe { kirin_hypha_all_keep_tail_pending(origin) });
    assert!(!origin_post.is_recording() && !tail_post.is_recording());
    assert!(
        !origin_post.keep_all(),
        "All Keep must wait for a different POST's tail"
    );
    assert!(!origin_post.is_recording() && !tail_post.is_recording());
    assert!(unsafe { kirin_hypha_all_keep_tail_pending(origin) });
    tail.unlock().unwrap();
    assert!(!unsafe { kirin_hypha_all_keep_tail_pending(origin) });
    assert!(
        unsafe { kirin_hypha_keep_all_resume(origin) },
        "the same All Keep resumes for the exact roster"
    );
}
