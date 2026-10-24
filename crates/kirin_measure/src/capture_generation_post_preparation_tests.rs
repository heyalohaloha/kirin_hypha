//! Exact POST work keeps preparation alive; idle producers still time out on a virtual clock.
use super::*;
use crate::capture_generation::CaptureGeneration;
use crate::capture_generation_tx::CaptureGenerationTransaction;
use crate::plugin_data::Role;
use crate::record_writer_claim::{claim_writer_for_generation, WriterClaimGuard};
use std::path::Path;

fn writer(base: &Path, generation: &CaptureGeneration, role: Role) -> WriterClaimGuard {
    let member = &generation.members[0];
    let instance = match role {
        Role::Pre => &member.pre_instance_id,
        Role::Post => &member.post_instance_id,
    };
    let mut writer = claim_writer_for_generation(
        base,
        &member.project_hash,
        &member.record_session_id,
        role,
        instance,
        &generation.capture_generation_id,
        generation.started_at_ms,
    )
    .unwrap();
    writer.mark_ready().unwrap();
    writer
}

fn post_preparation(
    stall: bool,
) -> (
    Result<(), crate::capture_generation::CaptureGenerationError>,
    Duration,
) {
    let temp = tempfile::tempdir().unwrap();
    let generation = CaptureGeneration::new_single(
        "project".into(),
        "post".into(),
        "pre".into(),
        "daw".into(),
        std::process::id(),
    );
    let mut transaction = CaptureGenerationTransaction::begin(temp.path(), &generation).unwrap();
    transaction.stage().unwrap();
    crate::record_signal::write_pending_claiming_expected_and_clock_for_generation(
        temp.path(),
        "project",
        "post",
        "pre".into(),
        "daw".into(),
        None,
        &generation,
    )
    .unwrap();
    let mut signal = crate::record_signal::read_signal(temp.path(), "project", "post").unwrap();
    signal.status = crate::record_signal::SignalStatus::Acknowledged;
    crate::record_signal::write_signal(temp.path(), "project", "post", &signal).unwrap();
    for (role, instance) in [(Role::Pre, "pre"), (Role::Post, "post")] {
        crate::record_entry_lock::claim_record_entry(
            temp.path(),
            "project",
            &generation.members[0].record_session_id,
            role,
            instance,
        )
        .unwrap();
    }
    let _pre_writer = (!stall).then(|| writer(temp.path(), &generation, Role::Pre));
    let mut post_writer = None;
    let origin = Instant::now();
    let elapsed = Cell::new(Duration::ZERO);
    let result = transaction.commit_when_ready_observing(
        TIMEOUT,
        || origin + elapsed.get(),
        || {
            elapsed.set(elapsed.get() + Duration::from_secs(1));
            let seconds = elapsed.get().as_secs();
            if !stall && [2, 4, 6].contains(&seconds) {
                crate::record_entry_lock::publish_measure_preparation(
                    temp.path(),
                    "project",
                    Role::Post,
                    "post",
                    &signal,
                    2,
                    seconds / 2 * 9_600,
                )
                .unwrap();
            }
            if !stall && seconds == 7 {
                post_writer = Some(writer(temp.path(), &generation, Role::Post));
            }
        },
    );
    if result.is_ok() {
        assert_eq!(
            crate::capture_generation_tx::ready::ready_producer_count(temp.path(), &generation),
            2,
            "completed Watch chunks are progress, never a substitute for both writers",
        );
    }
    (result, elapsed.get())
}

#[test]
fn post_completed_watch_chunks_keep_the_barrier_alive_until_its_writer_is_ready() {
    let (result, elapsed) = post_preparation(false);
    assert!(result.is_ok(), "POST preparation progresses: {result:?}");
    assert_eq!(elapsed, Duration::from_secs(7));
}

#[test]
fn both_idle_producers_still_return_a_typed_timeout_after_three_seconds() {
    let (result, elapsed) = post_preparation(true);
    assert_eq!(elapsed, Duration::from_secs(3));
    let Err(crate::capture_generation::CaptureGenerationError::Io(error)) = result else {
        panic!("stalled preparation must return its failure");
    };
    assert_eq!(error.kind(), std::io::ErrorKind::TimedOut);
    assert!(error.to_string().contains("PRE writer missing"));
    assert!(error.to_string().contains("POST writer missing"));
}
