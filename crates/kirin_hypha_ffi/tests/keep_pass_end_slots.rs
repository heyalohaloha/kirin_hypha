//! Real paired Keep whose TRACE pass ends before the Keep does (a LOOP wrap, a seek, a stop and a
//! new start) at every supported sample rate other than 48 kHz (44.1 to 192 kHz). Phase D measures
//! a 48 kHz resampled stream; the last 100 ms of the TRACE pass must still reach its TRACE slot,
//! so the record keeps every slot and a normal pass end degrades nothing. 48 kHz (no resampler) is
//! the control. Simulated clock facts, independent of actual DAW acceptance.
#[allow(dead_code)]
mod keep_loop_host;
use keep_loop_host::StorageRoot;
use kirin_hypha_ffi::{channel_abi::ChannelLayout, KirinHyphaEngine};
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
    /// Keep near a LOOP end yields an initial subsecond take to the first complete lap.
    KeepNearLoopEnd,
}

/// A host playing 10 ms callbacks at `rate`, paced to audio time.
struct Host {
    rate: u32,
    position: i64,
    counter: i64,
    played_frames: u64,
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
        self.wait_for_measure(pre, post);
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
        self.played_frames += frames as u64;
        self.position += frames;
        if self.loop_end.is_some_and(|end| self.position >= end) {
            self.position = 0;
            self.wrapped = true;
        }
        self.pace();
    }

    /// Bound admitted audio ahead of each meter by half a second, across every loop/restart.
    fn wait_for_measure(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine) {
        let deadline = Instant::now() + Duration::from_secs(600);
        let hold = Duration::from_millis(100);
        let caught_up = |engine: &KirinHyphaEngine| {
            let accepted = self
                .played_frames
                .saturating_sub(engine.overflow_count() / 2);
            engine.poll_meter_session().is_some_and(|meter| {
                meter.active_frames.saturating_add(u64::from(self.rate / 2)) >= accepted
            })
        };
        while !caught_up(pre) || !caught_up(post) {
            assert!(
                Instant::now() < deadline,
                "meters stopped advancing at {} played frames",
                self.played_frames
            );
            self.hold_clock(pre, post, true);
            sleep(hold);
            self.start += hold;
        }
    }

    /// Hold the host clock without audio until both Record readers consumed all
    /// admitted audio. A previous lap's larger TIME endpoint cannot prove this after a rewind.
    /// The read-only probe includes both the Audio-to-spool ring and the spool reader cursor;
    /// Stop therefore judges the pass end without racing a loaded worker's remaining backlog.
    fn catch_up(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, tag: &str) {
        let deadline = Instant::now() + Duration::from_secs(600);
        let hold = Duration::from_secs_f64(self.block() as f64 / f64::from(self.rate));
        while !pre.__ring_drained_for_test() || !post.__ring_drained_for_test() {
            assert!(
                Instant::now() < deadline,
                "{tag}: the meters catch up with the host"
            );
            self.hold_clock(pre, post, false);
            sleep(hold);
            self.start += hold;
        }
    }

    /// A stopped callback still lets the producer apply a pending take promotion.
    fn hold_clock(&self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, playing: bool) {
        for engine in [pre, post] {
            engine.set_signal_state(u8::from(playing));
            engine.note_record_block(RecordTakeBlock {
                generation: 0,
                recording: engine.is_recording(),
                rendered: false,
                playing,
                offline: false,
                position_valid: true,
                position_samples: self.position,
                num_frames: 0,
                clock_start_samples: 0,
                clock_end_samples: None,
            });
            engine.push_samples(&[], 2);
        }
    }

    /// Stopped-transport callbacks: the host calls on, nothing is captured.
    fn stopped(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, blocks: i64) {
        for _ in 0..blocks {
            self.hold_clock(pre, post, false);
            self.pace();
        }
    }
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
            if let Ok(mut v) =
                serde_json::from_slice::<serde_json::Value>(&std::fs::read(&p).unwrap())
            {
                if v["schema_version"] == "1.3" && v["frames"].is_array() {
                    v["fixture_record_path"] = p.display().to_string().into();
                    results.push(v)
                }
            }
        }
    }
}

