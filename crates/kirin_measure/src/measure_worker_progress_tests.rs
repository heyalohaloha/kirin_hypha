//! Real worker coverage for cursor publication, pass cutovers and a parked Keep lane.
use super::*;
use crate::record_take::RecordTakeBlock;
use crate::watch_playback_pass::{
    add_watch_ring_cursor_samples, publish_watch_playback_pass_boundary,
};
use worker_probe::{Controller, Event, Iteration};

fn pcm(level: f32) -> Vec<f32> {
    vec![
        level,
        -level,
        level / 2.0,
        -level / 2.0,
        -level,
        level,
        0.0,
        level,
    ]
}

struct Worker {
    controller: Controller,
    worker: Option<JoinHandle<()>>,
    producer: rtrb::Producer<f32>,
    pass: Arc<AtomicU64>,
    cutover: Arc<AtomicU64>,
    epoch: Arc<AtomicU64>,
    cursor_pass: Arc<AtomicU64>,
    cursor: Arc<AtomicU64>,
    sm: Arc<RecordStateMachine>,
    tracker: Arc<RecordTakeTracker>,
    ingress: Arc<crate::RecordIngress>,
    meter: Arc<Mutex<MeterSession>>,
    shutdown: Arc<AtomicBool>,
    fed: Vec<(i64, i64, Vec<f64>)>,
}

impl Worker {
    fn new(publish_cursor: bool) -> Self {
        let (controller, probe) = Controller::new();
        let sm = Arc::new(RecordStateMachine::new());
        sm.install_measure_worker_probe(probe);
        let tracker = Arc::new(RecordTakeTracker::new());
        let (producer, consumer) = rtrb::RingBuffer::new(128);
        let meter = Arc::new(Mutex::new(
            MeterSession::new_in_epoch(48_000, ChannelLayout::stereo(), 1).unwrap(),
        ));
        let mut fixture = Self {
            controller,
            worker: None,
            producer,
            pass: Arc::new(AtomicU64::new(1)),
            cutover: Arc::new(AtomicU64::new(0)),
            epoch: Arc::new(AtomicU64::new(0)),
            cursor_pass: Arc::new(AtomicU64::new(1)),
            cursor: Arc::new(AtomicU64::new(0)),
            sm,
            tracker,
            ingress: Arc::new(crate::RecordIngress::new(128)),
            meter,
            shutdown: Arc::new(AtomicBool::new(false)),
            fed: Vec::new(),
        };
        fixture.watch(1_000, 0.125);
        if !publish_cursor {
            fixture.cursor.store(0, Ordering::Release);
        }
        let signal = Arc::new(AtomicU8::new(0));
        store_signal_state(&signal, SignalState::Active);
        // Freshness is independent of scheduler pauses in this protocol fixture.
        let evaluator = Arc::new(LivenessEvaluator::new(
            Arc::new(AtomicU32::new(1)),
            Duration::MAX,
        ));
        fixture.worker = Some(spawn_measure_thread(
            consumer,
            48_000,
            ChannelLayout::stereo(),
            Arc::new(Mutex::new(MeasureResult::default())),
            Some(Arc::clone(&fixture.meter)),
            None,
            Arc::clone(&fixture.pass),
            Arc::clone(&fixture.cutover),
            Arc::clone(&fixture.epoch),
            Arc::clone(&fixture.cursor_pass),
            Arc::clone(&fixture.cursor),
            signal,
            Arc::clone(&fixture.shutdown),
            evaluator,
            Arc::clone(&fixture.sm),
            Arc::new(Mutex::new(None)),
            crate::record_writer::new_record_trace_queue(),
            Arc::clone(&fixture.tracker),
            Arc::clone(&fixture.ingress),
        ));
        fixture
    }

    fn step(&self) -> Event {
        self.controller.release();
        self.worker.as_ref().unwrap().thread().unpark();
        self.controller.receive()
    }

