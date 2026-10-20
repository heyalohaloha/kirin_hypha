//! Real paired Keep whose TRACE pass ends before the Keep does (a LOOP wrap, a seek, a stop and a
//! new start) at every supported sample rate other than 48 kHz (44.1 to 192 kHz). Phase D measures
//! a 48 kHz resampled stream; the last 100 ms of the TRACE pass must still reach its TRACE slot,
//! so the record keeps every slot and a normal pass end degrades nothing. 48 kHz (no resampler) is
//! the control. Simulated clock facts, independent of actual DAW acceptance.
use kirin_hypha_ffi::{
    channel_abi::ChannelLayout, KirinHyphaEngine, KIRIN_KEEP_PHASE_ARMED, KIRIN_KEEP_PHASE_IDLE,
};
use kirin_measure::{
    AuxiliaryClockSamples, AuxiliaryClockSource, CaptureClockSource, MeterHistoryResolution,
    PresentationLatencySamples, PresentationLatencySource, RecordTakeBlock,
};
use std::path::Path;
use std::thread::sleep;
use std::time::{Duration, Instant};

/// How the TRACE pass ends while the Keep goes on.
#[derive(Clone, Copy, Debug)]
enum PassEnd {
    /// The host's LOOP returns to the project start.
    LoopWrap,
    /// The user seeks back while the transport plays.
    Seek,
    /// The transport stops, then plays again from the project start.
    StopRestart,
    /// The host plays a LOOP; Keep is pressed just before its end and arms after the wrap, so the
    /// TRACE pass starts within one lap and ends at the next wrap.
    KeepNearLoopEnd,
}

/// A host playing 10 ms callbacks at `rate`, paced to audio time.
struct Host {
    rate: u32,
    position: i64,
    counter: i64,
    blocks: i64,
    start: Instant,
    /// The host's LOOP end, if it plays one from the project start.
    loop_end: Option<i64>,
    /// The next callback begins with the host's own LOOP wrap.
    wrapped: bool,
}

impl Host {
    fn block(&self) -> i64 {
        i64::from(self.rate / 100)
    }

    fn pace(&mut self) {
        self.counter += self.block();
        self.blocks += 1;
        let due = self.start
            + Duration::from_secs_f64((self.blocks * self.block()) as f64 / f64::from(self.rate));
        if due > Instant::now() {
            sleep(due - Instant::now());
        }
    }

    /// One playing callback of a tone on both sides; `wrap` names the host's own LOOP wrap.
    fn play(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, wrap: bool) {
        let wrap = wrap || std::mem::take(&mut self.wrapped);
        let frames = self.block();
        let block: Vec<f32> = (0..frames)
            .flat_map(|i| {
                let phase = std::f64::consts::TAU * 997.0 * (self.counter + i) as f64
                    / f64::from(self.rate);
                let x = 0.3 * phase.sin() as f32;
                [x, x]
            })
            .collect();
        for engine in [pre, post] {
            let recording = engine.is_recording();
            engine.set_signal_state(1);
            engine.note_capture_loop_wrap(wrap);
            engine.note_record_block(RecordTakeBlock {
                generation: 0,
                recording,
                rendered: recording,
                playing: true,
                offline: false,
                position_valid: true,
                position_samples: self.position,
                num_frames: frames as u64,
                clock_start_samples: 0,
                clock_end_samples: None,
            });
            engine.note_capture_window_with_clocks(
                true,
                self.position,
                frames as u64,
                CaptureClockSource::ProjectTimeline,
                PresentationLatencySamples {
                    source: PresentationLatencySource::Vst3,
                    input: Some(0),
                    output: Some(0),
                },
                AuxiliaryClockSamples {
                    source: AuxiliaryClockSource::Vst3Continuous,
                    samples: Some(self.counter),
                },
                false,
            );
            engine.push_samples(&block, 2);
        }
        self.position += frames;
        if self.loop_end.is_some_and(|end| self.position >= end) {
            self.position = 0;
            self.wrapped = true;
        }
        self.pace();
    }

    /// Heartbeat-only callbacks paced to audio time, without audio and without advancing the host
    /// clock, until both sides measured what was played (their latest TIME endpoint is within
    /// half a second of the host position). A slow host (a debug build, a loaded runner) measures
    /// behind the host; the Stop then judges the pass end, not the drain's own pace.
    fn catch_up(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, tag: &str) {
        let deadline = Instant::now() + Duration::from_secs(60);
        let hold = Duration::from_secs_f64(self.block() as f64 / f64::from(self.rate));
        let measured = |engine: &KirinHyphaEngine| {
            engine
                .poll_meter_history(MeterHistoryResolution::Hz10, 4)
                .and_then(|history| history.last()?.last_timeline_endpoint_samples)
                .is_some_and(|end| end + i64::from(self.rate / 2) >= self.position)
        };
        while !measured(pre) || !measured(post) {
            assert!(
                Instant::now() < deadline,
                "{tag}: the meters catch up with the host"
            );
            for engine in [pre, post] {
                engine.set_signal_state(1);
                engine.push_samples(&[], 2);
            }
            sleep(hold);
            self.start += hold;
        }
    }

