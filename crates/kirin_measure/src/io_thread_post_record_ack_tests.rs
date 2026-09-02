use super::*;
use crate::record::RecordState;
use crate::record_expected::ExpectedWavMetadata;
use crate::record_signal::{RecordSignal, SignalStatus};
use std::sync::atomic::{AtomicU64, Ordering};

const TEST_PH: &str = "ph";
const TEST_POST_IID: &str = "post-iid";

fn isolated_base(tag: &str) -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_ack_barrier_{pid}_{n}_{tag}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

fn expected_wav() -> ExpectedWavMetadata {
    ExpectedWavMetadata {
        expected_duration_samples: 48_000,
        expected_sample_rate: 48_000,
        wav_time_reference_samples: None,
        wav_path: "/tmp/kirin-post-ack-test.wav".to_string(),
        bounce_id: "test-bounce".to_string(),
        created_at_ms: chrono::Utc::now().timestamp_millis(),
        wav_file_size: Some(1),
        wav_mtime_ms: chrono::Utc::now().timestamp_millis(),
        wav_hash: Some("test-wav-hash".to_string()),
        consumed_at_ms: None,
        consumed_by_session_id: None,
    }
}

fn write_ack(base: &Path, started_at: &str) {
    write_ack_with_expected(base, started_at, Some(expected_wav()));
}

fn write_ack_with_expected(
    base: &Path,
    started_at: &str,
    expected_wav: Option<ExpectedWavMetadata>,
) {
    let signal = RecordSignal {
        status: SignalStatus::Acknowledged,
        requested_by: TEST_POST_IID.to_string(),
        target_pre_instance_id: "pre-iid".to_string(),
        daw_session_id: "daw-1".to_string(),
        session_id: "session-post-ack".to_string(),
        capture_generation_id: String::new(),
        generation_started_at_ms: 0,
        t: "2026-07-05T00:00:00Z".to_string(),
        started_at: started_at.to_string(),
        started_at_position_samples: None,
        paired_pre_name: "PRE".to_string(),
        release_reason: None,
        expected_wav,
    };
    crate::record_signal::write_signal(base, TEST_PH, TEST_POST_IID, &signal).unwrap();
}

#[test]
fn exact_generation_member_accepts_instance_scoped_au_vst_daw_ids() {
    let base = isolated_base("split-au-vst-daw-id");
    let generation = crate::CaptureGeneration::new_single_named(
        TEST_PH.to_string(),
        TEST_POST_IID.to_string(),
        "pre-iid".to_string(),
        "originator-au-daw-id".to_string(),
        crate::current_host_process_id(),
        Some("2Mix".to_string()),
    );
    let member = generation.members[0].clone();
    let mut transaction =
        crate::CaptureGenerationTransaction::begin(&base, &generation).expect("generation lock");
    transaction.stage().expect("producer preparation");
    let signal = RecordSignal {
        status: SignalStatus::Acknowledged,
        requested_by: TEST_POST_IID.to_string(),
        target_pre_instance_id: member.pre_instance_id.clone(),
        daw_session_id: "remote-vst-daw-id".to_string(),
        session_id: member.record_session_id,
        capture_generation_id: generation.capture_generation_id.clone(),
        generation_started_at_ms: generation.started_at_ms,
        t: "2026-07-15T00:00:00Z".to_string(),
        started_at: "2026-07-15T00:00:00Z".to_string(),
        started_at_position_samples: None,
        paired_pre_name: "2Mix".to_string(),
        release_reason: None,
        expected_wav: None,
    };

    assert!(post_ack_generation_is_authorized(
        &base,
        TEST_PH,
        TEST_POST_IID,
        &signal,
    ));
}

