use super::*;
use crate::local_blind_capture_protocol::{
    publish_local_blind_capture_armed, publish_local_blind_capture_request,
    LocalBlindCaptureRequest, LocalBlindPairAuthority,
};
use crate::PairOwnershipLease;

struct Fixture {
    root: tempfile::TempDir,
    pre_dir: PathBuf,
    request: LocalBlindCaptureRequest,
    _lease: PairOwnershipLease,
}

impl Fixture {
    fn new() -> Self {
        Self::new_with_armed(true)
    }

    fn new_with_armed(armed: bool) -> Self {
        let root = tempfile::tempdir().unwrap();
        let pre_dir = root.path().join("pre-project").join("pre-a");
        std::fs::create_dir_all(&pre_dir).unwrap();
        let post_dir = root.path().join("post-project").join("post-a");
        let lease = PairOwnershipLease::new();
        lease
            .commit_claimed_binding_if(
                root.path(),
                Some(&post_dir),
                Some("pre-a"),
                "post-project",
                "post-a",
                7.0,
                || true,
                || Some(()),
            )
            .unwrap()
            .expect("exact pair claim");
        let request = LocalBlindCaptureRequest::new(
            LocalBlindPairAuthority {
                pair_generation: 11,
                pair_owner_id: lease.owner_id().to_string(),
                pair_claimed_at_bits: 7.0f64.to_bits(),
                host_process_id: std::process::id(),
                post_project_hash: "post-project".into(),
                post_instance_id: "post-a".into(),
                pre_project_hash: "pre-project".into(),
                pre_instance_id: "pre-a".into(),
            },
            22,
            33,
            1,
            -10,
            48_000,
            2,
            -4,
            6,
            1_000,
            10_000,
        )
        .unwrap();
        publish_local_blind_capture_request(root.path(), &pre_dir, &request).unwrap();
        if armed {
            publish_local_blind_capture_armed(root.path(), &pre_dir, &request, 1_001).unwrap();
        }
        Self {
            root,
            pre_dir,
            request,
            _lease: lease,
        }
    }
}

#[test]
fn result_transport_requires_the_exact_pre_arm_proof() {
    let fixture = Fixture::new_with_armed(false);
    assert!(publish_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &pcm(),
        11_001,
    )
    .is_err());
}

#[test]
fn exact_pre_failure_round_trips_after_admission_deadline() {
    let fixture = Fixture::new();
    publish_local_blind_pre_capture_failure(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        6,
        7,
        11_001,
    )
    .unwrap();
    let failure = read_local_blind_pre_capture_failure(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        11_002,
    )
    .expect("matching terminal PRE failure");
    assert_eq!(failure.request_id, fixture.request.request_id);
    assert_eq!(failure.owner_failure, 6);
    assert_eq!(failure.capture_failure, 7);
}

#[test]
fn pre_failure_rejects_unarmed_invalid_and_mismatched_requests() {
    let unarmed = Fixture::new_with_armed(false);
    assert!(publish_local_blind_pre_capture_failure(
        unarmed.root.path(),
        &unarmed.pre_dir,
        &unarmed.request,
        6,
        1,
        1_002,
    )
    .is_err());

    let fixture = Fixture::new();
    for (owner, lane) in [(0, 0), (8, 0), (2, 1), (6, 8)] {
        assert!(publish_local_blind_pre_capture_failure(
            fixture.root.path(),
            &fixture.pre_dir,
            &fixture.request,
            owner,
            lane,
            1_002,
        )
        .is_err());
    }
    let mut changed = fixture.request.clone();
    changed.capture_generation += 1;
    assert!(read_local_blind_pre_capture_failure(
        fixture.root.path(),
        &fixture.pre_dir,
        &changed,
        1_003,
    )
    .is_none());
}

#[test]
fn one_request_cannot_publish_both_terminal_results() {
    let failed = Fixture::new();
    publish_local_blind_pre_capture_failure(
        failed.root.path(),
        &failed.pre_dir,
        &failed.request,
        6,
        7,
        1_002,
    )
    .unwrap();
    assert!(publish_local_blind_pre_capture(
        failed.root.path(),
        &failed.pre_dir,
        &failed.request,
        &pcm(),
        1_003,
    )
    .is_err());

    let completed = Fixture::new();
    publish_local_blind_pre_capture(
        completed.root.path(),
        &completed.pre_dir,
        &completed.request,
        &pcm(),
        1_002,
    )
    .unwrap();
    assert!(publish_local_blind_pre_capture_failure(
        completed.root.path(),
        &completed.pre_dir,
        &completed.request,
        6,
        7,
        1_003,
    )
    .is_err());
}

