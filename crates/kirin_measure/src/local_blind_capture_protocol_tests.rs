use super::*;
use crate::{PairOwnershipLease, PAIR_CLAIM_SCHEMA};

struct Fixture {
    root: tempfile::TempDir,
    pre_dir: PathBuf,
    lease: PairOwnershipLease,
    authority: LocalBlindPairAuthority,
}

impl Fixture {
    fn new() -> Self {
        let root = tempfile::tempdir().unwrap();
        let pre_dir = root.path().join("pre-project").join("pre-a");
        std::fs::create_dir_all(&pre_dir).unwrap();
        let post_dir = root.path().join("post-project").join("post-a");
        let lease = PairOwnershipLease::new();
        let pair_claimed_at = 7.0;
        lease
            .commit_claimed_binding_if(
                root.path(),
                Some(&post_dir),
                Some("pre-a"),
                "post-project",
                "post-a",
                pair_claimed_at,
                || true,
                || Some(()),
            )
            .unwrap()
            .expect("exact claim");
        let claim = crate::read_pair_claim(root.path(), std::process::id(), "pre-a").unwrap();
        assert_eq!(claim.schema, PAIR_CLAIM_SCHEMA);
        Self {
            authority: LocalBlindPairAuthority {
                pair_generation: 11,
                pair_owner_id: lease.owner_id().to_string(),
                pair_claimed_at_bits: pair_claimed_at.to_bits(),
                host_process_id: std::process::id(),
                post_project_hash: "post-project".into(),
                post_instance_id: "post-a".into(),
                pre_project_hash: "pre-project".into(),
                pre_instance_id: "pre-a".into(),
            },
            root,
            pre_dir,
            lease,
        }
    }

    fn request(&self) -> LocalBlindCaptureRequest {
        LocalBlindCaptureRequest::new(
            self.authority.clone(),
            22,
            33,
            1,
            0,
            48_000,
            2,
            0,
            192_000,
            1_000,
            10_000,
        )
        .expect("valid request")
    }

    fn target(&self) -> CaptureTarget<'_> {
        CaptureTarget {
            kirin_root: self.root.path(),
            instance_dir: &self.pre_dir,
            pre_project_hash: "pre-project",
            pre_instance_id: "pre-a",
            sample_rate: 48_000,
            channels: 2,
        }
    }
}

#[test]
fn exact_pair_request_and_armed_echo_round_trip_without_names_or_host_context() {
    let fixture = Fixture::new();
    let request = fixture.request();
    publish_local_blind_capture_request(fixture.root.path(), &fixture.pre_dir, &request).unwrap();
    let read = read_validated_local_blind_capture_request(
        fixture.root.path(),
        &fixture.pre_dir,
        "pre-project",
        "pre-a",
        48_000,
        2,
        1_001,
    )
    .expect("validated request");
    assert_eq!(read, request);
    let encoded = serde_json::to_string(&read).unwrap();
    assert!(!encoded.contains("pair_name"));
    assert!(!encoded.contains("document"));
    assert!(!encoded.contains("channel_id"));

    publish_local_blind_capture_armed(fixture.root.path(), &fixture.pre_dir, &read, 1_002).unwrap();
    let armed = read_matching_local_blind_capture_armed(
        fixture.root.path(),
        &fixture.pre_dir,
        &request,
        1_003,
    )
    .expect("matching armed echo");
    assert_eq!(armed.capture_generation, 22);
    assert_eq!(armed.clock_generation, 33);
    assert_eq!(armed.request_sha256.len(), 64);

    assert!(read_validated_local_blind_capture_request(
        fixture.root.path(),
        &fixture.pre_dir,
        "pre-project",
        "pre-a",
        48_000,
        2,
        11_001,
    )
    .is_none());
    assert_eq!(
        read_validated_local_blind_capture_request_for_active_result(
            fixture.root.path(),
            &fixture.pre_dir,
            "pre-project",
            "pre-a",
            48_000,
            2,
            11_001,
        ),
        Some(request)
    );
    assert!(active_capture_result_was_armed(
        fixture.root.path(),
        &fixture.pre_dir,
        &read,
        11_001,
    ));
}

#[test]
fn released_pair_invalidates_an_already_published_request() {
    let fixture = Fixture::new();
    let request = fixture.request();
    publish_local_blind_capture_request(fixture.root.path(), &fixture.pre_dir, &request).unwrap();
    publish_local_blind_capture_armed(fixture.root.path(), &fixture.pre_dir, &request, 1_001)
        .unwrap();
    fixture
        .lease
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
    assert!(read_validated_local_blind_capture_request(
        fixture.root.path(),
        &fixture.pre_dir,
        "pre-project",
        "pre-a",
        48_000,
        2,
        1_001,
    )
    .is_none());
    assert!(publish_local_blind_capture_armed(
        fixture.root.path(),
        &fixture.pre_dir,
        &request,
        1_002,
    )
    .is_err());
    assert!(read_matching_local_blind_capture_armed(
        fixture.root.path(),
        &fixture.pre_dir,
        &request,
        1_002,
    )
    .is_none());
    assert!(!active_capture_result_was_armed(
        fixture.root.path(),
        &fixture.pre_dir,
        &request,
        11_001,
    ));
}

#[test]
fn malformed_expired_and_misdirected_requests_fail_closed() {
    let fixture = Fixture::new();
    let valid = fixture.request();
    for variant in 0..10 {
        let mut request = valid.clone();
        match variant {
            0 => request.authority.pair_generation = 0,
            1 => request.capture_generation = 0,
            2 => request.clock_generation = 0,
            3 => request.clock_source = 0,
            4 => request.clock_position_at_issue = 1,
            5 => request.channels = 6,
            6 => request.frames = 192_001,
            7 => request.native_start = i64::MAX,
            8 => request.authority.pre_instance_id = "other-pre".into(),
            9 => request.expires_at_unix_ms = 1_000,
            _ => unreachable!(),
        }
        assert!(!request.valid_for_target(fixture.target(), 1_001));
    }
    assert!(LocalBlindCaptureRequest::new(
        fixture.authority,
        22,
        33,
        1,
        0,
        48_000,
        2,
        0,
        1,
        1_000,
        LOCAL_BLIND_CAPTURE_LEASE_MS + 1,
    )
    .is_none());
}

#[test]
fn armed_echo_is_bound_to_every_request_generation_and_digest() {
    let fixture = Fixture::new();
    let request = fixture.request();
    let armed = LocalBlindCaptureArmed::for_request(&request, 1_001).unwrap();
    for variant in 0..5 {
        let mut changed = request.clone();
        match variant {
            0 => changed.request_id = Uuid::new_v4().to_string(),
            1 => changed.capture_generation += 1,
            2 => changed.clock_generation += 1,
            3 => changed.native_start += 1,
            4 => changed.clock_position_at_issue -= 1,
            _ => unreachable!(),
        }
        assert!(!armed.matches_request(&changed, 1_001));
    }
}