    fn first(&self) -> Iteration {
        match self.controller.receive() {
            Event::Iteration(state) => state,
            event => panic!("expected initial iteration, got {event:?}"),
        }
    }

    fn iteration(&self) -> Iteration {
        match self.step() {
            Event::Iteration(state) => state,
            event => panic!("expected iteration, got {event:?}"),
        }
    }

    fn idle(&self, recording: bool) {
        match self.step() {
            Event::Idle { recording: actual } => assert_eq!(actual, recording),
            event => panic!("the actual worker must choose the idle path, got {event:?}"),
        }
    }

    fn feed(&mut self, expected_range: (i64, i64), level: f32) {
        let (position, endpoint) = expected_range;
        let Event::Feed { samples, clock } = self.step() else {
            panic!("expected measured PCM at {position}");
        };
        assert_eq!(
            samples,
            pcm(level).into_iter().map(f64::from).collect::<Vec<_>>()
        );
        assert_eq!(clock.position_samples, Some(position));
        assert_eq!(clock.source, CaptureClockSource::ProjectTimeline);
        assert!(clock.epoch.is_some());
        // The next fixture span begins elsewhere, so this endpoint identifies this PCM once.
        let measured_end = clock.position_samples.unwrap() + (samples.len() / 2) as i64;
        assert_eq!(measured_end, endpoint);
        self.fed.push((position, measured_end, samples));
    }

    fn capture(&self, position: i64) {
        self.tracker.note_meter_active(true);
        self.tracker.note_block(RecordTakeBlock {
            generation: self.sm.generation(),
            recording: self.sm.is_recording(),
            rendered: true,
            playing: true,
            offline: false,
            position_valid: true,
            position_samples: position,
            num_frames: 4,
            clock_start_samples: 0,
            clock_end_samples: None,
        });
        self.tracker.note_capture_window(true, position, 4);
    }

    fn publish(&self, samples: u64) {
        add_watch_ring_cursor_samples(
            &self.epoch,
            &self.cursor_pass,
            &self.cursor,
            self.pass.load(Ordering::Acquire),
            samples,
        );
    }

    fn watch(&mut self, position: i64, level: f32) {
        self.capture(position);
        for sample in pcm(level) {
            self.producer.push(sample).unwrap();
        }
        self.publish(8);
    }

    fn boundary(&self) {
        publish_watch_playback_pass_boundary(
            &self.pass,
            &self.epoch,
            &self.cursor_pass,
            &self.cursor,
            &self.cutover,
        );
    }

    fn keep(&self) {
        assert!(self.ingress.prepare_for_generation(1));
        self.sm.try_enter_record(crate::License::Os).unwrap();
        assert_eq!(self.sm.generation(), 1);
        assert_eq!(self.tracker.captured_frames_total(), 16);
        self.capture(4_000);
        // This fixture thread is the sole Audio-side owner. The real worker remains gated.
        unsafe {
            assert!(self.ingress.adopt_from_audio());
            assert!(self.ingress.begin_generation_from_audio(1, 16));
            assert_eq!(self.ingress.push_from_audio(1, &pcm(0.625)), 8);
        }
        // Join the finite spool producer; no sleep/poll loop or throughput deadline in the test.
        self.ingress.finish_fixture_capture(1);
    }

    fn stop(&mut self) -> thread::Result<()> {
        self.shutdown.store(true, Ordering::Release);
        self.controller.disconnect();
        if let Some(worker) = self.worker.take() {
            worker.thread().unpark();
            worker.join()
        } else {
            Ok(())
        }
    }
}

impl Drop for Worker {
    fn drop(&mut self) {
        let _ = self.stop();
    }
}

fn assert_progress(state: Iteration, consumed: u64, native: u64, endpoint: i64) {
    assert_eq!(
        (state.consumed, state.native_frames, state.endpoint),
        (consumed, native, Some(endpoint)),
        "{state:?}"
    );
}

