//! Low-frequency coordination publications only; no real storage or audio worker.
use super::*;
use crate::atomic_file::durable::test_hook::syncs_during;
use crate::capture_generation::{publish_current_generation, CaptureGeneration};
use crate::capture_generation_lifecycle::{mark_generation_terminal, GenerationTerminalReason};
use crate::record_signal::{self, RecordSignal};
use std::path::PathBuf;

fn generation() -> CaptureGeneration {
    CaptureGeneration::new_single(
        "project".into(),
        "post".into(),
        "pre".into(),
        "daw".into(),
        std::process::id(),
    )
}
fn assert_publications(events: &[(&str, PathBuf)], targets: &[PathBuf]) {
    // File sync observes unique staging names, never the final name. Both must
    // share a parent, and publication order must remain the supplied order.
    let files: Vec<_> = events
        .iter()
        .enumerate()
        .filter(|(_, (kind, _))| *kind == "file")
        .collect();
    assert_eq!(files.len(), targets.len(), "{events:?}");
    for ((index, (_, staged)), target) in files.into_iter().zip(targets) {
        assert_eq!(staged.parent(), target.parent());
        assert_ne!(staged, target);
        #[cfg(unix)]
        assert!(events[index + 1..].iter().any(|(kind, path)|
            *kind == "directory" && Some(path.as_path()) == target.parent()), "{events:?}");
        #[cfg(not(unix))]
        let _ = index;
    }
}
fn pending() -> RecordSignal {
    RecordSignal::new_pending("post".into(), "pre".into(), "daw".into(), None, None)
}

#[test]
fn coordination_publications_sync_in_order_and_sync_failure_is_best_effort() {
    for fail in [false, true] {
        let root = tempfile::tempdir().unwrap();
        let base = root.path();
        let generation = generation();
        let current = crate::capture_generation::current_generation_path(base, "project");
        let events = syncs_during(fail, || {
            publish_current_generation(base, "project", &generation).unwrap()
        });
        assert_publications(&events, std::slice::from_ref(&current));
        assert_eq!(
            serde_json::from_slice::<CaptureGeneration>(&fs::read(current).unwrap())
                .unwrap()
                .capture_generation_id,
            generation.capture_generation_id
        );

        let events = syncs_during(fail, || {
            write_broadcast_for_generation(
                base,
                "project",
                "post",
                "daw".into(),
                std::process::id(),
                &generation,
            )
            .unwrap();
        });
        let broadcast = signal_path(base, "project", "post");
        let pointer = current_broadcast_path(base, "project");
        assert_publications(&events, &[broadcast.clone(), pointer.clone()]);
        assert_eq!(fs::read(broadcast).unwrap(), fs::read(pointer).unwrap());

        let signal = pending();
        let events = syncs_during(fail, || {
            record_signal::write_signal(base, "project", "post", &signal).unwrap()
        });
        let canonical = record_signal::signal_path(base, "project", "post");
        let inbox = record_signal::target_signal_path(base, "pre");
        assert_publications(&events, &[canonical, inbox]);
        assert_eq!(
            record_signal::read_signal(base, "project", "post")
                .unwrap()
                .session_id,
            signal.session_id
        );

        let terminal = crate::capture_generation_lifecycle::generation_terminal_path(
            base,
            &generation.capture_generation_id,
        );
        let events = syncs_during(fail, || {
            mark_generation_terminal(base, &generation, GenerationTerminalReason::AllStop).unwrap();
        });
        assert_publications(&events, std::slice::from_ref(&terminal));
        let original = fs::read(&terminal).unwrap();
        let events = syncs_during(fail, || {
            mark_generation_terminal(base, &generation, GenerationTerminalReason::DropCommitted)
                .unwrap();
        });
        assert!(!events.iter().any(|(kind, _)| *kind == "file"));
        assert_eq!(fs::read(terminal).unwrap(), original);
    }
}

#[test]
fn record_inbox_failure_preserves_pending_rollback_contract() {
    let root = tempfile::tempdir().unwrap();
    let base = root.path();
    let inbox = record_signal::target_signal_path(base, "pre");
    fs::create_dir_all(&inbox).unwrap(); // rename onto a directory fails, not a sync failure
    let events = syncs_during(false, || {
        assert!(record_signal::write_signal(base, "project", "post", &pending()).is_err());
    });
    assert!(!record_signal::signal_path(base, "project", "post").exists());
    assert!(inbox.is_dir());
    assert!(events.iter().any(|(kind, _)| *kind == "file"));
}

#[test]
fn broadcast_second_write_failure_keeps_first_publication_and_reports_error() {
    let root = tempfile::tempdir().unwrap();
    let base = root.path();
    let pointer = current_broadcast_path(base, "project");
    fs::create_dir_all(&pointer).unwrap();
    let generation = generation();
    assert!(write_broadcast_for_generation(
        base,
        "project",
        "post",
        "daw".into(),
        std::process::id(),
        &generation
    )
    .is_err());
    let first = fs::read(signal_path(base, "project", "post")).unwrap();
    let parsed: AllKeepBroadcast = serde_json::from_slice(&first).unwrap();
    assert_eq!(
        parsed.capture_generation_id,
        generation.capture_generation_id
    );
    assert!(pointer.is_dir());
}

#[test]
fn ordinary_write_failure_does_not_replace_existing_data_and_heartbeat_stays_unsynced() {
    let root = tempfile::tempdir().unwrap();
    let blocking = root.path().join("blocked");
    fs::write(&blocking, b"unchanged").unwrap();
    assert!(publish_current_generation(&blocking, "project", &generation()).is_err());
    assert_eq!(fs::read(blocking).unwrap(), b"unchanged");
    let heartbeat = root.path().join("pre.json");
    let events = syncs_during(false, || {
        crate::atomic_file::write_bytes_atomic(&heartbeat, b"{}").unwrap();
    });
    assert!(events.is_empty());
    assert_eq!(fs::read(heartbeat).unwrap(), b"{}");
}

#[test]
fn expected_metadata_publication_syncs_and_roundtrips() {
    use crate::record_expected::{expected_path, write_expected_metadata, ExpectedWavMetadata};
    let metadata = ExpectedWavMetadata {
        expected_duration_samples: 48000,
        expected_sample_rate: 48000,
        wav_time_reference_samples: None,
        wav_path: "synthetic.wav".into(),
        bounce_id: "fixture".into(),
        created_at_ms: 1,
        wav_file_size: Some(100),
        wav_mtime_ms: 1,
        wav_hash: Some("fixture-hash".into()),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    };
    for fail in [false, true] {
        let root = tempfile::tempdir().unwrap();
        let path = expected_path(root.path(), "project");
        let events = syncs_during(fail, || {
            write_expected_metadata(root.path(), "project", &metadata).unwrap()
        });
        assert_publications(&events, std::slice::from_ref(&path));
        assert_eq!(
            serde_json::from_slice::<ExpectedWavMetadata>(&fs::read(path).unwrap()).unwrap(),
            metadata
        );
    }
}
