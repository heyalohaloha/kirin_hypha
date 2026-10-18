//! Real paired Keep while the DAW plays a realtime LOOP. Every wrap starts a new capture epoch;
//! the live meters and the Keep summary must follow every captured frame, never only the first
//! pass, and the record lists every pass. Simulated clock facts (a project timeline that wraps and
//! a continuous host counter), independent of actual DAW acceptance. A post-bounce position jump
//! keeps its own contract.
use kirin_hypha_ffi::{
    channel_abi::ChannelLayout, kirin_hypha_poll_observatory_frame, KirinHyphaEngine,
    KirinObservatoryFrame, KIRIN_KEEP_PHASE_ARMED, KIRIN_KEEP_PHASE_IDLE,
};
use kirin_measure::MeterHistoryResolution;
use kirin_measure::{
    AuxiliaryClockSamples, AuxiliaryClockSource, CaptureClockSource, PresentationLatencySamples,
    PresentationLatencySource, RecordTakeBlock,
};
use std::{
    path::Path,
    thread::sleep,
    time::{Duration, Instant},
};

const RATE: f64 = 48_000.0;
const BLOCK: i64 = 512;
const LOOP_END: i64 = 96_000; // a two-second cycle that starts at the project origin
const QUIET: f32 = 0.1;
const LOUD: f32 = 0.5;

struct Host {
    position: i64,
    counter: i64,
    blocks: i64,
    start: Instant,
    cycle: bool,
}

impl Host {
    /// One realtime callback on both sides, paced to audio time. Returns true at a cycle wrap.
    fn play(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, level: f32) -> bool {
        let block = if level > 0.0 {
            tone(level, self.counter)
        } else {
            impulses(self.counter)
        };
        for engine in [pre, post] {
            push(engine, &block, self.position, self.counter);
        }
        self.counter += BLOCK;
        self.position += BLOCK;
        let wrapped = self.cycle && self.position >= LOOP_END;
        if wrapped {
            self.position -= LOOP_END;
        }
        self.blocks += 1;
        let due = self.start + Duration::from_secs_f64(self.blocks as f64 * BLOCK as f64 / RATE);
        if due > Instant::now() {
            sleep(due - Instant::now());
        }
        wrapped
    }
}

impl Host {
    /// Stopped transport callbacks: the host clock runs on, nothing is captured.
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
                    num_frames: BLOCK as u64,
                    clock_start_samples: 0,
                    clock_end_samples: None,
                });
                engine.push_samples(&[], 2);
            }
            self.counter += BLOCK;
            self.blocks += 1;
            let due =
                self.start + Duration::from_secs_f64(self.blocks as f64 * BLOCK as f64 / RATE);
            if due > Instant::now() {
                sleep(due - Instant::now());
            }
        }
    }

    /// Plays until both sides record a Keep the POST accepted.
    fn arm(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, level: f32) {
        let deadline = Instant::now() + Duration::from_secs(8);
        while post.keep_phase() != KIRIN_KEEP_PHASE_ARMED
            || !post.is_recording()
            || !pre.is_recording()
        {
            assert!(
                Instant::now() < deadline,
                "ARMED deadline: phase {} POST {} PRE {}",
                post.keep_phase(),
                post.is_recording(),
                pre.is_recording()
            );
            self.play(pre, post, level);
        }
    }

    /// After a Keep the pair plays on from `position`: each side's TIME points name the audio they
    /// describe and the LEVEL Δ of identical PRE/POST audio is zero; a joined Δ of different
    /// windows is a wrong pair.
    fn plays_on_after_keep(
        &mut self,
        pre: &KirinHyphaEngine,
        post: &KirinHyphaEngine,
        position: i64,
    ) {
        self.cycle = false;
        self.position = position;
        let mut zero_after = DeltaReturn::default();
        let mut endpoints = [None, None];
        for _ in 0..470 {
            self.play(pre, post, 0.0);
            // The history read never waits for the Measure Thread; a busy read keeps the last one.
            for (side, engine) in [pre, post].into_iter().enumerate() {
                if let Some(endpoint) = latest_endpoint(engine) {
                    endpoints[side] = Some((endpoint, self.position));
                }
            }
            zero_after.note(zero_delta(post));
        }
        for (role, observed) in ["PRE", "POST"].into_iter().zip(endpoints) {
            let (endpoint, played) = observed.expect("TIME endpoint after the Keep");
            assert!(
                endpoint > position && endpoint <= played,
                "{role} TIME points name the audio after the Keep: {endpoint} not in ({position}, {played}]"
            );
        }
        zero_after.assert_returned("LEVEL Δ returns after the Keep");
    }
}

