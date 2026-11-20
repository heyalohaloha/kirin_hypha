use super::*;

#[test]
fn timeout_disarms_hook_before_unwind_and_allows_next_fixture() {
    assert_timeout_disarms_hook("wait", Duration::from_millis(1));
}

#[test]
fn expired_deadline_disarms_hook_before_wait_and_allows_next_fixture() {
    assert_timeout_disarms_hook("expired", Duration::ZERO);
}

fn assert_timeout_disarms_hook(case: &str, timeout: Duration) {
    let path =
        std::env::temp_dir().join(format!("hypha-pause-timeout-{}-{case}", std::process::id()));
    let mut pause = AtomicWritePause::install(path.clone());
    assert_eq!(pause.enter_timeout, Duration::from_secs(5));
    // Only this timeout fixture opts in; ordinary hook users keep five seconds.
    pause.enter_timeout = timeout;
    let result =
        std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| pause.wait_until_entered()));
    assert!(result.is_err(), "fixture must exercise the real timeout");
    // Even with no arriving worker, timeout must not poison the next fixture.
    drop(pause);
    let next = AtomicWritePause::install(path.clone());
    std::thread::scope(|scope| {
        scope.spawn(|| pause_atomic_write_if_requested(&path));
        next.wait_until_entered();
        next.release();
    });
}
