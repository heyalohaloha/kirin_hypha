//! HYP-233: observe the exact preparation/active publications, not just their shared parent.
//! All storage is synthetic; sync failure is best effort, unlike a failed rename.
use super::*;
use crate::atomic_file::durable::test_hook::syncs_during;
use crate::capture_generation::{read_archived_generation, read_stop_target_generation};
use std::fs;

fn generation() -> CaptureGeneration {
    CaptureGeneration::new_single(
        "project".into(),
        "post".into(),
        "pre".into(),
        "daw".into(),
        std::process::id(),
    )
}

fn ready_writers(
    base: &Path,
    generation: &CaptureGeneration,
) -> Vec<crate::record_writer_claim::WriterClaimGuard> {
    let member = &generation.members[0];
    [(Role::Pre, "pre"), (Role::Post, "post")]
        .into_iter()
        .map(|(role, instance)| {
            let mut writer = crate::record_writer_claim::claim_writer_for_generation(
                base,
                "project",
                &member.record_session_id,
                role,
                instance,
                &generation.capture_generation_id,
                generation.started_at_ms,
            )
            .unwrap();
            writer.mark_ready().unwrap();
            writer
        })
        .collect()
}

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

fn seed_previous(base: &Path) -> (CaptureGeneration, Vec<u8>) {
    let previous = generation();
    let bytes = serde_json::to_vec(&previous).unwrap();
    crate::atomic_file::write_bytes_atomic(&active_generation_path(base), &bytes).unwrap();
    publish_current_generation(base, "project", &previous).unwrap();
    (previous, bytes)
}

#[test]
fn preparing_and_active_sync_exact_targets_and_preserve_promotion_order() {
    for fail_sync in [false, true] {
        let root = tempfile::tempdir().unwrap();
        let base = root.path();
        let (_, previous_bytes) = seed_previous(base);
        let next = generation();
        let current = current_generation_path(base, "project");
        let preparing = preparing_generation_path(base);
        let active = active_generation_path(base);
        let mut tx = CaptureGenerationTransaction::begin(base, &next).unwrap();
        let stage_events = syncs_during(fail_sync, || tx.stage().unwrap());
        assert_syncs(&stage_events, &[(&current, true), (&preparing, true)]);
        assert_eq!(
            read_current_generation(base, "project").unwrap(),
            Some(next.clone())
        );
        assert_eq!(read_preparing_generation(base).unwrap(), Some(next.clone()));
        assert_eq!(fs::read(&active).unwrap(), previous_bytes);
        assert_eq!(
            read_stop_target_generation(base, "project", "post").unwrap(),
            Some(next.clone())
        );

        let _writers = ready_writers(base, &next);
        let mut observed_promotion = false;
        let commit_events = syncs_during(fail_sync, || {
            tx.commit_observing_active(|| {
                observed_promotion = true;
                assert_eq!(read_active_generation(base).unwrap(), Some(next.clone()));
                assert_eq!(read_preparing_generation(base).unwrap(), Some(next.clone()));
                assert_eq!(
                    read_stop_target_generation(base, "project", "post").unwrap(),
                    Some(next.clone())
                );
            })
            .unwrap();
        });
        assert!(observed_promotion);
        assert_syncs(&commit_events, &[(&active, true)]);
        assert!(tx.committed);
        assert!(!preparing.exists());
        assert_eq!(read_active_generation(base).unwrap(), Some(next.clone()));
        assert_eq!(
            read_archived_generation(base, &next.capture_generation_id, next.started_at_ms)
                .unwrap(),
            Some(next)
        );
    }
}

