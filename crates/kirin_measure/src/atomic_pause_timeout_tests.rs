use super::*;

#[test]
fn timeout_disarms_hook_before_unwind_and_allows_next_fixture() {
    let path = std::env::temp_dir().join(format!("hypha-pause-timeout-{}", std::process::id()));
    let pause = AtomicWritePause::install(path.clone());
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
