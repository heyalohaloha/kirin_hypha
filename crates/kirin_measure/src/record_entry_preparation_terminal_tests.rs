use super::*;
use crate::atomic_file::durable::test_hook::syncs_during;
use crate::capture_generation::{CaptureGeneration, CaptureGenerationMember};
use crate::capture_generation_lifecycle::{mark_generation_terminal, GenerationTerminalReason};

struct Fixture {
    root: tempfile::TempDir,
    generation: CaptureGeneration,
    signal: crate::record_signal::RecordSignal,
}

impl Fixture {
    fn new() -> Self {
        let root = tempfile::tempdir().unwrap();
        let generation = CaptureGeneration::new_single(
            "project".into(),
            "post".into(),
            "pre".into(),
            "daw".into(),
            std::process::id(),
        );
        crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
            root.path(),
            "project",
            "post",
            "pre".into(),
            "daw".into(),
            None,
            &generation,
        )
        .unwrap();
        let signal = crate::record_signal::read_signal(root.path(), "project", "post").unwrap();
        claim_record_entry(
            root.path(),
            "project",
            &generation.members[0].record_session_id,
            Role::Post,
            "post",
        )
        .unwrap();
        Self {
            root,
            generation,
            signal,
        }
    }
    fn member(&self) -> &CaptureGenerationMember {
        &self.generation.members[0]
    }
    fn path(&self) -> PathBuf {
        entry_lock_path(
            self.root.path(),
            "project",
            &self.member().record_session_id,
            Role::Post,
            "post",
        )
    }
    fn publish(&self, samples: u64) {
        publish_measure_preparation(
            self.root.path(),
            "project",
            Role::Post,
            "post",
            &self.signal,
            2,
            samples,
        )
        .unwrap();
    }
    fn samples(&self) -> usize {
        measure_preparation_samples(
            self.root.path(),
            &self.generation,
            self.member(),
            Role::Post,
        )
    }
    fn sync_count(&self, action: impl FnOnce()) -> usize {
        syncs_during(false, action)
            .iter()
            .filter(|(kind, path)| *kind == "file" && path.parent() == self.path().parent())
            .count()
    }
}

#[test]
fn actual_ready_endpoint_syncs_last_samples_once_and_blocks_late_progress() {
    let f = Fixture::new();
    assert_eq!(f.sync_count(|| f.publish(9_600)), 0);
    let mut writer = crate::record_writer_claim::claim_writer_for_generation(
        f.root.path(),
        "project",
        &f.member().record_session_id,
        Role::Post,
        "post",
        &f.generation.capture_generation_id,
        f.generation.started_at_ms,
    )
    .unwrap();
    assert_eq!(f.sync_count(|| writer.mark_ready().unwrap()), 1);
    let before = fs::read(f.path()).unwrap();
    assert_eq!(f.sync_count(|| writer.mark_ready().unwrap()), 0);
    assert_eq!(f.sync_count(|| f.publish(19_200)), 0);
    assert_eq!(fs::read(f.path()).unwrap(), before);
    assert_eq!(f.samples(), 9_600);
}

#[test]
fn actual_stop_drop_abort_endpoints_sync_once_without_new_schema() {
    for reason in [
        GenerationTerminalReason::AllStop,
        GenerationTerminalReason::DropCommitted,
        GenerationTerminalReason::Aborted,
    ] {
        let f = Fixture::new();
        f.publish(19_200);
        let before = fs::read(f.path()).unwrap();
        assert_eq!(
            f.sync_count(|| {
                mark_generation_terminal(f.root.path(), &f.generation, reason).unwrap();
            }),
            1
        );
        assert_eq!(fs::read(f.path()).unwrap(), before);
        assert_eq!(
            f.sync_count(|| {
                crate::capture_generation_lifecycle::mark_generation_terminal_by_identity(
                    f.root.path(),
                    &f.generation.capture_generation_id,
                    f.generation.started_at_ms,
                    reason,
                )
                .unwrap();
                f.publish(28_800);
            }),
            0
        );
        assert_eq!(fs::read(f.path()).unwrap(), before);
    }
}

#[test]
fn terminal_write_failure_retries_but_never_allows_plain_progress() {
    let f = Fixture::new();
    f.publish(9_600);
    let before = fs::read(f.path()).unwrap();
    fs::remove_file(f.path()).unwrap();
    fs::create_dir(f.path()).unwrap();
    fs::write(f.path().join("block"), b"sentinel").unwrap();
    mark_generation_terminal(
        f.root.path(),
        &f.generation,
        GenerationTerminalReason::Aborted,
    )
    .unwrap();
    assert_eq!(fs::read(f.path().join("block")).unwrap(), b"sentinel");
    fs::remove_dir_all(f.path()).unwrap();
    fs::write(f.path(), &before).unwrap();
    // Retry via the real progress endpoint: it may finish the old samples, never publish more.
    assert_eq!(f.sync_count(|| f.publish(19_200)), 1);
    assert_eq!(fs::read(f.path()).unwrap(), before);
    assert_eq!(f.sync_count(|| f.publish(28_800)), 0);
}

