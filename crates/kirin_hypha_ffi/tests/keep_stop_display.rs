//! Real paired Keep, explicit Stop, Measure drain, seal and writer close (the Test 4 steps): once
//! both records are closed, the POST display shows the Keep's result and never stays RECORD
//! FINALIZING. A Keep of signal; one whose transport plays on past the material (a silent tail the
//! shell reports Inactive while the Keep still captures it); and one with an offline bounce of the
//! material, after which realtime playback resumes or the transport stays stopped. Simulated clock
//! facts, independent of actual DAW acceptance; the lengths are shortened from the field case.
#[allow(dead_code)]
mod keep_loop_host;

use keep_loop_host::*;
use kirin_hypha_ffi::{channel_abi::ChannelLayout, KirinHyphaEngine};
use kirin_measure::{
    AuxiliaryClockSamples, AuxiliaryClockSource, CaptureClockSource, PresentationLatencySamples,
    PresentationLatencySource, RecordDisplayStatus, RecordTakeBlock,
};
use std::path::Path;
use std::thread::sleep;
use std::time::{Duration, Instant};

const SIGNAL_BLOCKS: i64 = 4 * RATE as i64 / BLOCK;
const SILENT_TAIL_BLOCKS: i64 = 4 * RATE as i64 / BLOCK;
/// The bounced material: the signal's length from the project start.
const BOUNCE_BLOCKS: i64 = SIGNAL_BLOCKS;
const AFTER_BOUNCE_BLOCKS: i64 = RATE as i64 / BLOCK;
/// Stopped-transport callbacks between two reads of the closed records (about 0.25 s).
const CLOSE_READ_BLOCKS: i64 = 24;

/// One callback on both sides. `offline`: the host renders a bounce faster than realtime (no
/// pacing); otherwise the callback follows audio time. `level` 0 is digital silence, which the
/// shell reports Inactive (silence gates the displayed state also during Record) while the Keep
/// captures it.
fn callback(
    host: &mut Host,
    pre: &KirinHyphaEngine,
    post: &KirinHyphaEngine,
    level: f32,
    offline: bool,
) {
    let block = if level > 0.0 {
        tone(level, host.counter)
    } else {
        vec![0.0_f32; 2 * BLOCK as usize]
    };
    for engine in [pre, post] {
        let recording = engine.is_recording();
        engine.set_signal_state(u8::from(level > 0.0));
        engine.note_record_block(RecordTakeBlock {
            generation: 0,
            recording,
            rendered: recording,
            playing: !offline,
            offline,
            position_valid: true,
            position_samples: host.position,
            num_frames: BLOCK as u64,
            clock_start_samples: 0,
            clock_end_samples: None,
        });
        engine.note_capture_window_with_clocks(
            true,
            host.position,
            BLOCK as u64,
            CaptureClockSource::ProjectTimeline,
            PresentationLatencySamples {
                source: PresentationLatencySource::Vst3,
                input: Some(0),
                output: Some(0),
            },
            AuxiliaryClockSamples {
                source: AuxiliaryClockSource::Vst3Continuous,
                samples: Some(host.counter),
            },
            false,
        );
        engine.push_samples(&block, 2);
    }
    host.counter += BLOCK;
    host.position += BLOCK;
    if offline {
        // An offline bounce runs ahead of audio time; pacing resumes from here.
        host.start =
            Instant::now() - Duration::from_secs_f64(host.blocks as f64 * BLOCK as f64 / RATE);
        return;
    }
    host.blocks += 1;
    let due = host.start + Duration::from_secs_f64(host.blocks as f64 * BLOCK as f64 / RATE);
    if due > Instant::now() {
        sleep(due - Instant::now());
    }
}

/// What the host does inside the Keep before the explicit Stop.
#[derive(Clone, Copy, Debug)]
enum Keep {
    /// Signal only.
    Signal,
    /// Signal, then the transport plays on past the material in silence.
    SilentTail,
    /// Signal, an offline bounce of the material from the project start, then realtime playback
    /// resumes where it was (the host's position after a bounce).
    BounceThenPlay,
    /// Signal, an offline bounce, then the transport stays stopped until Stop.
    BounceThenStopped,
}

/// The closed records of the two instances, once both are closed.
fn closed_records(plugin_data: &Path, instances: [&str; 2]) -> Option<Vec<serde_json::Value>> {
    let mut output = Vec::new();
    if plugin_data.exists() {
        collect(plugin_data, &mut output);
    }
    let closed: Vec<_> = output
        .into_iter()
        .filter(|record| instances.iter().any(|id| record["instance_id"] == *id))
        .filter(|record| record["status"] == "closed")
        .collect();
    instances
        .iter()
        .all(|id| closed.iter().any(|record| record["instance_id"] == *id))
        .then_some(closed)
}

