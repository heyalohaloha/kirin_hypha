//! The POST IO poll publishes only this state machine's completed Watch preparation.
use super::*;
use crate::capture_generation::CaptureGeneration;

#[test]
fn the_post_io_poll_publishes_completed_preparation_for_its_exact_session() {
    let temp = tempfile::tempdir().unwrap();
    let generation = CaptureGeneration::new_single(
        "project".into(),
        "post".into(),
        "pre".into(),
        "daw".into(),
        std::process::id(),
    );
    let member = &generation.members[0];
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
    let mut signal = record_signal::read_signal(temp.path(), "project", "post").unwrap();
    signal.status = SignalStatus::Acknowledged;
    record_signal::write_signal(temp.path(), "project", "post", &signal).unwrap();
    crate::record_entry_lock::claim_record_entry(
        temp.path(),
        "project",
        &member.record_session_id,
        PluginDataRole::Post,
        "post",
    )
    .unwrap();
    let sm = Arc::new(RecordStateMachine::new());
    sm.try_enter_record_started_at_clock_window_transaction(
        crate::License::Os,
        generation.started_at_ms,
        None,
        None,
        member.record_session_id.clone(),
    )
    .unwrap();
    let local_generation = sm.generation();
    sm.begin_measure_preparation(local_generation);
    sm.note_measure_preparation_samples(9_600);
    let ingress = crate::RecordIngress::new(32);
    let label = Arc::new(Mutex::new(String::new()));
    let read = || {
        crate::record_entry_lock::measure_preparation_samples(
            temp.path(),
            &generation,
            member,
            PluginDataRole::Post,
        )
    };
    assert_eq!(read(), 0);
    poll_record_signal_ack_with_base(
        temp.path(),
        "project",
        "post",
        48_000,
        &sm,
        &label,
        &ingress,
    );
    assert_eq!(read(), 9_600);
    sm.note_measure_preparation_samples(4_800);
    poll_record_signal_ack_with_base(
        temp.path(),
        "project",
        "post",
        48_000,
        &sm,
        &label,
        &ingress,
    );
    assert_eq!(read(), 14_400);
    // A different session cannot publish the currently accumulating samples into the old claim.
    signal.session_id = "other-session".into();
    record_signal::write_signal(temp.path(), "project", "post", &signal).unwrap();
    sm.note_measure_preparation_samples(4_800);
    poll_record_signal_ack_with_base(
        temp.path(),
        "project",
        "post",
        48_000,
        &sm,
        &label,
        &ingress,
    );
    assert_eq!(read(), 14_400);
}
