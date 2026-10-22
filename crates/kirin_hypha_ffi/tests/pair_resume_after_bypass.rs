//! A host bypass of PRE while POST plays a whole LOOP pass alone, then a stop and the next
//! playbacks. A host can stop callbacks or keep calling with an explicit bypass flag. Either way PRE measures nothing for that pass, and the matched LEVEL
//! Δ of identical PRE/POST audio returns on the next cycle and on the next linear playback, joining
//! only the same windows. Simulated host facts, independent of actual DAW acceptance.
// Shared with the Keep LOOP test, which uses the rest of the simulated host.
#[allow(dead_code)]
mod keep_loop_host;

use keep_loop_host::callbacks::PreCallback;
use keep_loop_host::*;
use kirin_hypha_ffi::{channel_abi::ChannelLayout, KirinHyphaEngine};
use std::time::Instant;

/// A playback until the matched Δ has returned and held, paced by the measurement.
fn linear(host: &mut Host, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, what: &str) {
    host.cycle = false;
    host.wrapped = false;
    let mark = host.mark();
    let mut delta = DeltaReturn::default();
    while delta.polls < 470 || (!delta.returned() && host.played_since(&mark) < MEASURED_RUN_BLOCKS)
    {
        host.play_measured(pre, post, 0.0, &mark);
        delta.note(zero_delta(post));
    }
    delta.assert_returned(what);
}

#[test]
#[ignore = "slow: real PRE/POST pair across a realtime host bypass of PRE"]
fn the_delta_returns_after_post_played_a_pass_alone() {
    let sandbox = tempfile::tempdir().unwrap();
    let _storage = StorageRoot::set(sandbox.path());
    let paths = kirin_measure::PlatformPaths::default_current()
        .unwrap()
        .storage;
    assert!(
        paths.kirin_os_root.starts_with(sandbox.path()),
        "test storage stays in the sandbox"
    );
    for (name, bypass) in [
        ("stopped-callbacks", PreCallback::NotCalled),
        ("flagged", PreCallback::Bypassed),
    ] {
        let pre = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
        pre.set_identity(
            format!("{name}-pre"),
            format!("{name}-project"),
            "".into(),
            "mix".into(),
        );
        pre.enable_pre_writes();
        let post = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
        post.set_identity(
            format!("{name}-post"),
            format!("{name}-project"),
            "".into(),
            "output".into(),
        );
        post.enable_post_writes();
        post.set_pair_target("mix".into());
        let mut host = Host {
            position: 0,
            counter: 0,
            blocks: 0,
            start: Instant::now(),
            cycle: false,
            wrapped: false,
        };
        // Positive control: a linear playback compares; it stops seconds after the cycle's end.
        linear(
            &mut host,
            &pre,
            &post,
            &format!("{name}: Δ before the bypass"),
        );
        host.stopped(&pre, &post, 50);
        eprintln!("=== {name}: bypass");

        // PRE bypassed: POST alone plays the cycle from its start through a wrap, then stops.
        host.cycle = true;
        host.position = 0;
        host.wrapped = false;
        for _ in 0..LOOP_END * 5 / 4 / BLOCK {
            host.play_with(&pre, bypass, &post, 0.0);
        }
        host.stopped_with(&pre, bypass, &post, 50);
        host.stopped(&pre, &post, 50);

        eprintln!("=== {name}: cycle");
        // PRE is back. The cycle plays from its start: the Δ returns within it.
        host.position = 0;
        host.wrapped = false;
        let mut cycle = DeltaReturn::default();
        for _ in 0..3 * LOOP_END / BLOCK {
            host.play_cycle_measured(&pre, &post, 0.0);
            cycle.note(zero_delta(&post));
        }
        cycle.assert_returned(&format!("{name}: Δ in the cycle after the bypass"));
        host.stopped(&pre, &post, 50);

        eprintln!("=== {name}: linear");
        // A linear playback from inside the cycle, before where it stopped.
        host.position = LOOP_END / 4;
        linear(
            &mut host,
            &pre,
            &post,
            &format!("{name}: Δ in a linear playback"),
        );
    }
}