#[test]
fn acknowledged_signal_waits_until_started_at_barrier() {
    let base = isolated_base("future");
    write_ack(&base, "2099-01-01T00:00:00Z");
    let sm = Arc::new(RecordStateMachine::new());
    let pair_label = Arc::new(Mutex::new(String::new()));
    let record_ingress = crate::RecordIngress::new(16);

    poll_record_signal_ack_with_base(
        &base,
        TEST_PH,
        TEST_POST_IID,
        48_000,
        &sm,
        &pair_label,
        &record_ingress,
    );
    assert_eq!(sm.current(), RecordState::Watch);

    write_ack(&base, "2026-07-05T00:00:00Z");
    poll_record_signal_ack_with_base(
        &base,
        TEST_PH,
        TEST_POST_IID,
        48_000,
        &sm,
        &pair_label,
        &record_ingress,
    );
    assert_eq!(sm.current(), RecordState::Record);
    assert_eq!(
        sm.record_started_at_ms(),
        parse_iso8601_to_epoch_ms("2026-07-05T00:00:00Z").unwrap()
    );
}

#[test]
fn acknowledged_without_expected_metadata_enters_degraded_record_path() {
    let base = isolated_base("degraded");
    write_ack_with_expected(&base, "2026-07-05T00:00:00Z", None);
    let sm = Arc::new(RecordStateMachine::new());
    let pair_label = Arc::new(Mutex::new(String::new()));
    let record_ingress = crate::RecordIngress::new(16);

    poll_record_signal_ack_with_base(
        &base,
        TEST_PH,
        TEST_POST_IID,
        48_000,
        &sm,
        &pair_label,
        &record_ingress,
    );

    assert_eq!(sm.current(), RecordState::Record);
    assert_eq!(
        sm.record_started_at_ms(),
        parse_iso8601_to_epoch_ms("2026-07-05T00:00:00Z").unwrap()
    );
}

#[test]
fn second_keep_retries_after_prior_record_lane_finishes_draining() {
    let base = isolated_base("record-lane-drain-retry");
    write_ack(&base, "2026-07-05T00:00:00Z");
    let sm = Arc::new(RecordStateMachine::new());
    let pair_label = Arc::new(Mutex::new(String::new()));
    let record_ingress = crate::RecordIngress::new(16);

    // Model the prior take between Stop and the Measure-thread drain acknowledgement.
    assert!(record_ingress.prepare_for_generation(7));
    poll_record_signal_ack_with_base(
        &base,
        TEST_PH,
        TEST_POST_IID,
        48_000,
        &sm,
        &pair_label,
        &record_ingress,
    );
    assert_eq!(sm.current(), RecordState::Watch);

    // The failed poll must not have claimed this new session. Once the prior lane drain is
    // acknowledged, the same durable ACK enters Record on its next ordinary poll.
    let mut prior_consumer = record_ingress.take_consumer_for_measure(7).unwrap();
    assert!(record_ingress.finish_capture_from_measure(7, std::time::Duration::from_secs(1)));
    while prior_consumer.pop().is_ok() {}
    record_ingress.mark_drained_from_measure(7);
    poll_record_signal_ack_with_base(
        &base,
        TEST_PH,
        TEST_POST_IID,
        48_000,
        &sm,
        &pair_label,
        &record_ingress,
    );
    assert_eq!(sm.current(), RecordState::Record);
}

#[test]
fn duplicate_post_state_machines_same_ack_only_one_enters_record() {
    let base = isolated_base("duplicate-post-entry");
    write_ack(&base, "2026-07-05T00:00:00Z");
    let first = Arc::new(RecordStateMachine::new());
    let second = Arc::new(RecordStateMachine::new());
    let pair_label = Arc::new(Mutex::new(String::new()));
    let first_ingress = crate::RecordIngress::new(16);
    let second_ingress = crate::RecordIngress::new(16);

    poll_record_signal_ack_with_base(
        &base,
        TEST_PH,
        TEST_POST_IID,
        48_000,
        &first,
        &pair_label,
        &first_ingress,
    );
    poll_record_signal_ack_with_base(
        &base,
        TEST_PH,
        TEST_POST_IID,
        48_000,
        &second,
        &pair_label,
        &second_ingress,
    );

    assert_eq!(first.current(), RecordState::Record);
    assert_eq!(
        second.current(),
        RecordState::Watch,
        "same session/POST instance must have only one cross-process Record entrant"
    );
}