#[test]
fn foreign_owner_pid_identity_or_generation_never_receives_terminal_write() {
    for field in 0..5 {
        let f = Fixture::new();
        f.publish(9_600);
        let mut value: RecordEntryLockFile =
            serde_json::from_slice(&fs::read(f.path()).unwrap()).unwrap();
        match field {
            0 => value.owner_id = "other".into(),
            1 => value.host_process_id = std::process::id().saturating_add(1),
            2 => value.record_session_id = "other".into(),
            3 => {
                value
                    .measure_preparation
                    .as_mut()
                    .unwrap()
                    .capture_generation_id = "other".into()
            }
            _ => {
                value
                    .measure_preparation
                    .as_mut()
                    .unwrap()
                    .record_generation += 1
            }
        }
        fs::write(f.path(), serde_json::to_vec(&value).unwrap()).unwrap();
        let before = fs::read(f.path()).unwrap();
        assert_eq!(
            f.sync_count(|| {
                mark_generation_terminal(
                    f.root.path(),
                    &f.generation,
                    GenerationTerminalReason::AllStop,
                )
                .unwrap();
            }),
            0
        );
        assert_eq!(fs::read(f.path()).unwrap(), before);
    }
}

#[test]
fn terminal_before_first_progress_and_abandonment_do_not_create_samples() {
    let f = Fixture::new();
    let before = fs::read(f.path()).unwrap();
    mark_generation_terminal(
        f.root.path(),
        &f.generation,
        GenerationTerminalReason::AllStop,
    )
    .unwrap();
    assert_eq!(f.sync_count(|| f.publish(9_600)), 0);
    assert_eq!(fs::read(f.path()).unwrap(), before);
    let abandoned = Fixture::new();
    abandoned.publish(9_600);
    // Simulated loss of local owner bookkeeping is not an invitation to finalize another owner.
    terminal::owners().unwrap().remove(&abandoned.path());
    let before = fs::read(abandoned.path()).unwrap();
    assert_eq!(
        abandoned.sync_count(|| {
            mark_generation_terminal(
                abandoned.root.path(),
                &abandoned.generation,
                GenerationTerminalReason::Aborted,
            )
            .unwrap();
            abandoned.publish(19_200);
        }),
        0
    );
    assert_eq!(fs::read(abandoned.path()).unwrap(), before);
}

#[test]
fn late_io_thread_progress_after_terminal_is_suppressed_without_polling() {
    let f = Fixture::new();
    f.publish(9_600);
    let (send, receive) = std::sync::mpsc::channel();
    std::thread::scope(|scope| {
        let fixture = &f;
        let worker = scope.spawn(move || {
            receive.recv().unwrap();
            fixture.publish(19_200);
        });
        mark_generation_terminal(
            f.root.path(),
            &f.generation,
            GenerationTerminalReason::AllStop,
        )
        .unwrap();
        send.send(()).unwrap();
        worker.join().unwrap();
    });
    assert_eq!(f.samples(), 9_600);
}

#[test]
fn sync_failure_preserves_existing_best_effort_contract_and_one_attempt() {
    let f = Fixture::new();
    f.publish(9_600);
    let before = fs::read(f.path()).unwrap();
    let events = syncs_during(true, || {
        mark_generation_terminal(
            f.root.path(),
            &f.generation,
            GenerationTerminalReason::Aborted,
        )
        .unwrap();
    });
    assert_eq!(
        events
            .iter()
            .filter(|(kind, path)| *kind == "file" && path.parent() == f.path().parent())
            .count(),
        1
    );
    assert_eq!(fs::read(f.path()).unwrap(), before);
    assert_eq!(f.sync_count(|| f.publish(19_200)), 0);
}

#[test]
fn in_flight_plain_publication_finishes_before_terminal_sync() {
    let f = Fixture::new();
    f.publish(9_600);
    let pause = crate::atomic_file::AtomicWritePause::install(f.path());
    std::thread::scope(|scope| {
        let progress = scope.spawn(|| f.publish(19_200));
        pause.wait_until_entered();
        let (send, receive) = std::sync::mpsc::channel();
        let fixture = &f;
        let terminal = scope.spawn(move || {
            send.send(()).unwrap();
            mark_generation_terminal(
                fixture.root.path(),
                &fixture.generation,
                GenerationTerminalReason::AllStop,
            )
            .unwrap();
        });
        receive.recv().unwrap();
        pause.release();
        progress.join().unwrap();
        terminal.join().unwrap();
    });
    assert_eq!(f.samples(), 19_200);
    assert_eq!(f.sync_count(|| f.publish(28_800)), 0);
    assert_eq!(f.samples(), 19_200);
}