    /// Stopped-transport callbacks: the host calls on, nothing is captured.
    fn stopped(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, blocks: i64) {
        for _ in 0..blocks {
            for engine in [pre, post] {
                engine.set_signal_state(0);
                engine.note_record_block(RecordTakeBlock {
                    generation: 0,
                    recording: engine.is_recording(),
                    rendered: false,
                    playing: false,
                    offline: false,
                    position_valid: true,
                    position_samples: self.position,
                    num_frames: self.block() as u64,
                    clock_start_samples: 0,
                    clock_end_samples: None,
                });
                engine.push_samples(&[], 2);
            }
            self.pace();
        }
    }
}

fn epoch_ms() -> i64 {
    std::time::SystemTime::now()
        .duration_since(std::time::UNIX_EPOCH)
        .map_or(0, |elapsed| elapsed.as_millis() as i64)
}

fn collect(path: &Path, results: &mut Vec<serde_json::Value>) {
    let Ok(entries) = std::fs::read_dir(path) else {
        return;
    };
    for entry in entries {
        let p = entry.unwrap().path();
        if p.is_dir() {
            collect(&p, results)
        } else if p.extension().is_some_and(|x| x == "json") {
            if let Ok(v) = serde_json::from_slice::<serde_json::Value>(&std::fs::read(p).unwrap()) {
                if v["schema_version"] == "1.3" && v["frames"].is_array() {
                    results.push(v)
                }
            }
        }
    }
}

/// The closed records of the two instances for the Keep pressed at `keep_ms` (a Keep that failed
/// to arm before it leaves a short record of its own), once both are closed.
fn closed_records(
    plugin_data: &Path,
    instances: [&str; 2],
    keep_ms: i64,
) -> Option<Vec<serde_json::Value>> {
    let mut output = Vec::new();
    collect(plugin_data, &mut output);
    let closed: Vec<_> = output
        .into_iter()
        .filter(|record| instances.iter().any(|id| record["instance_id"] == *id))
        .filter(|record| record["status"] == "closed")
        .filter(|record| {
            record["started_at_ms"]
                .as_i64()
                .is_some_and(|ms| ms >= keep_ms)
        })
        .collect();
    instances
        .iter()
        .all(|id| closed.iter().any(|record| record["instance_id"] == *id))
        .then_some(closed)
}

