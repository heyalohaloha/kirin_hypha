//! Real paired Keep while the DAW plays a realtime LOOP. Every wrap starts a new capture epoch;
//! the live meters and the Keep summary must follow every captured frame, never only the first
//! pass, and the record lists every pass. Simulated clock facts (a project timeline that wraps and
//! a continuous host counter), independent of actual DAW acceptance. A post-bounce position jump
//! keeps its own contract.
mod keep_loop_host;

use keep_loop_host::*;
use kirin_hypha_ffi::{channel_abi::ChannelLayout, KirinHyphaEngine, KIRIN_KEEP_PHASE_IDLE};
use std::time::{Duration, Instant};

/// The record explains the Keep's length: the TRACE is the first pass, every later pass is listed
/// with its host range and accepted frames, and a LOOP wrap degrades nothing.
fn explain_passes(role: &str, member: &serde_json::Value) -> (usize, u64) {
    let Some(passes) = member["keep_passes"].as_array() else {
        return (0, 0); // A Keep that never left its first pass lists nothing.
    };
    let field = |pass: &serde_json::Value, key: &str| pass[key].as_i64().expect(key);
    let first = &passes[0];
    assert!(first["in_trace"] == true && first["trace_excluded_reason"].is_null());
    let wraps = passes[1..]
        .iter()
        .take_while(|pass| pass["trace_excluded_reason"] == "loop_wrap")
        .count();
    let mut total = 0;
    for pass in passes {
        let frames = field(pass, "frames");
        assert_eq!(
            frames,
            field(pass, "host_end_position_samples") - field(pass, "host_start_position_samples"),
            "{role} pass frames are its host range"
        );
        assert_eq!(
            pass["summary_frames"], pass["frames"],
            "{role} realtime passes reach the summary"
        );
        total += frames as u64;
    }
    let trace_frames = member["frames"].as_array().expect("TRACE").len() as i64;
    assert!(
        trace_frames * 4_800 <= field(first, "frames") + 4_800,
        "{role} TRACE stays within its first pass"
    );
    assert_eq!(
        member["integrity_degraded"], false,
        "{role} a LOOP wrap degrades nothing"
    );
    (wraps, total)
}

