use super::*;
use kirin_measure::{channel_layout::ChannelLayout, LatchedPre, LatchedPreReadiness};
use std::ffi::CStr;

fn fixture(role: PluginDataRole, project: &str, instance: &str) -> KirinHyphaEngine {
    let engine = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    // Disposable in-memory identity: no IO worker, discovery, audio or preference file.
    engine.set_identity(
        instance.into(),
        project.into(),
        String::new(),
        String::new(),
    );
    engine.identity.lock().unwrap().project_hash = project.into();
    *engine.write_role.lock().unwrap() = Some(role);
    engine
}

fn selected(project: &str, instance: &str) -> LatchedPre {
    LatchedPre {
        name: "same display name".into(),
        instance_id: instance.into(),
        project_dir: project.into(),
        pre_json: format!("{project}/{instance}/pre.json").into(),
        daw_session_id: None,
        host_process_id: None,
        readiness: LatchedPreReadiness::RestoredWaiting,
    }
}

fn handle(engine: &KirinHyphaEngine) -> *mut KirinHyphaEngine {
    (engine as *const KirinHyphaEngine).cast_mut()
}

fn poll(engine: &KirinHyphaEngine) -> Option<([c_char; 67], [c_char; 67])> {
    let mut project = [0x55; 67];
    let mut instance = [0x66; 67];
    let success = unsafe {
        kirin_hypha_get_vu_calibration_locator(
            handle(engine),
            project.as_mut_ptr(),
            65,
            instance.as_mut_ptr(),
            65,
        )
    };
    if success {
        Some((project, instance))
    } else {
        assert_eq!(project, [0x55; 67]);
        assert_eq!(instance, [0x66; 67]);
        None
    }
}

#[test]
fn legal_64_byte_suffixes_survive_on_pre_and_exact_post_without_collision() {
    let project_a = format!("{}a", "p".repeat(63));
    let project_b = format!("{}b", "p".repeat(63));
    let instance_a = format!("{}a", "i".repeat(63));
    let instance_b = format!("{}b", "i".repeat(63));
    let pre_a = fixture(PluginDataRole::Pre, &project_a, &instance_a);
    let pre_b = fixture(PluginDataRole::Pre, &project_b, &instance_b);
    let post = fixture(PluginDataRole::Post, "local-project", "local-post");
    let a = poll(&pre_a).unwrap();
    let b = poll(&pre_b).unwrap();
    assert_ne!(a, b);
    for (actual, expected, sentinel) in [
        (&a.0, &project_a, 0x55),
        (&a.1, &instance_a, 0x66),
        (&b.0, &project_b, 0x55),
        (&b.1, &instance_b, 0x66),
    ] {
        assert_eq!(
            &actual[..64],
            expected
                .as_bytes()
                .iter()
                .map(|b| *b as c_char)
                .collect::<Vec<_>>()
        );
        assert_eq!(actual[64], 0);
        assert_eq!(&actual[65..], &[sentinel; 2]);
    }
    post.pair_binding.replace_exact(
        "same display name".into(),
        selected(&project_a, &instance_a),
    );
    assert_eq!(poll(&post).unwrap(), a); // RestoredWaiting with no PRE file remains exact.
    post.pair_binding.replace_exact(
        "same display name".into(),
        selected(&project_b, &instance_b),
    );
    assert_eq!(poll(&post).unwrap(), b);
    post.pair_binding.replace_name(String::new());
    let local = poll(&post).unwrap();
    assert_eq!(
        unsafe { CStr::from_ptr(local.0.as_ptr()) }.to_bytes(),
        b"local-project"
    );
    assert_eq!(
        unsafe { CStr::from_ptr(local.1.as_ptr()) }.to_bytes(),
        b"local-post"
    );
    post.pair_binding.replace_name("not resolved".into());
    assert!(poll(&post).is_none());
}

#[test]
fn invalid_or_short_either_output_preserves_both_canaries() {
    let engine = fixture(PluginDataRole::Pre, &"p".repeat(64), &"i".repeat(64));
    for (project_len, instance_len) in [(0, 65), (64, 65), (65, 0), (65, 64)] {
        let mut project = [0x55; 67];
        let mut instance = [0x66; 67];
        assert!(!unsafe {
            kirin_hypha_get_vu_calibration_locator(
                handle(&engine),
                project.as_mut_ptr(),
                project_len,
                instance.as_mut_ptr(),
                instance_len,
            )
        });
        assert_eq!(project, [0x55; 67]);
        assert_eq!(instance, [0x66; 67]);
    }
    for null_index in 0..3 {
        let mut project = [0x55; 67];
        let mut instance = [0x66; 67];
        assert!(!unsafe {
            kirin_hypha_get_vu_calibration_locator(
                if null_index == 0 {
                    std::ptr::null_mut()
                } else {
                    handle(&engine)
                },
                if null_index == 1 {
                    std::ptr::null_mut()
                } else {
                    project.as_mut_ptr()
                },
                65,
                if null_index == 2 {
                    std::ptr::null_mut()
                } else {
                    instance.as_mut_ptr()
                },
                65,
            )
        });
        assert_eq!(project, [0x55; 67]);
        assert_eq!(instance, [0x66; 67]);
    }
    let mut same_region = [0x77; 128];
    assert!(!unsafe {
        kirin_hypha_get_vu_calibration_locator(
            handle(&engine),
            same_region.as_mut_ptr(),
            65,
            same_region.as_mut_ptr().add(1),
            65,
        )
    });
    assert_eq!(same_region, [0x77; 128]);
}

#[test]
fn contention_retries_same_authority_and_invalid_identity_fails_closed() {
    let engine = fixture(PluginDataRole::Post, "own-project", "own-post");
    engine
        .pair_binding
        .replace_exact("mix".into(), selected("project-a", "pre-a"));
    let expected = poll(&engine).unwrap();
    {
        let _role = engine.write_role.lock().unwrap();
        assert!(poll(&engine).is_none());
    }
    {
        let slot = engine.pair_binding.latched_pre();
        let _selection = slot.lock().unwrap();
        assert!(poll(&engine).is_none());
    }
    assert_eq!(poll(&engine).unwrap(), expected);
    engine.pair_binding.replace_name(String::new());
    {
        let _identity = engine.identity.lock().unwrap();
        assert!(poll(&engine).is_none());
    }
    assert!(poll(&engine).is_some());
    engine.identity.lock().unwrap().project_hash = "unsafe/path".into();
    assert!(poll(&engine).is_none());
    *engine.write_role.lock().unwrap() = None;
    assert!(poll(&engine).is_none());
}
