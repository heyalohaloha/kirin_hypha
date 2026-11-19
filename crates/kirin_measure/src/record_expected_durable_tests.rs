//! Durable session, generation-correspondence and metadata-free markers.
//! A failed sync remains best effort; a failed publication remains an error.
use super::*;
use crate::atomic_file::durable::test_hook::syncs_during;

fn assert_syncs(events: &[(&str, PathBuf)], targets: &[(&Path, bool)]) {
    let files: Vec<_> = events
        .iter()
        .enumerate()
        .filter(|(_, (kind, _))| *kind == "file")
        .collect();
    assert_eq!(files.len(), targets.len(), "{events:?}");
    for ((index, (_, staged)), (target, published)) in files.into_iter().zip(targets) {
        assert_eq!(staged.parent(), target.parent());
        let prefix = format!(".{}.tmp.", target.file_name().unwrap().to_str().unwrap());
        assert!(
            staged
                .file_name()
                .unwrap()
                .to_str()
                .unwrap()
                .starts_with(&prefix),
            "wrong publication or order: {staged:?} -> {target:?}"
        );
        assert!(!staged.exists(), "staging file escaped cleanup: {staged:?}");
        #[cfg(unix)]
        if *published {
            assert_eq!(
                events
                    .get(index + 1)
                    .map(|(kind, path)| (*kind, path.as_path())),
                Some(("directory", target.parent().unwrap())),
                "{events:?}"
            );
        }
        #[cfg(not(unix))]
        let _ = (index, published);
    }
}

fn assert_marker(marker: &ExpectedWavClaimMarker, metadata: Option<&ExpectedWavMetadata>) {
    assert_eq!(marker.schema_version, EXPECTED_CLAIM_SCHEMA);
    assert_eq!(marker.session_id, "session");
    assert_eq!(marker.requested_by_post_instance_id, "post");
    assert_eq!(marker.capture_generation_id, "generation");
    assert_eq!(marker.generation_started_at_ms, 123);
    assert_eq!(marker.metadata.as_ref(), metadata);
    match metadata {
        Some(metadata) => {
            assert_eq!(marker.bounce_id, metadata.bounce_id);
            assert_eq!(marker.created_at_ms, metadata.created_at_ms);
            assert_eq!(marker.wav_hash, metadata.wav_hash.as_deref().unwrap());
        }
        None => {
            assert!(marker.bounce_id.is_empty());
            assert_eq!(marker.created_at_ms, 0);
            assert!(marker.wav_hash.is_empty());
        }
    }
}

fn read_marker(path: &Path) -> ExpectedWavClaimMarker {
    serde_json::from_slice(&fs::read(path).unwrap()).unwrap()
}

#[test]
fn empty_and_bound_markers_sync_in_order_even_when_sync_fails() {
    for fail_sync in [false, true] {
        let root = tempfile::tempdir().unwrap();
        let base = root.path();
        let metadata = metadata_fixture("bounce");
        write_expected_metadata(base, "project", &metadata).unwrap();
        let current = expected_path(base, "project");
        let current_bytes = fs::read(&current).unwrap();
        let session = expected_session_claim_marker_path(base, "project", "session");
        let matching = expected_claim_marker_path(base, "project", &metadata, "session");
        let empty_events = syncs_during(fail_sync, || {
            begin_expected_session_for_generation(
                base,
                "project",
                "session",
                "post",
                "generation",
                123,
            )
            .unwrap();
        });
        assert_syncs(&empty_events, &[(&session, true)]);
        let empty = read_marker(&session);
        assert_marker(&empty, None);
        assert!(empty.closed_at_ms.is_none());
        assert!(!matching.exists());
        assert_eq!(fs::read(&current).unwrap(), current_bytes);

        let events = syncs_during(fail_sync, || {
            assert!(
                mark_expected_metadata_consumed(base, "project", Some("bounce"), "session")
                    .unwrap()
            );
        });
        assert_syncs(&events, &[(&session, true), (&matching, true)]);
        let bound = read_marker(&session);
        assert_marker(&bound, Some(&metadata));
        assert_eq!(bound.claimed_at_ms, empty.claimed_at_ms);
        assert!(bound.closed_at_ms.is_some());
        assert_eq!(fs::read(&session).unwrap(), fs::read(&matching).unwrap());
        assert_eq!(fs::read(current).unwrap(), current_bytes);
    }
}