fn pcm() -> Vec<f32> {
    vec![
        0.0, -0.0, 0.25, -0.75, 1.0, -1.0, 0.125, 0.5, -0.1, 0.2, 0.3, -0.4,
    ]
}

#[test]
fn exact_pre_pcm_and_receipt_round_trip_before_consumption() {
    let fixture = Fixture::new();
    let source = pcm();
    let receipt = publish_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &source,
        1_001,
    )
    .unwrap();
    let read = read_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        1_002,
    )
    .expect("exact immutable PRE capture");
    assert_eq!(read.receipt, receipt);
    assert_eq!(read.receipt.start, -4);
    assert_eq!(read.receipt.frames, 6);
    for (actual, expected) in read.interleaved.iter().zip(source.iter()) {
        assert_eq!(actual.to_bits(), expected.to_bits());
    }
    assert!(!local_blind_pre_capture_was_consumed(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &receipt.pcm_sha256,
        1_003,
    ));
    publish_local_blind_pre_capture_consumed(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &receipt.pcm_sha256,
        1_003,
    )
    .unwrap();
    assert!(local_blind_pre_capture_was_consumed(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &receipt.pcm_sha256,
        1_004,
    ));
    std::fs::write(
        consumed_path(&fixture.pre_dir, &fixture.request),
        vec![0; HEADER_MAX_BYTES + 1],
    )
    .unwrap();
    assert!(!local_blind_pre_capture_was_consumed(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &receipt.pcm_sha256,
        1_004,
    ));
    remove_local_blind_pre_capture(&fixture.pre_dir, &fixture.request.request_id).unwrap();
    assert!(read_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        1_005,
    )
    .is_none());
}

#[test]
fn immutable_identity_rejects_different_pcm_and_tampering() {
    let fixture = Fixture::new();
    let source = pcm();
    publish_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &source,
        1_001,
    )
    .unwrap();
    publish_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &source,
        1_002,
    )
    .expect("equal retry is idempotent");
    let mut changed = source.clone();
    changed[3] = 0.75;
    assert!(publish_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &changed,
        1_002,
    )
    .is_err());

    let path = capture_path(&fixture.pre_dir, &fixture.request);
    let mut bytes = std::fs::read(&path).unwrap();
    *bytes.last_mut().unwrap() ^= 0x01;
    std::fs::write(path, bytes).unwrap();
    assert!(read_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        1_003,
    )
    .is_none());
}

#[test]
fn malformed_missing_and_stale_pair_results_fail_closed_after_admission() {
    let fixture = Fixture::new();
    let source = pcm();
    for invalid_source in [source[..source.len() - 1].to_vec(), {
        let mut value = source.clone();
        value[0] = f32::NAN;
        value
    }] {
        assert!(publish_local_blind_pre_capture(
            fixture.root.path(),
            &fixture.pre_dir,
            &fixture.request,
            &invalid_source,
            1_001,
        )
        .is_err());
    }
    assert!(read_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        1_001,
    )
    .is_none());
    let oversized_path = capture_path(&fixture.pre_dir, &fixture.request);
    std::fs::create_dir_all(oversized_path.parent().unwrap()).unwrap();
    let maximum_blob =
        BLOB_MAGIC.len() + HEADER_LENGTH_BYTES + HEADER_MAX_BYTES + source.len() * size_of::<f32>();
    std::fs::write(&oversized_path, vec![0; maximum_blob + 1]).unwrap();
    assert!(read_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        1_001,
    )
    .is_none());
    std::fs::remove_file(oversized_path).unwrap();
    let completed_after_admission = publish_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &source,
        11_001,
    )
    .expect("an admitted exact capture may finalize after its admission deadline");
    assert!(read_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        11_002,
    )
    .is_some());
    assert!(canonical_sha256(&completed_after_admission.pcm_sha256));

    fixture
        ._lease
        .commit_claimed_binding_if(
            fixture.root.path(),
            Some(&fixture.root.path().join("post-project").join("post-a")),
            None,
            "post-project",
            "post-a",
            0.0,
            || true,
            || Some(()),
        )
        .unwrap();
    assert!(publish_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &source,
        1_002,
    )
    .is_err());
    assert!(remove_local_blind_pre_capture(&fixture.pre_dir, "../not-a-request").is_err());
}