#[test]
fn a_real_worker_waits_for_cursor_publication_then_consumes_the_existing_tail() {
    let mut worker = Worker::new(false);
    let state = worker.first();
    assert_eq!((state.consumed, state.queued), (0, 8));
    // A non-empty ring is fixed in place, with zero cursor permission. A yield regression
    // produces another Iteration event here instead of Idle and fails without a time threshold.
    worker.idle(false);
    assert_eq!(worker.cursor.load(Ordering::Acquire), 0);
    worker.publish(8);
    let state = worker.iteration();
    assert_eq!((state.consumed, state.queued), (0, 8));
    worker.feed((1_000, 1_004), 0.125);
    worker.idle(false);
    let state = worker.iteration();
    assert_progress(state, 8, 4, 1_004);
    assert_eq!(state.queued, 0);
    assert_eq!(worker.fed.len(), 1);
    assert_eq!(worker.meter.lock().unwrap().snapshot().active_frames, 4);
    worker.stop().unwrap();
}

#[test]
fn pass_cutovers_and_keep_restore_each_watch_sample_and_capture_endpoint_once() {
    let mut worker = Worker::new(true);
    assert_eq!(worker.first().consumed, 0);
    worker.feed((1_000, 1_004), 0.125);
    worker.idle(false);
    assert_progress(worker.iteration(), 8, 4, 1_004);

    // The consumer misses two boundaries: drain the old pass, then measure the latest one.
    worker.boundary();
    worker.watch(2_000, 0.25);
    worker.boundary();
    worker.watch(3_000, 0.375);
    assert_eq!(worker.cutover.load(Ordering::Acquire), 16);
    worker.feed((2_000, 2_004), 0.25);
    worker.idle(false);
    let state = worker.iteration();
    assert_eq!(
        (state.consumed, state.native_frames, state.queued),
        (16, 8, 8)
    );
    assert_eq!(state.playback_pass, 3);
    worker.feed((3_000, 3_004), 0.375);
    worker.idle(false);
    assert_progress(worker.iteration(), 24, 12, 3_004);

    // Entry must drain this admitted Watch tail before parking its non-zero consumer cursor.
    worker.watch(3_004, 0.5);
    worker.keep();
    worker.feed((3_004, 3_008), 0.5);
    worker.feed((4_000, 4_004), 0.625);
    worker.idle(true);
    let state = worker.iteration();
    assert_progress(state, 8, 20, 4_004);
    assert!(state.recording);
    assert_eq!(state.parked_watch_consumed, 32);

    for _ in 0..3 {
        worker.boundary();
    }
    worker.idle(true);
    let state = worker.iteration();
    assert_progress(state, 8, 20, 4_004);
    assert_eq!((state.playback_pass, state.parked_watch_consumed), (6, 32));

    worker.sm.exit_record();
    worker.watch(5_000, 0.75);
    worker.feed((5_000, 5_004), 0.75);
    worker.idle(false);
    let state = worker.iteration();
    assert_progress(state, 40, 24, 5_004);
    assert!(!state.recording);
    assert_eq!(state.queued, 0);
    assert_eq!(worker.cursor.load(Ordering::Acquire), 40);
    assert_eq!(worker.tracker.captured_frames_total(), 24);
    assert_eq!(worker.meter.lock().unwrap().snapshot().active_frames, 24);
    assert_eq!(worker.sm.seal(), 1);
    assert_eq!(
        worker
            .fed
            .iter()
            .map(|(start, end, _)| (*start, *end))
            .collect::<Vec<_>>(),
        vec![
            (1_000, 1_004),
            (2_000, 2_004),
            (3_000, 3_004),
            (3_004, 3_008),
            (4_000, 4_004),
            (5_000, 5_004)
        ]
    );
    // Empty follow-up iterations must neither replay samples nor advance the capture index.
    worker.idle(false);
    assert_progress(worker.iteration(), 40, 24, 5_004);
    worker.stop().unwrap();
}
