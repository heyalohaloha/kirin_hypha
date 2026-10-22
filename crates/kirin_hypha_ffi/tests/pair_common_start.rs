//! Unreported input latency: transport starts recover a real paired exchange after a late insert.
#[allow(dead_code)]
mod keep_loop_host;
use keep_loop_host::callbacks::{push_configured, Clock, PreCallback};
use keep_loop_host::*;
use kirin_hypha_ffi::{
    channel_abi::ChannelLayout, kirin_hypha_poll_observatory_frame, KirinHyphaEngine,
    KirinObservatoryFrame,
};
use kirin_measure::{AuxiliaryClockSource, PresentationLatencySamples, PresentationLatencySource};
use std::{
    thread::sleep,
    time::{Duration, Instant},
};

fn clock(known: bool) -> Clock {
    Clock {
        latency: PresentationLatencySamples {
            source: PresentationLatencySource::AudioUnitV2,
            input: known.then_some(0),
            output: Some(685),
        },
        auxiliary: AuxiliaryClockSource::AudioUnitRender,
    }
}
fn pair(tag: &str) -> (KirinHyphaEngine, KirinHyphaEngine, Host) {
    let pre = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    pre.set_identity(
        format!("{tag}-pre"),
        format!("{tag}-project"),
        "".into(),
        "mix".into(),
    );
    pre.enable_pre_writes();
    let post = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    post.set_identity(
        format!("{tag}-post"),
        format!("{tag}-project"),
        "".into(),
        "out".into(),
    );
    post.enable_post_writes();
    post.set_pair_target("mix".into());
    let host = Host {
        position: 0,
        counter: 0,
        blocks: 0,
        start: Instant::now(),
        cycle: false,
        wrapped: false,
    };
    (pre, post, host)
}
fn advance(host: &mut Host) {
    host.position += BLOCK;
    host.counter += BLOCK;
    host.blocks += 1;
    let due = host.start + Duration::from_secs_f64(host.blocks as f64 * BLOCK as f64 / RATE);
    if due > Instant::now() {
        sleep(due - Instant::now());
    }
}
fn linear(host: &mut Host, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, clock: Clock) {
    host.cycle = false;
    host.wrapped = false;
    let mark = host.mark();
    let mut result = DeltaReturn::default();
    while result.polls < 470
        || (!result.returned() && host.played_since(&mark) < MEASURED_RUN_BLOCKS)
    {
        host.play_measured_configured(pre, post, 0.0, &mark, clock);
        result.note(settled_zero_delta(post));
    }
    result.assert_returned("same-window delta after the common start");
}
// A newly inserted side has different filter history. A complete short-term window proves
// three seconds of common PCM before this fixture demands floating-point equality.
fn settled_zero_delta(post: &KirinHyphaEngine) -> bool {
    let mut frame = unsafe { std::mem::zeroed::<KirinObservatoryFrame>() };
    let ok = unsafe {
        kirin_hypha_poll_observatory_frame(std::ptr::from_ref(post).cast_mut(), &mut frame)
    };
    if !ok || frame.delta_available == 0 || !frame.delta.lufs_s.is_finite() {
        return false;
    }
    let d = frame.delta;
    assert!(
        d.true_peak.abs() < 1e-6 && d.crest.abs() < 1e-6 && d.lufs.abs() < 1e-6,
        "common PCM after a full window: TP {} CREST {} M {}",
        d.true_peak,
        d.crest,
        d.lufs
    );
    true
}
fn finite_delta(post: &KirinHyphaEngine) -> bool {
    let mut frame = unsafe { std::mem::zeroed::<KirinObservatoryFrame>() };
    let ok = unsafe {
        kirin_hypha_poll_observatory_frame(std::ptr::from_ref(post).cast_mut(), &mut frame)
    };
    ok && frame.delta_available != 0 && frame.delta.lufs.is_finite()
}
#[test]
#[ignore = "slow: real paired exchange across late insertion, common restart, silence and callback loss"]
fn common_transport_starts_recover_the_pair_without_optional_input_latency() {
    let sandbox = tempfile::tempdir().unwrap();
    let _storage = StorageRoot::set(sandbox.path());
    for known in [false, true] {
        let (pre, post, mut host) = pair(if known { "reported" } else { "unreported" });
        let clock = clock(known);
        for _ in 0..5 * RATE as i64 / BLOCK {
            push_configured(
                &pre,
                &impulses(host.counter),
                host.position,
                host.counter,
                clock,
                true,
            );
            advance(&mut host);
        }
        let mark = host.mark();
        let mut first = DeltaReturn::default();
        let mut observed = false;
        for _ in 0..470 {
            host.play_measured_configured(&pre, &post, 0.0, &mark, clock);
            observed |= finite_delta(&post);
            first.note(settled_zero_delta(&post));
        }
        if known {
            first.assert_returned("reported latency still admits the late insertion");
        } else {
            assert!(
                !observed,
                "different starts alone grant no comparison authority"
            );
        }
        host.stopped(&pre, &post, 50);
        host.position = 0;
        linear(&mut host, &pre, &post, clock);
        if !known {
            // A host that stops PRE callbacks while POST continues cannot reuse that missed edge.
            for _ in 0..RATE as i64 / BLOCK {
                host.play_configured(&pre, PreCallback::NotCalled, &post, 0.0, clock);
            }
            // PRE resumes first; its missed edge cannot grant the POST a common start.
            // This common audio also settles the two filters before the shared loop edge.
            let resume = host.mark();
            for _ in 0..RATE as i64 / BLOCK {
                host.play_measured_configured(&pre, &post, 0.0, &resume, clock);
            }
            host.cycle = true;
            host.position = 0;
            host.wrapped = false;
            let mut recovered = DeltaReturn::default();
            for _ in 0..3 * LOOP_END / BLOCK {
                if host.position + BLOCK >= LOOP_END {
                    host.hold_measured_for_cycle(&pre, &post);
                }
                host.play_configured(&pre, PreCallback::Measured, &post, 0.0, clock);
                recovered.note(zero_delta(&post));
            }
            recovered.assert_returned("common loop wrap recovers after callback loss");
        }
    }
    let (pre, post, mut host) = pair("silent-head");
    let clock = clock(false);
    for _ in 0..RATE as i64 / BLOCK {
        push_configured(
            &pre,
            &vec![0.0; 2 * BLOCK as usize],
            host.position,
            host.counter,
            clock,
            false,
        );
        let noise: Vec<_> = (0..BLOCK)
            .flat_map(|n| {
                let x = if n % 2 == 0 { 1e-6 } else { -1e-6 };
                [x, x]
            })
            .collect();
        push_configured(&post, &noise, host.position, host.counter, clock, true);
        advance(&mut host);
    }
    let mark = host.mark();
    let mut seen = false;
    for _ in 0..470 {
        host.play_measured_configured(&pre, &post, QUIET, &mark, clock);
        seen |= finite_delta(&post);
    }
    assert!(
        seen,
        "a silent PRE and a noisy POST still share their first transport start"
    );
}