/// The ways the Keep's records fail the pass end, if any.
fn keep_whose_trace_pass_ends(
    plugin_data: &Path,
    rate: u32,
    end: PassEnd,
    tag: &str,
) -> Vec<String> {
    let (pre_id, post_id, pair) = (
        format!("{tag}-pre"),
        format!("{tag}-post"),
        format!("{tag}-mix"),
    );
    let pre = KirinHyphaEngine::new(rate, ChannelLayout::stereo());
    pre.set_identity(
        pre_id.clone(),
        format!("{tag}-project"),
        "".into(),
        pair.clone(),
    );
    pre.enable_pre_writes();
    let post = KirinHyphaEngine::new(rate, ChannelLayout::stereo());
    post.set_identity(
        post_id.clone(),
        format!("{tag}-project"),
        "".into(),
        "output".into(),
    );
    post.enable_post_writes();
    post.set_pair_target(pair);
    let mut host = Host {
        rate,
        position: 0,
        counter: 0,
        blocks: 0,
        start: Instant::now(),
        loop_end: matches!(end, PassEnd::KeepNearLoopEnd).then_some(2 * i64::from(rate)),
        wrapped: false,
    };
    let deadline = Instant::now() + Duration::from_secs(30);
    let mut played = 0;
    // Near the LOOP end Keep is pressed 150 ms before the wrap.
    let press_at = host.loop_end.map(|end| end - 15 * host.block());
    while played < 150 || press_at.is_some_and(|press| host.position != press) || !post.keep() {
        assert!(Instant::now() < deadline, "{tag}: Keep accepted");
        host.play(&pre, &post, false);
        played += 1;
    }
    let mut keep_ms = epoch_ms() - 500;
    // A host too slow for the producer readiness deadline gets "Failed to arm PRE and POST": both
    // sides return to Watch and the Keep is pressed again, as a user would.
    let give_up = Instant::now() + Duration::from_secs(30);
    let mut deadline = Instant::now() + Duration::from_secs(8);
    let mut pressed_again = false;
    while post.keep_phase() != KIRIN_KEEP_PHASE_ARMED || !post.is_recording() || !pre.is_recording()
    {
        assert!(Instant::now() < deadline, "{tag}: ARMED");
        if post.keep_phase() == KIRIN_KEEP_PHASE_IDLE
            && post.drain_keep_action_notice().as_deref() == Some("Failed to arm PRE and POST")
        {
            assert!(
                Instant::now() < give_up,
                "{tag}: the Keep keeps failing to arm"
            );
            while post.is_recording() || pre.is_recording() || !post.keep() {
                assert!(
                    Instant::now() < give_up,
                    "{tag}: Keep again after a failed arm"
                );
                host.play(&pre, &post, false);
            }
            keep_ms = epoch_ms() - 500;
            pressed_again = true;
            deadline = Instant::now() + Duration::from_secs(8);
        }
        host.play(&pre, &post, false);
    }
    // An arming that lands on the LOOP's last callback starts the take after the wrap: it still
    // plays to the next wrap.
    if host.wrapped {
        host.play(&pre, &post, false);
    }
    // The TRACE pass plays on for over a second and ends on the 100 ms grid (ten callbacks).
    let pass_end = match host.loop_end {
        Some(loop_end) => loop_end,
        None => (host.position / (10 * host.block()) + 13) * 10 * host.block(),
    };
    while host.position < pass_end && !(host.loop_end.is_some() && host.wrapped) {
        host.play(&pre, &post, false);
    }
    match end {
        PassEnd::KeepNearLoopEnd => {}
        PassEnd::LoopWrap => {
            host.position = 0;
            host.play(&pre, &post, true);
        }
        PassEnd::Seek => host.position = pass_end / 2,
        PassEnd::StopRestart => {
            host.stopped(&pre, &post, 30);
            host.position = 0;
        }
    }
    // A later pass of the same Keep: the live meters and the summary only.
    for _ in 0..100 {
        host.play(&pre, &post, false);
    }
    host.catch_up(&pre, &post, tag);
    post.stop();
    let deadline = Instant::now() + Duration::from_secs(30);
    let records = loop {
        host.stopped(&pre, &post, 25);
        if let Some(records) = closed_records(plugin_data, [&pre_id, &post_id], keep_ms) {
            break records;
        }
        assert!(Instant::now() < deadline, "{tag}: both Records close");
    };
    let mut failures = Vec::new();
    for record in &records {
        let role = record["role"].as_str().unwrap_or("?");
        let passes = record["keep_passes"].as_array();
        assert!(
            passes.is_some_and(|passes| passes.len() >= 2
                && passes[0]["in_trace"] == true
                && passes[0]["host_end_position_samples"] == pass_end),
            "{tag} {role}: the TRACE pass ended at {pass_end} and the Keep went on: {:?}",
            record["keep_passes"]
        );
        if record["trace_diagnostics"]["missing_slots"] != 0
            || record["integrity_degraded"] != false
        {
            failures.push(format!(
                "{tag} {role} ({rate} Hz, {end:?}, pressed again {pressed_again}): {} {} \
                 frames={} first pass={} take={}",
                record["trace_diagnostics"],
                record["integrity_reasons"],
                record["frames"].as_array().map_or(0, Vec::len),
                record["keep_passes"][0],
                record["bounce_take"]
            ));
        }
    }
    failures
}

#[test]
#[ignore = "slow: real PRE/POST Keeps from 44.1 to 192 kHz whose TRACE pass ends early"]
fn a_trace_pass_that_ends_before_the_keep_keeps_its_last_slot_at_every_rate() {
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
    std::fs::write(paths.primary_path(), br#"{"schema_version":"1.0","installation_id":"keep-pass-end-fixture","hardware_id":"hw","hardware_components":{"iop":"a","sn":"b","bd":"c"},"machine_signature":"sig","license":"os","created_at":"2026-06-09T00:00:00Z","last_verified_at":"2026-06-09T00:00:00Z"}"#).unwrap();
    let plugin_data = paths.plugin_data_dir();
    let failures = [
        (96_000, PassEnd::LoopWrap, "w96"),
        (96_000, PassEnd::Seek, "s96"),
        (96_000, PassEnd::StopRestart, "r96"),
        (44_100, PassEnd::LoopWrap, "w44"),
        (44_100, PassEnd::Seek, "s44"),
        (44_100, PassEnd::StopRestart, "r44"),
        (96_000, PassEnd::KeepNearLoopEnd, "n96"),
        (44_100, PassEnd::KeepNearLoopEnd, "n44"),
        (48_000, PassEnd::KeepNearLoopEnd, "n48"),
        (48_000, PassEnd::LoopWrap, "w48"),
        (88_200, PassEnd::LoopWrap, "w88"),
        (88_200, PassEnd::Seek, "s88"),
        (88_200, PassEnd::StopRestart, "r88"),
        (176_400, PassEnd::LoopWrap, "w176"),
        (176_400, PassEnd::Seek, "s176"),
        (176_400, PassEnd::StopRestart, "r176"),
        (192_000, PassEnd::LoopWrap, "w192"),
        (192_000, PassEnd::Seek, "s192"),
        (192_000, PassEnd::StopRestart, "r192"),
        (192_000, PassEnd::KeepNearLoopEnd, "n192"),
    ]
    .into_iter()
    .flat_map(|(rate, end, tag)| keep_whose_trace_pass_ends(&plugin_data, rate, end, tag))
    .collect::<Vec<_>>();
    // Every TRACE slot of the pass, also its last 100 ms, and a normal pass end degrades nothing.
    assert!(failures.is_empty(), "{failures:#?}");
}