fn tone(level: f32, counter: i64) -> Vec<f32> {
    (0..BLOCK)
        .flat_map(|i| {
            let phase = std::f64::consts::TAU * 1_000.0 * (counter + i) as f64 / RATE;
            let x = level * phase.sin() as f32;
            [x, x]
        })
        .collect()
}

/// Peaks of changing height every 50 ms: TP and CREST differ between any two 400 ms windows that
/// are not the same samples, so a misaligned PRE/POST join cannot read as zero.
fn impulses(counter: i64) -> Vec<f32> {
    (0..BLOCK)
        .flat_map(|i| {
            let n = counter + i;
            let x = if n % 2_400 == 0 {
                0.2 + 0.07 * ((n / 2_400) % 11) as f32
            } else {
                0.0
            };
            [x, x]
        })
        .collect()
}

/// How a matched LEVEL Δ shows up over a run of polls. A busy poll reads nothing, so the count
/// varies with host load; the Δ must return and still be there at the end of the run.
#[derive(Default)]
struct DeltaReturn {
    polls: usize,
    zero: usize,
    last: usize,
}

impl DeltaReturn {
    fn note(&mut self, zero: bool) {
        self.polls += 1;
        if zero {
            self.zero += 1;
            self.last = self.polls;
        }
    }

    fn assert_returned(&self, what: &str) {
        assert!(
            self.zero >= 20 && self.last + 100 >= self.polls,
            "{what}: {} zero readings in {} polls, the last at {}",
            self.zero,
            self.polls,
            self.last
        );
    }
}

/// True for an available LEVEL Δ; any available Δ of identical PRE/POST audio must be zero
/// (floating-point noise only), never the ±1 dB of two different 400 ms windows.
fn zero_delta(post: &KirinHyphaEngine) -> bool {
    let mut frame = unsafe { std::mem::zeroed::<KirinObservatoryFrame>() };
    let polled = unsafe {
        kirin_hypha_poll_observatory_frame(std::ptr::from_ref(post).cast_mut(), &mut frame)
    };
    if !polled || frame.delta_available == 0 || !frame.delta.true_peak.is_finite() {
        return false;
    }
    let d = frame.delta;
    assert!(
        d.true_peak.abs() < 1.0e-6 && d.crest.abs() < 1.0e-6 && d.lufs.abs() < 1.0e-6,
        "identical PRE/POST audio joins only the same windows: TP {} CREST {} M {}",
        d.true_peak,
        d.crest,
        d.lufs
    );
    true
}

fn latest_endpoint(engine: &KirinHyphaEngine) -> Option<i64> {
    engine
        .poll_meter_history(MeterHistoryResolution::Hz10, 4)?
        .last()?
        .last_timeline_endpoint_samples
}

fn push(engine: &KirinHyphaEngine, samples: &[f32], position: i64, counter: i64) {
    let recording = engine.is_recording();
    engine.set_signal_state(1);
    engine.note_record_block(RecordTakeBlock {
        generation: 0,
        recording,
        rendered: recording,
        playing: true,
        offline: false,
        position_valid: true,
        position_samples: position,
        num_frames: BLOCK as u64,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    engine.note_capture_window_with_clocks(
        true,
        position,
        BLOCK as u64,
        CaptureClockSource::ProjectTimeline,
        PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(0),
        },
        AuxiliaryClockSamples {
            source: AuxiliaryClockSource::Vst3Continuous,
            samples: Some(counter),
        },
        false,
    );
    engine.push_samples(samples, 2);
}

fn collect(path: &Path, results: &mut Vec<serde_json::Value>) {
    for entry in std::fs::read_dir(path).unwrap() {
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
    for _ in 0..300 {
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
    keep_delta.assert_returned("the matched LEVEL Δ stays available through the Keep");
    assert!(wraps >= 3, "the Keep spans several cycle wraps");

    // Live display: the meter session catches up with every frame played in the Keep (the
    // Measure Thread may lag; it never stops at a wrap), and the live LEVEL follows the audio.
    let mut advanced = 0;
    for _ in 0..375 {
        advanced = post
            .poll_meter_session()
            .map_or(advanced, |m| m.active_frames - meter_before);
        if advanced >= keep_frames {
            break;
        }
        host.play(&pre, &post, LOUD);
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
    assert!(post.keep(), "a Keep started while stopped");
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
