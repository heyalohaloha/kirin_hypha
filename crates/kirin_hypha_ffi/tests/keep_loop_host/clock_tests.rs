//! The simulated host excludes the entire measurement hold, even after a late scheduler wake.
use super::*;
use std::cell::Cell;

#[test]
fn a_late_measurement_hold_leaves_no_callback_catch_up_debt() {
    let origin = Instant::now();
    let mut host = Host {
        position: BLOCK,
        counter: BLOCK,
        blocks: 1,
        start: origin,
        cycle: false,
        wrapped: false,
    };
    let elapsed = Cell::new(Duration::ZERO);
    let result = host.wait_for_measurement(
        || {
            let complete = elapsed.get() >= Duration::from_secs(1);
            [(
                Some(if complete { BLOCK } else { 0 }),
                Some(u64::from(complete)),
            ); 2]
        },
        |meter| meter.endpoint.unwrap_or(0) < BLOCK,
        || elapsed.get(),
        || elapsed.set(Duration::from_secs(1)),
    );
    assert!(result.is_ok());
    assert_eq!(host.start, origin + Duration::from_secs(1));
    let next_callback_due =
        host.start + Duration::from_secs_f64((host.blocks + 1) as f64 * BLOCK as f64 / RATE);
    let resumed = origin + elapsed.get();
    assert_eq!(
        next_callback_due.duration_since(resumed),
        Duration::from_secs_f64(2.0 * BLOCK as f64 / RATE),
    );
    assert_eq!(
        (host.blocks, host.position, host.counter),
        (1, BLOCK, BLOCK)
    );
}