#[test]
fn stalled_entry_does_not_block_other_claim_or_stop() {
    let a = Fixture::new();
    a.publish(9_600);
    let pause = crate::atomic_file::AtomicWritePause::install(a.path());
    std::thread::scope(|scope| {
        let progress = scope.spawn(|| a.publish(19_200));
        pause.wait_until_entered();
        let (send, receive) = std::sync::mpsc::channel();
        let a = &a;
        let worker = scope.spawn(move || {
            let b = Fixture::new();
            b.publish(9_600);
            mark_generation_terminal(
                b.root.path(),
                &b.generation,
                GenerationTerminalReason::AllStop,
            )
            .unwrap();
            mark_generation_terminal(
                a.root.path(),
                &a.generation,
                GenerationTerminalReason::AllStop,
            )
            .unwrap();
            send.send(()).unwrap();
        });
        let completed_before_release = receive.recv_timeout(std::time::Duration::from_secs(5));
        // Always unblock before asserting: the regression must fail, not hang a scoped join.
        pause.release();
        progress.join().unwrap();
        worker.join().unwrap();
        assert!(
            completed_before_release.is_ok(),
            "another entry and Stop must finish while IO remains stalled"
        );
    });
    assert_eq!(a.samples(), 19_200);
    assert!(!terminal::owners().unwrap().contains_key(&a.path()));
    assert_eq!(a.sync_count(|| a.publish(28_800)), 0);
    assert_eq!(a.samples(), 19_200);
}

#[test]
fn completed_entries_retire_instead_of_accumulating() {
    for _ in 0..100 {
        let f = Fixture::new();
        f.publish(9_600);
        mark_generation_terminal(
            f.root.path(),
            &f.generation,
            GenerationTerminalReason::AllStop,
        )
        .unwrap();
        assert!(!terminal::owners().unwrap().contains_key(&f.path()));
        assert_eq!(f.sync_count(|| f.publish(19_200)), 0);
        assert_eq!(f.samples(), 9_600);
    }
}

#[test]
fn old_work_cannot_release_or_retire_replacement_owner() {
    let f = Fixture::new();
    f.publish(9_600);
    let old = terminal::acquire(&f.path(), None).unwrap().unwrap();
    let mut replacement: RecordEntryLockFile =
        serde_json::from_slice(&fs::read(f.path()).unwrap()).unwrap();
    replacement.owner_id = "new-local-owner".into();
    terminal::register(
        &mut terminal::owners().unwrap(),
        f.root.path(),
        f.path(),
        replacement,
    );
    let next = terminal::acquire(&f.path(), None).unwrap().unwrap();
    drop(old);
    assert!(
        terminal::acquire(&f.path(), None).unwrap().is_none(),
        "old token cannot release new owner's IO"
    );
    drop(next);
    terminal::owners().unwrap().remove(&f.path());
}

#[test]
fn stalled_terminal_sync_does_not_hold_the_owners_table() {
    let a = Fixture::new();
    a.publish(9_600);
    let pause = crate::atomic_file::AtomicWritePause::install(a.path());
    std::thread::scope(|scope| {
        let terminal = scope.spawn(|| {
            mark_generation_terminal(
                a.root.path(),
                &a.generation,
                GenerationTerminalReason::AllStop,
            )
            .unwrap();
        });
        pause.wait_until_entered();
        let (send, receive) = std::sync::mpsc::channel();
        let a = &a;
        let independent = scope.spawn(move || {
            let b = Fixture::new();
            b.publish(9_600);
            mark_generation_terminal(
                b.root.path(),
                &b.generation,
                GenerationTerminalReason::AllStop,
            )
            .unwrap();
            // A repeated Stop must not wait for the already-running terminal writer either.
            terminal::finish_terminal_preparations(
                a.root.path(),
                &a.generation.capture_generation_id,
                a.generation.started_at_ms,
            );
            send.send(()).unwrap();
        });
        let completed = receive.recv_timeout(std::time::Duration::from_secs(5));
        pause.release();
        terminal.join().unwrap();
        independent.join().unwrap();
        assert!(
            completed.is_ok(),
            "terminal IO cannot hold unrelated claims or repeated Stop"
        );
    });
    assert_eq!(a.samples(), 9_600);
    assert!(!terminal::owners().unwrap().contains_key(&a.path()));
}