#[test]
#[ignore = "slow: real PRE/POST Keep across realtime LOOP wraps"]
fn keep_follows_every_pass_of_a_realtime_loop() {
    let sandbox = tempfile::tempdir().unwrap();
    std::env::set_var(kirin_measure::TEST_STORAGE_ROOT_ENV, sandbox.path());
    let paths = kirin_measure::PlatformPaths::default_current()
        .unwrap()
        .storage;
    assert!(
        paths.kirin_os_root.starts_with(sandbox.path()),
        "test storage stays in the sandbox"
    );
    std::fs::create_dir_all(&paths.kirin_os_root).unwrap();
    std::fs::write(paths.primary_path(), br#"{"schema_version":"1.0","installation_id":"keep-loop-fixture","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#).unwrap();
    let pre = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    pre.set_identity(
        "loop-pre".into(),
        "loop-project".into(),
        "".into(),
        "mix".into(),
    );
    pre.enable_pre_writes();
    let post = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    post.set_identity(
        "loop-post".into(),
        "loop-project".into(),
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
    };
    // Positive control: before any Keep the pair joins only identical windows.
    let mut zero_before = DeltaReturn::default();
    let deadline = Instant::now() + CATCH_UP;
    while zero_before.polls < 300 || (!zero_before.returned() && Instant::now() < deadline) {
        host.play(&pre, &post, 0.0);
        zero_before.note(zero_delta(&post));
    }
    zero_before.assert_returned("LEVEL Δ is available before the Keep");
    host.cycle = true;
    host.position = 0;
    for _ in 0..150 {
        host.play(&pre, &post, QUIET);
    }
    assert!(post.keep(), "unique private PRE accepted Keep");
    host.arm(&pre, &post, QUIET);

    // Keep across four realtime passes; the level rises after the first wrap, as a fader would.
    let meter_before = post
        .poll_meter_session()
        .expect("meter session")
        .active_frames;
    let mut level = QUIET;
    let mut wraps = 0;
    let mut keep_frames = 0_u64;
    let mut keep_delta = DeltaReturn::default();
    for _ in 0..750 {
        if host.play(&pre, &post, level) {
            wraps += 1;
            level = LOUD;
        }
        keep_frames += BLOCK as u64;
        keep_delta.note(zero_delta(&post));
    }
    assert!(wraps >= 3, "the Keep spans several cycle wraps");
    // A current Δ needs both Measure Threads within the comparison's freshness of the audio. A
    // host that cannot keep pace (background QoS) shows no Δ in the Keep, never a wrong one.
    let lag = keep_frames.saturating_sub(
        post.poll_meter_session()
            .map_or(0, |m| m.active_frames.saturating_sub(meter_before)),
    );
    let kept_pace = lag < 48_000;

    // Live display: the meter session catches up with every frame played in the Keep (the
    // Measure Thread may lag; it never stops at a wrap), and the live LEVEL follows the audio.
    // The Keep goes on meanwhile, so the matched Δ is still read inside it. Playback leaves the
    // cycle here: while a lagging side is still measuring the previous pass, the same positions
    // are never compared, so the Δ returns once both sides measure the same new audio.
    host.cycle = false;
    let mut advanced = 0;
    let deadline = Instant::now() + CATCH_UP;
    loop {
        advanced = post
            .poll_meter_session()
            .map_or(advanced, |m| m.active_frames.saturating_sub(meter_before));
        let delta_done = keep_delta.returned() || !kept_pace;
        if (advanced >= keep_frames && delta_done) || Instant::now() >= deadline {
            break;
        }
        host.play(&pre, &post, LOUD);
        keep_delta.note(zero_delta(&post));
    }
    if kept_pace {
        keep_delta.assert_returned("the matched LEVEL Δ stays available through the Keep");
    } else {
        // Points older than the comparison's freshness are never compared, so a host whose
        // Measure Threads lag seconds behind shows no current Δ; it must still never be wrong
        // (`zero_delta`). The hold and release across wraps are covered without timing by
        // kirin_measure's time_pair loop tests.
        eprintln!(
            "the Measure Thread lagged {lag} frames: in-Keep Δ {} of {} polls",
            keep_delta.zero, keep_delta.polls
        );
    }
    assert!(
        advanced >= keep_frames,
        "the meter session advances through every wrap: {advanced} of {keep_frames}"
    );
    let live = post.poll_result().expect("live result");
    let live_m = live.lufs_m.expect("live LUFS-M");
    assert!(
        live_m > -12.0,
        "live LEVEL shows the louder later passes: {live_m}"
    );
    // Keep summary: I and Max TP describe the whole Keep, as the README promises.
    let summary = post.poll_session().expect("Keep summary");
    let keep_tp = summary.max_true_peak.expect("Keep Max TP");
    assert!(
        keep_tp > -10.0,
        "Keep Max TP includes the later passes: {keep_tp}"
    );

    post.stop();
    host.plays_on_after_keep(&pre, &post, 3_000_000);

    // A Keep that ends while the transport is stopped, and a next Keep that starts before play
    // resumes: the Watch index still skips both Keeps' frames, so after them the TIME points name
    // their audio and the matched Δ returns to zero.
    host.cycle = true;
    host.position = 0;
    assert!(post.keep(), "second Keep");
    host.arm(&pre, &post, LOUD);
    for _ in 0..300 {
        host.play(&pre, &post, LOUD);
    }
    host.stopped(&pre, &post, 40);
    post.stop();
    let deadline = Instant::now() + Duration::from_secs(12);
    while post.is_recording() || pre.is_recording() || post.keep_phase() != KIRIN_KEEP_PHASE_IDLE {
        assert!(Instant::now() < deadline, "the stopped Keep closes");
        host.stopped(&pre, &post, 4);
    }
    host.stopped(&pre, &post, 40);
    // The previous Keep's writers may still be closing ("Another Keep is active"): press again.
    let deadline = Instant::now() + CATCH_UP;
    while !post.keep() {
        assert!(Instant::now() < deadline, "a Keep started while stopped");
        host.stopped(&pre, &post, 4);
    }
    host.stopped(&pre, &post, 40);
    host.cycle = false;
    host.position = 5_000_000;
    host.arm(&pre, &post, LOUD);
    for _ in 0..200 {
        host.play(&pre, &post, LOUD);
    }
    post.stop();
    host.plays_on_after_keep(&pre, &post, 7_000_000);
    drop(post);
    drop(pre);
    let mut output = Vec::new();
    collect(&paths.plugin_data_dir(), &mut output);
    for role in ["PRE", "POST"] {
        let members: Vec<_> = output.iter().filter(|v| v["role"] == role).collect();
        assert!(members.len() >= 3, "{role} records of all three Keeps");
        let (mut most_wraps, mut longest) = (0, 0);
        for member in members {
            let typed: kirin_measure::plugin_data::PluginDataFile =
                serde_json::from_value(member.clone()).unwrap();
            assert!(kirin_measure::plugin_data::verify_checksum(&typed));
            let lufs_i = member["lufs_i"].as_f64().expect("Keep I");
            assert!(lufs_i > -14.0, "{role} Keep I covers every pass: {lufs_i}");
            let (wraps, total) = explain_passes(role, member);
            most_wraps = most_wraps.max(wraps);
            longest = longest.max(total);
        }
        assert!(
            most_wraps >= 3,
            "{role} lists every LOOP wrap of the first Keep"
        );
        assert!(
            longest >= keep_frames,
            "{role} passes hold the Keep: {longest} of {keep_frames}"
        );
    }
}