/// The closed records of the exact accepted session, excluding any earlier aborted attempt.
fn closed_records(
    plugin_data: &Path,
    instances: [&str; 2],
    session_id: &str,
) -> Option<Vec<serde_json::Value>> {
    let mut output = Vec::new();
    collect(plugin_data, &mut output);
    let closed: Vec<_> = output
        .into_iter()
        .filter(|record| instances.iter().any(|id| record["instance_id"] == *id))
        .filter(|record| record["status"] == "closed")
        .filter(|record| record["record_session_id"] == session_id)
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
) -> (Vec<String>, bool) {
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
        played_frames: 0,
        blocks: 0,
        start: Instant::now(),
        loop_end: matches!(end, PassEnd::KeepNearLoopEnd).then_some(2 * i64::from(rate)),
        wrapped: false,
    };
    let deadline = Instant::now() + Duration::from_secs(600);
    let mut played = 0;
    // Near the LOOP end Keep is pressed 0.95 seconds before the wrap.
    let press_at = host.loop_end.map(|end| end - 95 * host.block());
    while played < 150 || press_at.is_some_and(|press| host.position != press) || !post.keep() {
        assert!(Instant::now() < deadline, "{tag}: Keep accepted");
        host.play(&pre, &post, false);
        played += 1;
    }
    let attempts = keep_loop_host::arm(&pre, &post, || host.play(&pre, &post, false));
    let first_pass_start = host.position;
    let generation = kirin_measure::capture_generation::read_current_generation(
        plugin_data,
        &format!("{tag}-project"),
    )
    .unwrap()
    .expect("an armed Keep committed its generation");
    let session_id = &generation.members[0].record_session_id;
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
    if matches!(end, PassEnd::KeepNearLoopEnd) {
        while !host.wrapped {
            host.play(&pre, &post, false);
        }
        host.play(&pre, &post, false);
    }
    host.catch_up(&pre, &post, tag);
    eprintln!("{tag}: stopping at {}, armed at {first_pass_start}, attempts {attempts}, PRE {:?}, POST {:?}",
        host.position,
        pre.poll_meter_history(MeterHistoryResolution::Hz10, 1).and_then(|h| h.last().map(|p| (p.run_id, p.last_observed_frames, p.last_timeline_endpoint_samples))),
        post.poll_meter_history(MeterHistoryResolution::Hz10, 1).and_then(|h| h.last().map(|p| (p.run_id, p.last_observed_frames, p.last_timeline_endpoint_samples))));
    post.stop();
    let deadline = Instant::now() + Duration::from_secs(600);
    let records = loop {
        host.stopped(&pre, &post, 25);
        if let Some(records) = closed_records(plugin_data, [&pre_id, &post_id], session_id) {
            break records;
        }
        assert!(Instant::now() < deadline, "{tag}: both Records close");
    };
    let mut failures = Vec::new();
    let mut short_replaced = false;
    for record in &records {
        let role = record["role"].as_str().unwrap_or("?");
        let mut diagnostic = record.clone();
        for field in [
            "frames",
            "trace_clock_observations",
            "trace_slot_positions",
            "trace_raw_host_slot_positions",
            "trace_comparison_slot_positions",
            "psb_snapshots",
        ] {
            let count = diagnostic[field].as_array().map_or(0, Vec::len);
            diagnostic.as_object_mut().unwrap().remove(field);
            diagnostic[format!("{field}_count")] = count.into();
        }
        let passes = record["keep_passes"].as_array();
        if passes.is_none() {
            diagnostic["keep_passes_state"] = "IO snapshot omitted the journal: no non-TRACE pass or no matching-generation chunks were available".into();
        }
        assert!(
            passes.is_some_and(|passes| passes.len() >= 2
                && passes
                    .iter()
                    .filter(|pass| pass["in_trace"] == true)
                    .count()
                    == 1
                && passes.iter().any(|pass| pass["in_trace"] == true
                    && pass["host_end_position_samples"] == pass_end)),
            "{tag} {role} ({rate} Hz, {end:?}, attempts {attempts}, armed at {first_pass_start}): \
             the TRACE pass ended at {pass_end} and the Keep went on: {diagnostic}"
        );
        if matches!(end, PassEnd::KeepNearLoopEnd) {
            let passes = passes.unwrap();
            let selected = passes.iter().find(|pass| pass["in_trace"] == true).unwrap();
            assert!(
                selected["frames"].as_u64().unwrap() >= u64::from(rate),
                "{tag} {role}: selected TRACE must last at least one second: {selected}, armed at {first_pass_start}"
            );
            let short = passes[0]["frames"].as_u64().unwrap() < u64::from(rate);
            if short {
                assert!(
                    passes
                        .iter()
                        .any(|pass| pass["trace_excluded_reason"] == "short_trace_replaced"),
                    "{tag} {role}: a short first lap must yield: {passes:?}"
                );
                short_replaced = true;
            }
        }
        if record["trace_diagnostics"]["missing_slots"] != 0
            || record["integrity_degraded"] != false
        {
            failures.push(format!(
                "{tag} {role} ({rate} Hz, {end:?}, attempts {attempts}): {diagnostic}"
            ));
        }
    }
    (failures, short_replaced)
}

#[test]
#[ignore = "slow: real PRE/POST Keeps from 44.1 to 192 kHz whose TRACE pass ends early"]
fn a_trace_pass_that_ends_before_the_keep_keeps_its_last_slot_at_every_rate() {
    let sandbox = tempfile::tempdir().unwrap();
    let _storage = StorageRoot::set(sandbox.path());
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
    let results = [
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
    .map(|(rate, end, tag)| keep_whose_trace_pass_ends(&plugin_data, rate, end, tag))
    .collect::<Vec<_>>();
    assert!(
        results.iter().any(|(_, replaced)| *replaced),
        "the near-wrap fixtures must exercise a short first lap"
    );
    let failures: Vec<_> = results
        .into_iter()
        .flat_map(|(failures, _)| failures)
        .collect();
    // Every TRACE slot of the pass, also its last 100 ms, and a normal pass end degrades nothing.
    assert!(failures.is_empty(), "{failures:#?}");
}