fn keep_then_stop(plugin_data: &Path, tag: &str, keep: Keep) {
    let (pre_id, post_id, pair) = (
        format!("{tag}-pre"),
        format!("{tag}-post"),
        format!("{tag}-mix"),
    );
    let pre = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    pre.set_identity(
        pre_id.clone(),
        format!("{tag}-project"),
        "".into(),
        pair.clone(),
    );
    pre.enable_pre_writes();
    let post = KirinHyphaEngine::new(48_000, ChannelLayout::stereo());
    post.set_identity(
        post_id.clone(),
        format!("{tag}-project"),
        "".into(),
        "output".into(),
    );
    post.enable_post_writes();
    post.set_pair_target(pair);
    let mut host = Host {
        position: 0,
        counter: 0,
        blocks: 0,
        start: Instant::now(),
        cycle: false,
        wrapped: false,
    };
    // The pair is found from the PRE's Watch publication; Keep once it is.
    let deadline = Instant::now() + CATCH_UP;
    let mut played = 0;
    while played < 150 || !post.keep() {
        assert!(
            Instant::now() < deadline,
            "{tag}: unique private PRE accepted Keep: {:?}",
            post.drain_keep_action_notice()
        );
        host.play(&pre, &post, LOUD);
        played += 1;
    }
    host.arm(&pre, &post, LOUD);
    let mark = host.mark();
    for _ in 0..SIGNAL_BLOCKS {
        if matches!(keep, Keep::Signal) {
            host.play_measured(&pre, &post, 0.0, &mark);
        } else {
            callback(&mut host, &pre, &post, LOUD, false);
        }
    }
    match keep {
        Keep::Signal => {}
        Keep::SilentTail => {
            for _ in 0..SILENT_TAIL_BLOCKS {
                callback(&mut host, &pre, &post, 0.0, false);
            }
        }
        Keep::BounceThenPlay | Keep::BounceThenStopped => {
            let resume = host.position;
            host.position = 0;
            for _ in 0..BOUNCE_BLOCKS {
                callback(&mut host, &pre, &post, LOUD, true);
            }
            host.position = resume;
            if matches!(keep, Keep::BounceThenPlay) {
                for _ in 0..AFTER_BOUNCE_BLOCKS {
                    callback(&mut host, &pre, &post, LOUD, false);
                }
            } else {
                host.stopped(&pre, &post, AFTER_BOUNCE_BLOCKS);
            }
        }
    }
    post.stop();

    // The transport stops; both Records drain, seal and close while the host calls on.
    let deadline = Instant::now() + CATCH_UP;
    let records = loop {
        host.stopped(&pre, &post, CLOSE_READ_BLOCKS);
        if let Some(records) = closed_records(plugin_data, [&pre_id, &post_id]) {
            break records;
        }
        assert!(Instant::now() < deadline, "{tag}: both Records close");
    };
    for record in &records {
        let reasons = record["integrity_reasons"].to_string();
        assert!(
            !reasons.contains("drain_seal_timeout"),
            "{tag}: the drain sealed: {reasons}"
        );
    }
    let shown = loop {
        // The editor's read never waits; a busy display is read again on the next callback.
        if let Some(shown) = post.poll_record_display() {
            break shown;
        }
        host.stopped(&pre, &post, 1);
    };
    assert_eq!(
        shown.status,
        RecordDisplayStatus::Finalized,
        "{tag}: a closed Keep shows its result, never RECORD FINALIZING"
    );
    if matches!(keep, Keep::Signal) {
        let delta = shown
            .delta
            .as_ref()
            .expect("the paired Keep carries a delta");
        eprintln!(
            "changing-peak result: {:?}",
            [delta.lufs, delta.tp, delta.crest, delta.psr]
        );
        for value in [delta.lufs, delta.tp, delta.crest, delta.psr] {
            assert!(
                value.is_some_and(|v| v.abs() < 1e-6),
                "identical windows have zero delta: {value:?}"
            );
        }
    }
    assert!(
        shown.summary.and_then(|summary| summary.lufs_i).is_some(),
        "{tag}: the result carries the Keep's integrated loudness"
    );
}

#[test]
#[ignore = "slow: real PRE/POST Keep, Stop and writer close after a silent tail or a bounce"]
fn a_stopped_keep_shows_its_result_after_a_silent_tail_or_a_bounce() {
    let sandbox = tempfile::tempdir().unwrap();
    let _storage = keep_loop_host::StorageRoot::set(sandbox.path());
    let paths = kirin_measure::PlatformPaths::default_current()
        .unwrap()
        .storage;
    assert!(
        paths.kirin_os_root.starts_with(sandbox.path()),
        "test storage stays in the sandbox"
    );
    std::fs::create_dir_all(&paths.kirin_os_root).unwrap();
    std::fs::write(paths.primary_path(), br#"{"schema_version":"1.0","installation_id":"keep-stop-display-fixture","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#).unwrap();
    let plugin_data = paths.plugin_data_dir();
    for (tag, keep) in [
        ("sig", Keep::Signal),
        ("tail", Keep::SilentTail),
        ("bplay", Keep::BounceThenPlay),
        ("bstop", Keep::BounceThenStopped),
    ] {
        keep_then_stop(&plugin_data, tag, keep);
    }
}
