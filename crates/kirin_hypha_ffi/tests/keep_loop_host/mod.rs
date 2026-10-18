//! The simulated host, signals and readers shared by the realtime Keep LOOP test.
use kirin_hypha_ffi::{
    kirin_hypha_poll_observatory_frame, KirinHyphaEngine, KirinObservatoryFrame,
    KIRIN_KEEP_PHASE_ARMED, KIRIN_KEEP_PHASE_IDLE,
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

pub const RATE: f64 = 48_000.0;
pub const BLOCK: i64 = 512;
pub const LOOP_END: i64 = 96_000; // a two-second cycle that starts at the project origin
pub const QUIET: f32 = 0.1;
pub const LOUD: f32 = 0.5;

pub struct Host {
    pub position: i64,
    pub counter: i64,
    pub blocks: i64,
    pub start: Instant,
    pub cycle: bool,
    /// The next callback begins with the host's own LOOP wrap.
    pub wrapped: bool,
}

impl Host {
    /// One realtime callback on both sides, paced to audio time. Returns true at a cycle wrap.
    pub fn play(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, level: f32) -> bool {
        let block = if level > 0.0 {
            tone(level, self.counter)
        } else {
            impulses(self.counter)
        };
        for engine in [pre, post] {
            engine.note_capture_loop_wrap(self.wrapped);
            push(engine, &block, self.position, self.counter);
        }
        self.counter += BLOCK;
        self.position += BLOCK;
        let wrapped = self.cycle && self.position >= LOOP_END;
        if wrapped {
            self.position -= LOOP_END;
        }
        self.wrapped = wrapped;
        self.blocks += 1;
        let due = self.start + Duration::from_secs_f64(self.blocks as f64 * BLOCK as f64 / RATE);
        if due > Instant::now() {
            sleep(due - Instant::now());
        }
        wrapped
    }
}

impl Host {
    pub fn mark(&self) -> Mark {
        Mark {
            blocks: self.blocks,
            position: self.position,
        }
    }

    /// Played blocks since `mark`.
    pub fn played_since(&self, mark: &Mark) -> i64 {
        self.blocks - mark.blocks
    }

    /// One callback of the linear run that began at `mark`, paced by the measurement instead of
    /// the wall clock: while either side's latest TIME endpoint in this run is more than the slack
    /// behind the host position (audio its meter has not measured yet, however long it has been
    /// queued), the host sends only heartbeat callbacks without audio; positions and host clock
    /// stay continuous. A slow host (a virtual CI machine, background QoS) is then slowed to its own
    /// measurement, so the Δ checks always run on fresh points; a meter that stops advancing still
    /// fails the wait.
    pub fn play_measured(
        &mut self,
        pre: &KirinHyphaEngine,
        post: &KirinHyphaEngine,
        level: f32,
        mark: &Mark,
    ) -> bool {
        let mut stalled = 0;
        loop {
            // A busy meter (one measuring a backlog) refuses a read, so it is read again.
            let endpoints = [settled_endpoint(pre), settled_endpoint(post)];
            let behind = endpoints.iter().any(|endpoint| {
                let measured = endpoint
                    .filter(|end| *end >= mark.position)
                    .unwrap_or(mark.position);
                measured + MEASURE_SLACK < self.position
            });
            if !behind {
                break;
            }
            // A heartbeat-only callback keeps both sides active while no audio is delivered.
            for engine in [pre, post] {
                engine.set_signal_state(1);
                engine.push_samples(&[], 2);
            }
            sleep(HOLD);
            self.start += HOLD;
            let advanced = [settled_endpoint(pre), settled_endpoint(post)] != endpoints;
            stalled = if advanced { 0 } else { stalled + 1 };
            assert!(
                stalled < STALLED_HOLDS,
                "a meter stopped measuring the played audio"
            );
        }
        self.play(pre, post, level)
    }

    /// Stopped transport callbacks: the host clock runs on, nothing is captured.
    pub fn stopped(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, blocks: i64) {
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

    /// Plays until both sides record a Keep the POST accepted. A host too slow for the producer
    /// readiness deadline gets "Failed to arm PRE and POST": both sides must then be back in Watch
    /// (no Record survives an aborted Keep) before the Keep is pressed again, as a user would.
    pub fn arm(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, level: f32) {
        let give_up = Instant::now() + CATCH_UP;
        let mut deadline = Instant::now() + Duration::from_secs(8);
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
            if post.keep_phase() == KIRIN_KEEP_PHASE_IDLE
                && post.drain_keep_action_notice().as_deref() == Some("Failed to arm PRE and POST")
            {
                assert!(Instant::now() < give_up, "the Keep keeps failing to arm");
                let rollback = Instant::now() + Duration::from_secs(8);
                while post.is_recording() || pre.is_recording() {
                    assert!(
                        Instant::now() < rollback,
                        "an aborted Keep leaves no Record: POST {} PRE {}",
                        post.is_recording(),
                        pre.is_recording()
                    );
                    self.play(pre, post, level);
                }
                while !post.keep() {
                    assert!(Instant::now() < rollback, "Keep again after a failed arm");
                    self.play(pre, post, level);
                }
                deadline = Instant::now() + Duration::from_secs(8);
            }
            self.play(pre, post, level);
        }
    }

    /// After a Keep the pair plays on from `position`: each side's TIME points name the audio they
    /// describe and the LEVEL Δ of identical PRE/POST audio is zero; a joined Δ of different
    /// windows is a wrong pair. The run is paced by the measurement, so a slow host checks it too.
    pub fn plays_on_after_keep(
        &mut self,
        pre: &KirinHyphaEngine,
        post: &KirinHyphaEngine,
        position: i64,
    ) {
        self.cycle = false;
        self.position = position;
        self.wrapped = false;
        let mut zero_after = DeltaReturn::default();
        let mut endpoints = [None, None];
        let mark = self.mark();
        while zero_after.polls < 470
            || (!zero_after.returned() && self.played_since(&mark) < MEASURED_RUN_BLOCKS)
        {
            self.play_measured(pre, post, 0.0, &mark);
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

pub fn tone(level: f32, counter: i64) -> Vec<f32> {
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
pub fn impulses(counter: i64) -> Vec<f32> {
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

/// A slow host (a virtual CI machine, background QoS) lags the Measure Thread far behind the
/// audio, and a stale TIME point is never compared. The arming waits are bounded by this
/// wall-clock budget instead of a block count.
pub const CATCH_UP: Duration = Duration::from_secs(30);

/// Played audio a meter may still have to measure before the host holds its next callback.
const MEASURE_SLACK: i64 = 24_000;
/// One host stall between heartbeat-only callbacks, shorter than the 400 ms liveness window.
const HOLD: Duration = Duration::from_millis(100);
/// Consecutive stalls without any meter advancing before the wait reports a stopped meter.
const STALLED_HOLDS: u32 = 100;
/// Audio a measurement-paced run may play before it must have seen the Δ return.
pub const MEASURED_RUN_BLOCKS: i64 = 30 * RATE as i64 / BLOCK;

/// Where a measurement-paced linear run starts: its host callback and position.
pub struct Mark {
    blocks: i64,
    position: i64,
}

/// How a matched LEVEL Δ shows up over a run of polls. A busy poll reads nothing, so the count
/// varies with host load; the Δ must return and still be there at the end of the run.
#[derive(Default)]
pub struct DeltaReturn {
    pub polls: usize,
    pub zero: usize,
    pub last: usize,
}

impl DeltaReturn {
    pub fn note(&mut self, zero: bool) {
        self.polls += 1;
        if zero {
            self.zero += 1;
            self.last = self.polls;
        }
    }

    pub fn returned(&self) -> bool {
        self.zero >= 20 && self.last + 100 >= self.polls
    }

    pub fn assert_returned(&self, what: &str) {
        assert!(
            self.returned(),
            "{what}: {} zero readings in {} polls, the last at {}",
            self.zero,
            self.polls,
            self.last
        );
    }
}

/// True for an available LEVEL Δ; any available Δ of identical PRE/POST audio must be zero
/// (floating-point noise only), never the ±1 dB of two different 400 ms windows.
pub fn zero_delta(post: &KirinHyphaEngine) -> bool {
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

/// The latest TIME endpoint, read again (without sleeping) while the meter is busy.
fn settled_endpoint(engine: &KirinHyphaEngine) -> Option<i64> {
    (0..1_000).find_map(|_| {
        let endpoint = latest_endpoint(engine);
        if endpoint.is_none() {
            std::thread::yield_now();
        }
        endpoint
    })
}

pub fn latest_endpoint(engine: &KirinHyphaEngine) -> Option<i64> {
    engine
        .poll_meter_history(MeterHistoryResolution::Hz10, 4)?
        .last()?
        .last_timeline_endpoint_samples
}

pub fn push(engine: &KirinHyphaEngine, samples: &[f32], position: i64, counter: i64) {
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

pub fn collect(path: &Path, results: &mut Vec<serde_json::Value>) {
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
