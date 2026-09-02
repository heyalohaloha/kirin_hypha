use super::{
    reconcile_closed_drop_target, resolve_closed_drop_target, ClosedDropRecovery, POLL_INTERVAL,
};
use crate::record_signal::{self, RecordSignal, ReleaseReason, SignalStatus};
use std::cell::{Cell, RefCell};
use std::fs;
use std::path::{Path, PathBuf};
use std::sync::atomic::{AtomicU64, Ordering};
use std::time::{Duration, Instant};

const PROJECT: &str = "project-a";
const POST: &str = "post-a";

fn isolated_base() -> PathBuf {
    static COUNTER: AtomicU64 = AtomicU64::new(0);
    let n = COUNTER.fetch_add(1, Ordering::Relaxed);
    let base = std::env::temp_dir().join(format!(
        "kirin-closed-drop-target-{}-{n}",
        std::process::id()
    ));
    let _ = fs::remove_dir_all(&base);
    fs::create_dir_all(&base).unwrap();
    base
}

fn write_signal(base: &Path, status: SignalStatus, session_id: &str, pre_id: &str) {
    let mut signal = RecordSignal::new_pending(
        POST.to_string(),
        pre_id.to_string(),
        "daw-a".to_string(),
        None,
        None,
    );
    signal.status = status;
    signal.session_id = session_id.to_string();
    signal.release_reason = (status == SignalStatus::Released).then_some(ReleaseReason::ManualStop);
    record_signal::write_signal(base, PROJECT, POST, &signal).unwrap();
}

#[test]
fn record_state_defers_poll_without_consuming_the_immediate_watch_tick() {
    let start = Instant::now();
    let mut state = ClosedDropRecovery::new(start);
    state.service_with(start, true, |_| {
        panic!("Record must not inspect a closed session")
    });
    let calls = Cell::new(0);
    state.service_with(start, false, |_| {
        calls.set(calls.get() + 1);
        None
    });
    assert_eq!(calls.get(), 1);
}

#[test]
fn watch_poll_is_throttled_and_includes_the_one_second_boundary() {
    let start = Instant::now();
    let mut state = ClosedDropRecovery::new(start);
    let calls = Cell::new(0);
    state.service_with(start, false, |_| {
        calls.set(calls.get() + 1);
        None
    });
    state.service_with(
        start + POLL_INTERVAL - Duration::from_nanos(1),
        false,
        |_| {
            calls.set(calls.get() + 1);
            None
        },
    );
    assert_eq!(calls.get(), 1);
    state.service_with(start + POLL_INTERVAL, false, |_| {
        calls.set(calls.get() + 1);
        None
    });
    assert_eq!(calls.get(), 2);
}

#[test]
fn completed_session_is_remembered_and_passed_to_later_polls() {
    let start = Instant::now();
    let mut state = ClosedDropRecovery::new(start);
    state.service_with(start, false, |_| Some("session-done".to_string()));
    let observed = RefCell::new(None);
    state.service_with(start + POLL_INTERVAL, false, |completed| {
        *observed.borrow_mut() = completed.map(str::to_string);
        None
    });
    assert_eq!(observed.into_inner().as_deref(), Some("session-done"));
}

#[test]
fn released_signal_recovers_target_after_all_memory_is_lost() {
    let base = isolated_base();
    write_signal(&base, SignalStatus::Released, "session-a", "pre-a");
    assert_eq!(
        resolve_closed_drop_target(&base, PROJECT, POST, None, None),
        Some(("session-a".to_string(), "pre-a".to_string()))
    );
}

#[test]
fn active_signal_never_claims_to_be_a_stopped_drop_target() {
    let base = isolated_base();
    write_signal(
        &base,
        SignalStatus::Acknowledged,
        "session-live",
        "pre-live",
    );
    assert_eq!(
        resolve_closed_drop_target(&base, PROJECT, POST, None, None),
        None
    );
}

#[test]
fn in_memory_target_remains_the_legacy_fallback() {
    let base = isolated_base();
    assert_eq!(
        resolve_closed_drop_target(
            &base,
            PROJECT,
            POST,
            Some("session-memory"),
            Some("pre-memory"),
        ),
        Some(("session-memory".to_string(), "pre-memory".to_string()))
    );
}

#[test]
fn completed_session_is_not_reconciled_twice() {
    let base = isolated_base();
    write_signal(&base, SignalStatus::Released, "session-done", "pre-a");
    assert_eq!(
        reconcile_closed_drop_target(&base, PROJECT, POST, None, None, Some("session-done")),
        None
    );
}
