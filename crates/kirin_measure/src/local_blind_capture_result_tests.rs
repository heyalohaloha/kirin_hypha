use super::*;
use crate::local_blind_capture_protocol::{LocalBlindCaptureRequest, LocalBlindPairAuthority};
use crate::PairOwnershipLease;

struct Fixture {
    root: tempfile::TempDir,
    pre_dir: PathBuf,
    request: LocalBlindCaptureRequest,
    _lease: PairOwnershipLease,
}

impl Fixture {
    fn new() -> Self {
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
        Self {
            root,
            pre_dir,
            request,
            _lease: lease,
        }
    }
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
fn malformed_missing_expired_and_stale_pair_results_fail_closed() {
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
    assert!(publish_local_blind_pre_capture(
        fixture.root.path(),
        &fixture.pre_dir,
        &fixture.request,
        &source,
        11_001,
    )
    .is_err());

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
