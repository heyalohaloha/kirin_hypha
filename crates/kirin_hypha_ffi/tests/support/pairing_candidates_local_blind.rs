use super::*;
use kirin_hypha_ffi::{
    kirin_hypha_ack_local_blind_capture_request, kirin_hypha_issue_local_blind_capture_request,
    kirin_hypha_local_blind_capture_is_armed, kirin_hypha_poll_local_blind_capture_request,
    KirinLocalBlindCaptureRequest,
};

#[test]
#[ignore = "slow: exact PRE/POST request handshake with IO threads (sets HOME/TMPDIR)"]
fn exact_unnamed_pair_completes_the_local_blind_request_handshake() {
    let (home, tmp) = isolate_env("local_blind_request");
    let pre = spawn_pre("pre-unnamed", "pre-project", "daw-a", "");
    let post = spawn_post("post-a", "post-project", "daw-a", "");
    let select_deadline = Instant::now() + Duration::from_secs(4);
    while !post.set_pair_candidate("pre-unnamed") {
        assert!(
            Instant::now() < select_deadline,
            "unnamed PRE did not become selectable"
        );
        pre.push_samples(&[], 2);
        post.push_samples(&[], 2);
        sleep(Duration::from_millis(20));
    }

    let pre_handle = (&pre as *const KirinHyphaEngine).cast_mut();
    let post_handle = (&post as *const KirinHyphaEngine).cast_mut();
    let mut issued = KirinLocalBlindCaptureRequest::default();
    let mut issued_after_claim = false;
    let claim_deadline = Instant::now() + Duration::from_secs(4);
    while Instant::now() < claim_deadline {
        issued_after_claim = unsafe {
            kirin_hypha_issue_local_blind_capture_request(
                post_handle,
                22,
                33,
                -96,
                0,
                192_000,
                &mut issued,
            )
        };
        if issued_after_claim {
            break;
        }
        pre.push_samples(&[], 2);
        post.push_samples(&[], 2);
        sleep(Duration::from_millis(20));
    }
    assert!(
        issued_after_claim,
        "request did not wait through canonical claim publication"
    );
    assert_ne!(issued.pair_generation, 0);
    assert_eq!(issued.capture_generation, 22);
    assert_eq!(issued.clock_generation, 33);

    let mut received = KirinLocalBlindCaptureRequest::default();
    assert!(unsafe { kirin_hypha_poll_local_blind_capture_request(pre_handle, &mut received) });
    assert_eq!(received.request_id, issued.request_id);
    assert_eq!(received.pre_start, -96);
    assert_eq!(received.post_start, 0);
    assert_eq!(received.frames, 192_000);
    assert!(unsafe {
        kirin_hypha_ack_local_blind_capture_request(pre_handle, received.request_id.as_ptr())
    });
    assert!(unsafe {
        kirin_hypha_local_blind_capture_is_armed(post_handle, received.request_id.as_ptr())
    });

    post.set_pair_target(String::new());
    assert!(!unsafe {
        kirin_hypha_local_blind_capture_is_armed(post_handle, received.request_id.as_ptr())
    });

    drop(post);
    drop(pre);
    let _ = std::fs::remove_dir_all(home.parent().unwrap_or(&home));
    let _ = std::fs::remove_dir_all(tmp.parent().unwrap_or(&tmp));
}