#[test]
fn preparing_rename_failure_retains_old_active_and_rolls_back_project_pointer() {
    let root = tempfile::tempdir().unwrap();
    let base = root.path();
    let (previous, previous_bytes) = seed_previous(base);
    let next = generation();
    let current = current_generation_path(base, "project");
    let preparing = preparing_generation_path(base);
    fs::create_dir(&preparing).unwrap();
    let sentinel = preparing.join("untouched");
    fs::write(&sentinel, b"fixture").unwrap();
    let mut tx = CaptureGenerationTransaction::begin(base, &next).unwrap();
    let events = syncs_during(false, || assert!(tx.stage().is_err()));
    assert_syncs(&events, &[(&current, true), (&preparing, false)]);
    assert!(!tx.committed);
    assert_eq!(
        fs::read(active_generation_path(base)).unwrap(),
        previous_bytes
    );
    assert!(read_preparing_generation(base).is_err());
    assert!(read_stop_target_generation(base, "project", "post").is_err());
    drop(tx);
    assert_eq!(
        read_current_generation(base, "project").unwrap(),
        Some(previous)
    );
    assert_eq!(
        fs::read(active_generation_path(base)).unwrap(),
        previous_bytes
    );
    assert_eq!(fs::read(&sentinel).unwrap(), b"fixture");

    // A failed transaction is terminal. Recovery uses a fresh identity, not a reused abort.
    fs::remove_file(sentinel).unwrap();
    fs::remove_dir(preparing).unwrap();
    let retry = generation();
    let mut tx = CaptureGenerationTransaction::begin(base, &retry).unwrap();
    tx.stage().unwrap();
    let _writers = ready_writers(base, &retry);
    tx.commit().unwrap();
    assert_eq!(read_active_generation(base).unwrap(), Some(retry));
}

#[test]
fn active_rename_failure_keeps_preparing_until_retry_and_does_not_claim_commit() {
    let root = tempfile::tempdir().unwrap();
    let base = root.path();
    let next = generation();
    let mut tx = CaptureGenerationTransaction::begin(base, &next).unwrap();
    tx.stage().unwrap();
    let _writers = ready_writers(base, &next);
    let active = active_generation_path(base);
    // No previous active pointer in this fixture. Block only its final rename.
    fs::create_dir(&active).unwrap();
    let sentinel = active.join("untouched");
    fs::write(&sentinel, b"fixture").unwrap();
    let mut observed_promotion = false;
    let events = syncs_during(false, || {
        assert!(tx
            .commit_observing_active(|| observed_promotion = true)
            .is_err());
    });
    assert_syncs(&events, &[(&active, false)]);
    assert!(!observed_promotion);
    assert!(!tx.committed);
    assert_eq!(read_preparing_generation(base).unwrap(), Some(next.clone()));
    assert_eq!(
        read_current_generation(base, "project").unwrap(),
        Some(next.clone())
    );
    assert_eq!(
        read_stop_target_generation(base, "project", "post").unwrap(),
        Some(next.clone())
    );
    assert_eq!(fs::read(&sentinel).unwrap(), b"fixture");
    assert_eq!(
        read_archived_generation(base, &next.capture_generation_id, next.started_at_ms).unwrap(),
        Some(next.clone())
    );

    fs::remove_file(sentinel).unwrap();
    fs::remove_dir(&active).unwrap();
    let events = syncs_during(false, || tx.commit().unwrap());
    assert_syncs(&events, &[(&active, true)]);
    assert!(tx.committed);
    assert!(!preparing_generation_path(base).exists());
    assert_eq!(read_active_generation(base).unwrap(), Some(next));
}

#[test]
fn dropping_after_active_failure_removes_uncommitted_preparation_and_project_pointer() {
    let root = tempfile::tempdir().unwrap();
    let base = root.path();
    let next = generation();
    let mut tx = CaptureGenerationTransaction::begin(base, &next).unwrap();
    tx.stage().unwrap();
    let _writers = ready_writers(base, &next);
    let active = active_generation_path(base);
    fs::create_dir(&active).unwrap();
    assert!(tx.commit().is_err());
    drop(tx);
    assert!(active.is_dir());
    assert!(!preparing_generation_path(base).exists());
    assert!(read_current_generation(base, "project").unwrap().is_none());
    let terminal = crate::capture_generation_lifecycle::read_generation_terminal_recovering(
        base,
        &next.capture_generation_id,
        next.started_at_ms,
    )
    .unwrap()
    .unwrap();
    assert_eq!(
        terminal.terminal_reason,
        crate::capture_generation_lifecycle::GenerationTerminalReason::Aborted
    );
}
