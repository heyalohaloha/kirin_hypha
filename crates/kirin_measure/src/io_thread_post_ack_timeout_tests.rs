use super::*;
use crate::record::RecordState;
use crate::record_signal::{mark_acknowledged, write_pending};
use std::sync::atomic::AtomicU64;

const TEST_PH: &str = "ph";
const TEST_POST_IID: &str = "post-iid";

fn isolated_base(tag: &str) -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let pid = std::process::id();
    let dir = std::env::temp_dir().join(format!("kirin_ack_timeout_{pid}_{n}_{tag}"));
    let _ = fs::remove_dir_all(&dir);
    fs::create_dir_all(&dir).unwrap();
    dir
}

/// ACK timeout is diagnostic only. PRE may still become ready after bounce starts, so timeout
/// does not own Stop authority.
#[test]
fn pending_over_30s_keeps_record_armed() {
    let base = isolated_base("stale");
    let sm = Arc::new(RecordStateMachine::new());
    sm.try_enter_record(crate::License::Os).unwrap();
    let pair_label = Arc::new(Mutex::new("pair: deadbeef".to_string()));
    let paired_pre_target = Arc::new(Mutex::new(Some("pre-1".to_string())));

    write_pending(
        &base,
        TEST_PH,
        TEST_POST_IID,
        "pre-1".into(),
        "daw-1".into(),
    )
    .unwrap();
    crate::reservation::reserve_pairing(&base, TEST_PH, "pre-1", TEST_POST_IID).unwrap();
    assert_eq!(crate::reservation::count_frames(&base, TEST_PH), 1);
    let future_now = chrono::Utc::now() + chrono::Duration::seconds(31);

    poll_ack_timeout_with_base(
        &base,
        TEST_PH,
        TEST_POST_IID,
        &sm,
        &pair_label,
        &paired_pre_target,
        future_now,
    );

    let after = record_signal::read_signal(&base, TEST_PH, TEST_POST_IID).unwrap();
    assert_eq!(after.status, SignalStatus::Pending);
    assert_eq!(sm.current(), RecordState::Record);
    assert_eq!(
        pair_label.lock().unwrap().as_str(),
        "pair: deadbeef",
        "ACK timeout must not clear pair_label"
    );
    assert_eq!(
        paired_pre_target.lock().unwrap().as_deref(),
        Some("pre-1"),
        "ACK timeout must not reset paired_pre_target"
    );
    assert_eq!(
        crate::reservation::count_frames(&base, TEST_PH),
        1,
        "ACK timeout must not release the O_EXCL reservation frame"
    );
}

#[test]
fn pending_within_30s_is_noop() {
    let base = isolated_base("fresh");
    let sm = Arc::new(RecordStateMachine::new());
    sm.try_enter_record(crate::License::Os).unwrap();
    let pair_label = Arc::new(Mutex::new("pair: deadbeef".to_string()));
    let paired_pre_target = Arc::new(Mutex::new(Some("pre-1".to_string())));

    write_pending(
        &base,
        TEST_PH,
        TEST_POST_IID,
        "pre-1".into(),
        "daw-1".into(),
    )
    .unwrap();
    poll_ack_timeout_with_base(
        &base,
        TEST_PH,
        TEST_POST_IID,
        &sm,
        &pair_label,
        &paired_pre_target,
        chrono::Utc::now(),
    );

    let after = record_signal::read_signal(&base, TEST_PH, TEST_POST_IID).unwrap();
    assert_eq!(after.status, SignalStatus::Pending);
    assert_eq!(sm.current(), RecordState::Record);
    assert_eq!(
        pair_label.lock().unwrap().as_str(),
        "pair: deadbeef",
        "G-115-64: within-window must NOT clear pair_label"
    );
    assert_eq!(
        paired_pre_target.lock().unwrap().as_deref(),
        Some("pre-1"),
        "G-115-64: within-window must NOT clear paired_pre_target"
    );
}

#[test]
fn acknowledged_is_noop_even_over_30s() {
    let base = isolated_base("acked");
    let sm = Arc::new(RecordStateMachine::new());
    sm.try_enter_record(crate::License::Os).unwrap();
    let pair_label = Arc::new(Mutex::new("pair: deadbeef".to_string()));
    let paired_pre_target = Arc::new(Mutex::new(Some("pre-1".to_string())));

    write_pending(
        &base,
        TEST_PH,
        TEST_POST_IID,
        "pre-1".into(),
        "daw-1".into(),
    )
    .unwrap();
    mark_acknowledged(&base, TEST_PH, TEST_POST_IID).unwrap();

    let future_now = chrono::Utc::now() + chrono::Duration::seconds(300);
    poll_ack_timeout_with_base(
        &base,
        TEST_PH,
        TEST_POST_IID,
        &sm,
        &pair_label,
        &paired_pre_target,
        future_now,
    );

    let after = record_signal::read_signal(&base, TEST_PH, TEST_POST_IID).unwrap();
    assert_eq!(after.status, SignalStatus::Acknowledged);
    assert_eq!(sm.current(), RecordState::Record);
    assert_eq!(
        pair_label.lock().unwrap().as_str(),
        "pair: deadbeef",
        "G-115-64: Acknowledged must NOT clear pair_label"
    );
    assert_eq!(
        paired_pre_target.lock().unwrap().as_deref(),
        Some("pre-1"),
        "G-115-64: Acknowledged must NOT clear paired_pre_target"
    );
}

#[test]
fn missing_signal_is_noop() {
    let base = isolated_base("missing");
    let sm = Arc::new(RecordStateMachine::new());
    let pair_label = Arc::new(Mutex::new(String::new()));
    let paired_pre_target = Arc::new(Mutex::new(None));

    poll_ack_timeout_with_base(
        &base,
        TEST_PH,
        TEST_POST_IID,
        &sm,
        &pair_label,
        &paired_pre_target,
        chrono::Utc::now(),
    );

    assert_eq!(sm.current(), RecordState::Watch);
    assert!(pair_label.lock().unwrap().is_empty());
    assert!(paired_pre_target.lock().unwrap().is_none());
}
