use super::*;
use kirin_hypha_ffi::{
    kirin_hypha_ack_local_blind_capture_request, kirin_hypha_ack_local_blind_pre_capture,
    kirin_hypha_issue_local_blind_capture_request_v2, kirin_hypha_local_blind_capture_is_armed,
    kirin_hypha_local_blind_pre_capture_was_consumed, kirin_hypha_poll_local_blind_capture_request,
    kirin_hypha_publish_local_blind_pre_capture, kirin_hypha_read_local_blind_pre_capture,
    kirin_hypha_retire_local_blind_pre_capture, KirinLocalBlindCaptureRequest,
    KirinLocalBlindPreCaptureReceipt,
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
            kirin_hypha_issue_local_blind_capture_request_v2(
                post_handle,
                22,
                33,
                1,
                0,
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
    assert_eq!(issued.clock_source, 1);
    assert_eq!(issued.clock_position_at_issue, 0);

    let mut received = KirinLocalBlindCaptureRequest::default();
    assert!(unsafe { kirin_hypha_poll_local_blind_capture_request(pre_handle, &mut received) });
    assert_eq!(received.request_id, issued.request_id);
    assert_eq!(received.native_start, 0);
    assert_eq!(received.frames, 192_000);
    assert!(unsafe {
        kirin_hypha_ack_local_blind_capture_request(pre_handle, received.request_id.as_ptr())
    });
    assert!(unsafe {
        kirin_hypha_local_blind_capture_is_armed(post_handle, received.request_id.as_ptr())
    });

    let sample_count =
        usize::try_from(received.frames).unwrap() * usize::try_from(received.channels).unwrap();
    let source = (0..sample_count)
        .map(|index| {
            if index % 11 == 0 {
                -0.0
            } else {
                (index % 97) as f32 / 96.0 - 0.5
            }
        })
        .collect::<Vec<_>>();
    let mut pre_receipt = KirinLocalBlindPreCaptureReceipt::default();
    assert!(unsafe {
        kirin_hypha_publish_local_blind_pre_capture(
            pre_handle,
            received.request_id.as_ptr(),
            source.as_ptr(),
            source.len(),
            &mut pre_receipt,
        )
    });
    assert_eq!(pre_receipt.pair_generation, received.pair_generation);
    assert_eq!(pre_receipt.sample_count, source.len() as u64);

    let mut rejected_pcm = vec![123.0; source.len() - 1];
    let mut rejected_receipt = KirinLocalBlindPreCaptureReceipt {
        pair_generation: 999,
        ..KirinLocalBlindPreCaptureReceipt::default()
    };
    assert!(!unsafe {
        kirin_hypha_read_local_blind_pre_capture(
            post_handle,
            received.request_id.as_ptr(),
            rejected_pcm.as_mut_ptr(),
            rejected_pcm.len(),
            &mut rejected_receipt,
        )
    });
    assert!(rejected_pcm
        .iter()
        .all(|value| value.to_bits() == 123.0f32.to_bits()));
    assert_eq!(
        rejected_receipt.pair_generation, 999,
        "rejected read must leave its outputs unchanged"
    );

    let mut copied = vec![0.0; source.len()];
    let mut post_receipt = KirinLocalBlindPreCaptureReceipt::default();
    assert!(unsafe {
        kirin_hypha_read_local_blind_pre_capture(
            post_handle,
            received.request_id.as_ptr(),
            copied.as_mut_ptr(),
            copied.len(),
            &mut post_receipt,
        )
    });
    assert!(copied
        .iter()
        .zip(&source)
        .all(|(actual, expected)| actual.to_bits() == expected.to_bits()));
    assert_eq!(post_receipt.pcm_sha256, pre_receipt.pcm_sha256);
    let mut forged_receipt = post_receipt;
    forged_receipt.clock_generation += 1;
    assert!(!unsafe { kirin_hypha_ack_local_blind_pre_capture(post_handle, &forged_receipt) });
    assert!(!unsafe { kirin_hypha_local_blind_pre_capture_was_consumed(pre_handle, &pre_receipt) });
    assert!(unsafe { kirin_hypha_ack_local_blind_pre_capture(post_handle, &post_receipt) });
    assert!(unsafe { kirin_hypha_local_blind_pre_capture_was_consumed(pre_handle, &pre_receipt) });
    assert!(unsafe {
        kirin_hypha_retire_local_blind_pre_capture(pre_handle, received.request_id.as_ptr())
    });
    copied.fill(321.0);
    assert!(!unsafe {
        kirin_hypha_read_local_blind_pre_capture(
            post_handle,
            received.request_id.as_ptr(),
            copied.as_mut_ptr(),
            copied.len(),
            &mut post_receipt,
        )
    });
    assert!(copied
        .iter()
        .all(|value| value.to_bits() == 321.0f32.to_bits()));

    post.set_pair_target(String::new());
    assert!(!unsafe {
        kirin_hypha_local_blind_capture_is_armed(post_handle, received.request_id.as_ptr())
    });

    drop(post);
    drop(pre);
    let _ = std::fs::remove_dir_all(home.parent().unwrap_or(&home));
    let _ = std::fs::remove_dir_all(tmp.parent().unwrap_or(&tmp));
}