#[test]
fn first_marker_rename_failure_does_not_publish_correspondence_and_can_retry() {
    let root = tempfile::tempdir().unwrap();
    let base = root.path();
    let metadata = metadata_fixture("bounce");
    write_expected_metadata(base, "project", &metadata).unwrap();
    let current = expected_path(base, "project");
    let current_bytes = fs::read(&current).unwrap();
    let session = expected_session_claim_marker_path(base, "project", "session");
    let matching = expected_claim_marker_path(base, "project", &metadata, "session");
    fs::create_dir_all(&session).unwrap();
    let sentinel = session.join("untouched");
    fs::write(&sentinel, b"fixture").unwrap();
    let publish = || {
        write_claim_marker_for_generation(
            base,
            "project",
            &metadata,
            "session",
            200,
            Some(300),
            "post",
            "generation",
            123,
        )
    };
    let events = syncs_during(false, || assert!(publish().is_err()));
    assert_syncs(&events, &[(&session, false)]);
    assert!(!matching.exists());
    assert_eq!(fs::read(&sentinel).unwrap(), b"fixture");
    assert_eq!(fs::read(&current).unwrap(), current_bytes);

    fs::remove_file(sentinel).unwrap();
    fs::remove_dir(&session).unwrap();
    let events = syncs_during(false, || publish().unwrap());
    assert_syncs(&events, &[(&session, true), (&matching, true)]);
    assert_marker(&read_marker(&session), Some(&metadata));
    assert_eq!(fs::read(session).unwrap(), fs::read(matching).unwrap());
    assert_eq!(fs::read(current).unwrap(), current_bytes);
}

#[test]
fn second_marker_rename_failure_retains_session_fact_and_retry_reconciles_both() {
    let root = tempfile::tempdir().unwrap();
    let base = root.path();
    let metadata = metadata_fixture("bounce");
    write_expected_metadata(base, "project", &metadata).unwrap();
    begin_expected_session_for_generation(base, "project", "session", "post", "generation", 123)
        .unwrap();
    let session = expected_session_claim_marker_path(base, "project", "session");
    let matching = expected_claim_marker_path(base, "project", &metadata, "session");
    let empty = read_marker(&session);
    let current_bytes = fs::read(expected_path(base, "project")).unwrap();
    fs::create_dir_all(&matching).unwrap();
    let sentinel = matching.join("untouched");
    fs::write(&sentinel, b"fixture").unwrap();
    let events = syncs_during(false, || {
        assert!(
            mark_expected_metadata_consumed(base, "project", Some("bounce"), "session").is_err()
        );
    });
    assert_syncs(&events, &[(&session, true), (&matching, false)]);
    let published_bytes = fs::read(&session).unwrap();
    let published = read_marker(&session);
    assert_marker(&published, Some(&metadata));
    assert_eq!(published.claimed_at_ms, empty.claimed_at_ms);
    assert!(published.closed_at_ms.is_some());
    assert_eq!(fs::read(&sentinel).unwrap(), b"fixture");
    assert_eq!(
        fs::read(expected_path(base, "project")).unwrap(),
        current_bytes
    );

    fs::remove_file(sentinel).unwrap();
    fs::remove_dir(&matching).unwrap();
    let events = syncs_during(false, || {
        assert!(
            mark_expected_metadata_consumed(base, "project", Some("bounce"), "session").unwrap()
        );
    });
    assert_syncs(&events, &[(&session, true), (&matching, true)]);
    assert_eq!(fs::read(&session).unwrap(), published_bytes);
    assert_eq!(fs::read(&matching).unwrap(), published_bytes);
    assert_eq!(
        fs::read(expected_path(base, "project")).unwrap(),
        current_bytes
    );
}

#[test]
fn empty_marker_rename_failure_is_reported_and_retry_stays_metadata_free() {
    let root = tempfile::tempdir().unwrap();
    let base = root.path();
    let metadata = metadata_fixture("previous-bounce");
    write_expected_metadata(base, "project", &metadata).unwrap();
    let current = expected_path(base, "project");
    let current_bytes = fs::read(&current).unwrap();
    let session = expected_session_claim_marker_path(base, "project", "session");
    fs::create_dir_all(&session).unwrap();
    let sentinel = session.join("untouched");
    fs::write(&sentinel, b"fixture").unwrap();
    let publish = || {
        write_empty_claim_marker_for_generation(
            base,
            "project",
            "session",
            "post",
            200,
            None,
            "generation",
            123,
        )
    };
    let events = syncs_during(false, || assert!(publish().is_err()));
    assert_syncs(&events, &[(&session, false)]);
    assert_eq!(fs::read(&sentinel).unwrap(), b"fixture");
    assert_eq!(fs::read(&current).unwrap(), current_bytes);

    fs::remove_file(sentinel).unwrap();
    fs::remove_dir(&session).unwrap();
    let events = syncs_during(false, || publish().unwrap());
    assert_syncs(&events, &[(&session, true)]);
    let empty = read_marker(&session);
    assert_marker(&empty, None);
    assert_eq!(empty.claimed_at_ms, 200);
    assert!(empty.closed_at_ms.is_none());
    assert!(!expected_claim_marker_path(base, "project", &metadata, "session").exists());
    assert_eq!(fs::read(current).unwrap(), current_bytes);
}
