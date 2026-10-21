//! Let real workers finish their admitted content; elapsed wall time is never their completion.
use kirin_hypha_ffi::KirinHyphaEngine;
use std::time::{Duration, Instant};

fn worker_state(engine: &KirinHyphaEngine) -> String {
    format!(
        "keep_phase={}, is_recording={}, display_status={:?}, measure_alive={}, ring_drained={}",
        engine.keep_phase(),
        engine.is_recording(),
        engine.poll_record_display().map(|snapshot| snapshot.status),
        engine.measure_alive(),
        engine.__ring_drained_for_test(),
    )
}

pub fn service_until(
    pre: &KirinHyphaEngine,
    post: &KirinHyphaEngine,
    mut ready: impl FnMut() -> bool,
) {
    // Only readiness succeeds; the deadline makes a stalled worker fail with diagnostics.
    let started = Instant::now();
    loop {
        // Keep host liveness fresh without admitting another audio frame or moving its clock.
        pre.push_samples(&[], 2);
        post.push_samples(&[], 2);
        if ready() {
            return;
        }
        assert!(
            pre.measure_alive()
                && post.measure_alive()
                && started.elapsed() < Duration::from_secs(120),
            "paired workers stopped or exceeded the failure deadline: elapsed={:?}; PRE: {}; POST: {}",
            started.elapsed(),
            worker_state(pre),
            worker_state(post),
        );
        std::thread::sleep(Duration::from_millis(5));
    }
}

pub fn wait_for_admitted_audio(pre: &KirinHyphaEngine, post: &KirinHyphaEngine) {
    service_until(pre, post, || {
        pre.__ring_drained_for_test() && post.__ring_drained_for_test()
    });
}

pub fn wait_for_closed_writers(pre: &KirinHyphaEngine, post: &KirinHyphaEngine) {
    service_until(pre, post, || !post.is_recording() && !pre.is_recording());
    service_until(pre, post, || {
        [pre, post].into_iter().all(|engine| {
            engine.poll_record_display().is_some_and(|snapshot| {
                matches!(
                    snapshot.status,
                    kirin_measure::RecordDisplayStatus::Finalized
                        | kirin_measure::RecordDisplayStatus::Unavailable
                        | kirin_measure::RecordDisplayStatus::Dismissed
                )
            })
        })
    });
}
