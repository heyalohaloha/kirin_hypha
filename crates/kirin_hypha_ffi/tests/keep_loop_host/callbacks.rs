//! Callback availability and clock variants for the simulated host.
use super::*;
#[derive(Clone, Copy, Debug)]
pub enum PreCallback {
    Measured,
    Bypassed,
    NotCalled,
}
#[derive(Clone, Copy)]
pub struct Clock {
    pub latency: PresentationLatencySamples,
    pub auxiliary: AuxiliaryClockSource,
}
impl Default for Clock {
    fn default() -> Self {
        Self {
            latency: PresentationLatencySamples {
                source: PresentationLatencySource::Vst3,
                input: Some(0),
                output: Some(0),
            },
            auxiliary: AuxiliaryClockSource::Vst3Continuous,
        }
    }
}
impl Host {
    /// One realtime callback on both sides, paced to audio time. Returns true at a cycle wrap.
    pub fn play(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, level: f32) -> bool {
        self.play_configured(pre, PreCallback::Measured, post, level, Clock::default())
    }

    pub fn play_with(
        &mut self,
        pre: &KirinHyphaEngine,
        callback: PreCallback,
        post: &KirinHyphaEngine,
        level: f32,
    ) -> bool {
        self.play_configured(pre, callback, post, level, Clock::default())
    }

    pub fn play_configured(
        &mut self,
        pre: &KirinHyphaEngine,
        callback: PreCallback,
        post: &KirinHyphaEngine,
        level: f32,
        clock: Clock,
    ) -> bool {
        let block = if level > 0.0 {
            tone(level, self.counter)
        } else {
            impulses(self.counter)
        };
        match callback {
            PreCallback::Measured => {
                pre.note_capture_loop_wrap(self.wrapped);
                push_configured(pre, &block, self.position, self.counter, clock, true);
            }
            PreCallback::Bypassed => bypassed(pre, self.position, true),
            PreCallback::NotCalled => {}
        }
        post.note_capture_loop_wrap(self.wrapped);
        push_configured(post, &block, self.position, self.counter, clock, true);
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
    /// Stopped transport callbacks: the host clock runs on, nothing is captured.
    pub fn stopped(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine, blocks: i64) {
        self.stopped_with(pre, PreCallback::Measured, post, blocks);
    }

    pub fn stopped_with(
        &mut self,
        pre: &KirinHyphaEngine,
        callback: PreCallback,
        post: &KirinHyphaEngine,
        blocks: i64,
    ) {
        for _ in 0..blocks {
            let engines: &[&KirinHyphaEngine] = match callback {
                PreCallback::Measured => &[pre, post],
                PreCallback::Bypassed => {
                    bypassed(pre, self.position, false);
                    &[post]
                }
                PreCallback::NotCalled => &[post],
            };
            for engine in engines {
                engine.set_signal_state(0);
                engine.note_transport_block(false, true, self.position, BLOCK as u64, false);
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
}

impl Host {
    fn wait_for_measurement(
        &mut self,
        read: impl FnMut() -> [(Option<i64>, Option<u64>); 2],
        behind: impl Fn(&progress::MeterProgress) -> bool,
        mut elapsed: impl FnMut() -> Duration,
        hold: impl FnMut(),
    ) -> Result<(), [progress::MeterProgress; 2]> {
        let began = elapsed();
        let result = progress::wait_for_measurement(CATCH_UP, read, behind, &mut elapsed, hold);
        self.start += elapsed().saturating_sub(began);
        result
    }

    pub fn hold_measured_for_cycle(&mut self, pre: &KirinHyphaEngine, post: &KirinHyphaEngine) {
        self.hold_measured(pre, post, |end| end.unwrap_or(0));
    }
    pub fn play_cycle_measured(
        &mut self,
        pre: &KirinHyphaEngine,
        post: &KirinHyphaEngine,
        level: f32,
    ) -> bool {
        if self.position + BLOCK >= LOOP_END {
            self.hold_measured(pre, post, |end| end.unwrap_or(0));
        }
        self.play(pre, post, level)
    }
    pub fn play_measured_configured(
        &mut self,
        pre: &KirinHyphaEngine,
        post: &KirinHyphaEngine,
        level: f32,
        mark: &Mark,
        clock: Clock,
    ) {
        let delay = i64::from(clock.latency.output.unwrap_or(0));
        self.hold_measured(pre, post, |end| {
            end.filter(|end| *end >= mark.position - delay)
                .unwrap_or(mark.position)
        });
        self.play_configured(pre, PreCallback::Measured, post, level, clock);
    }
    pub(super) fn hold_measured(
        &mut self,
        pre: &KirinHyphaEngine,
        post: &KirinHyphaEngine,
        measured: impl Fn(Option<i64>) -> i64,
    ) {
        let started = Instant::now();
        let position = self.position;
        let result = self.wait_for_measurement(
            || {
                [pre, post].map(|engine| {
                    (
                        settled_endpoint(engine),
                        engine
                            .poll_meter_session()
                            .map(|session| session.observed_frames),
                    )
                })
            },
            |meter| measured(meter.endpoint) + MEASURE_SLACK < position,
            || started.elapsed(),
            || {
                // Heartbeat-only callbacks keep both sides active without adding more audio.
                for engine in [pre, post] {
                    engine.set_signal_state(1);
                    engine.push_samples(&[], 2);
                }
                sleep(HOLD);
            },
        );
        assert!(result.is_ok(), "measurement catch-up stopped: {result:?}");
    }
}

#[cfg(test)]
#[path = "clock_tests.rs"]
mod clock_tests;

fn bypassed(engine: &KirinHyphaEngine, position: i64, playing: bool) {
    engine.set_signal_state(2);
    engine.note_transport_block(playing, true, position, BLOCK as u64, false);
    engine.note_record_block(RecordTakeBlock {
        generation: 0,
        recording: engine.is_recording(),
        rendered: false,
        playing,
        offline: false,
        position_valid: true,
        position_samples: position,
        num_frames: BLOCK as u64,
        clock_start_samples: 0,
        clock_end_samples: None,
    });
    engine.push_samples(&[], 2);
}
pub fn push_configured(
    engine: &KirinHyphaEngine,
    samples: &[f32],
    position: i64,
    counter: i64,
    clock: Clock,
    active: bool,
) {
    let recording = engine.is_recording();
    engine.set_signal_state(u8::from(active));
    engine.note_transport_block(true, true, position, BLOCK as u64, false);
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
    if active || recording {
        engine.note_capture_window_with_clocks(
            true,
            position,
            BLOCK as u64,
            CaptureClockSource::ProjectTimeline,
            clock.latency,
            AuxiliaryClockSamples {
                source: clock.auxiliary,
                samples: Some(counter),
            },
            false,
        );
        engine.push_samples(samples, 2);
    } else {
        engine.push_samples(&[], 2);
    }
}
