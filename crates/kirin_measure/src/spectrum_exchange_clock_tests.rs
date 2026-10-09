//! Actual worker and transport proof: local FREQ survives without an invented aligned pair.
use super::*;
use crate::spectrum_runtime::SpectrumInputClock;
use std::thread;

struct Fixture {
    _temp: tempfile::TempDir,
    pre: Arc<SpectrumCoordinator>,
    post: Arc<SpectrumCoordinator>,
    pre_runtime: Arc<SpectrumRuntime>,
    post_runtime: Arc<SpectrumRuntime>,
    target: SpectrumTarget,
}

impl Fixture {
    fn new() -> Self {
        let temp = tempfile::tempdir().unwrap();
        let pre_json = temp.path().join("pre").join("pre.json");
        crate::atomic_file::write_bytes_atomic(&pre_json, b"{}").unwrap();
        let pre_runtime =
            SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let post_runtime =
            SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
        let pre = SpectrumCoordinator::new(48_000, Arc::clone(&pre_runtime));
        let post = SpectrumCoordinator::new(48_000, Arc::clone(&post_runtime));
        let target = SpectrumTarget::from_pre_json("pre".into(), &pre_json).unwrap();
        post.set_post_visible(true);
        assert!(post.post_tick("post", Some(target.clone())));
        assert!(pre.pre_tick("pre", &target.instance_dir));
        Self {
            _temp: temp,
            pre,
            post,
            pre_runtime,
            post_runtime,
            target,
        }
    }

    fn paired(&self) {
        feed(&self.pre_runtime, SpectrumInputClock::LegacyPresentation);
        feed(&self.post_runtime, SpectrumInputClock::LegacyPresentation);
        assert!(self.serve_pre());
        assert!(self.post.post_tick("post", Some(self.target.clone())));
        assert_eq!(
            self.post.try_view().unwrap().status,
            SpectrumViewStatus::Active
        );
        assert!(self.post.try_view().unwrap().difference.is_some());
    }

    fn renew_request(&self) {
        // Worker completion can outlast the 1.5 s request lease on a loaded runner.
        // Model the real POST IO heartbeat before PRE serves the request; keep the lease limit.
        assert!(self.post.post_tick("post", Some(self.target.clone())));
    }

    fn serve_pre(&self) -> bool {
        self.renew_request();
        self.pre.pre_tick("pre", &self.target.instance_dir)
    }
}

impl Drop for Fixture {
    fn drop(&mut self) {
        self.pre.shutdown();
        self.post.shutdown();
        self.pre_runtime.shutdown_and_join();
        self.post_runtime.shutdown_and_join();
    }
}

fn feed(runtime: &SpectrumRuntime, clock: impl Fn(i64) -> SpectrumInputClock) {
    feed_level(runtime, clock, 1.0);
}

fn feed_level(
    runtime: &SpectrumRuntime,
    clock: impl Fn(i64) -> SpectrumInputClock,
    amplitude: f32,
) {
    for position in (0..10_240).step_by(256) {
        let samples = (position..position + 256)
            .flat_map(|index| {
                let sample = (std::f32::consts::TAU * 1_000.0 * index as f32 / 48_000.0).sin();
                [sample * amplitude, sample * amplitude]
            })
            .collect::<Vec<_>>();
        assert!(runtime.push_block_from_audio_with_clock(&samples, 2, Some(clock(position))));
        thread::sleep(Duration::from_millis(2));
    }
    let deadline = Instant::now() + Duration::from_secs(3);
    while Instant::now() < deadline {
        if runtime.try_history().is_some_and(|history| {
            history
                .newest()
                .is_some_and(|frame| frame.presentation_end_samples >= 9_600)
        }) {
            return;
        }
        thread::sleep(Duration::from_millis(2));
    }
    panic!("actual Spectrum worker failed to publish");
}

#[test]
fn clock_pair_fixture_renews_request_after_worker_delay() {
    let fixture = Fixture::new();
    thread::sleep(Duration::from_millis(REQUEST_LEASE_MS as u64 + 50));
    fixture.paired();
}

#[test]
fn local_post_keeps_absolute_spectrum_and_immediately_retires_an_old_delta_lease() {
    let fixture = Fixture::new();
    fixture.paired();
    feed(&fixture.post_runtime, SpectrumInputClock::LocalProject);
    assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
    let view = fixture.post.try_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::Unavailable);
    assert!(view.difference.is_none());
    assert!(view.post_spectrum.is_some());
    assert!(view.post_spectrum_history.newest().is_some());
    assert!(read_snapshot(&fixture.target.instance_dir).is_some());
    // Returning to the unchanged aligned path still produces the exact pair.
    feed(
        &fixture.post_runtime,
        SpectrumInputClock::LegacyPresentation,
    );
    assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
    assert!(fixture.post.try_view().unwrap().difference.is_some());
}

#[test]
fn local_pre_cannot_publish_and_known_authority_rewrites_the_same_endpoint() {
    let fixture = Fixture::new();
    fixture.paired();
    feed(&fixture.pre_runtime, SpectrumInputClock::LocalProject);
    assert!(fixture.serve_pre());
    assert!(read_snapshot(&fixture.target.instance_dir).is_none());
    assert!(fixture.serve_pre());
    assert!(read_snapshot(&fixture.target.instance_dir).is_none());
    feed(&fixture.pre_runtime, SpectrumInputClock::LegacyPresentation);
    assert!(fixture.serve_pre());
    assert!(read_snapshot(&fixture.target.instance_dir).is_some());
}

#[test]
fn clock_cleanup_preserves_foreign_publication_and_contention_preserves_owned_publication() {
    let fixture = Fixture::new();
    fixture.paired();
    let before = read_snapshot(&fixture.target.instance_dir).unwrap();
    fixture.renew_request();
    fixture.pre_runtime.with_locked_history_for_clock_test(|| {
        assert!(fixture.pre.pre_tick("pre", &fixture.target.instance_dir));
        assert_eq!(
            read_snapshot(&fixture.target.instance_dir)
                .unwrap()
                .request_id,
            before.request_id
        );
    });
    let foreign = Uuid::new_v4();
    write_snapshot(
        &fixture.target.instance_dir,
        &encode_snapshot(foreign, &before.history),
    )
    .unwrap();
    feed(&fixture.pre_runtime, SpectrumInputClock::LocalRender);
    assert!(fixture.serve_pre());
    assert_eq!(
        read_snapshot(&fixture.target.instance_dir)
            .unwrap()
            .request_id,
        foreign
    );
}

#[test]
fn local_unpaired_post_works_without_creating_any_pre_publication() {
    let fixture = Fixture::new();
    assert!(fixture.post.post_tick("post", None));
    feed(&fixture.post_runtime, SpectrumInputClock::LocalRender);
    assert!(fixture.post.post_tick("post", None));
    let view = fixture.post.try_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::NoPair);
    assert!(view.post_spectrum.is_some());
    assert!(view.difference.is_none());
    assert!(read_snapshot(&fixture.target.instance_dir).is_none());
}

fn known(
    position: i64,
    source: crate::PresentationLatencySource,
    output_latency_samples: u32,
) -> SpectrumInputClock {
    SpectrumInputClock::Presentation {
        start_samples: position,
        source,
        output_latency_samples,
    }
}

#[test]
fn known_clock_cutover_replaces_same_endpoint_pre_bytes_and_post_delta_timeline() {
    let fixture = Fixture::new();
    fixture.paired();
    let old = read_snapshot(&fixture.target.instance_dir).unwrap();
    feed_level(
        &fixture.pre_runtime,
        |p| known(p, crate::PresentationLatencySource::Vst3, 2_048),
        0.5,
    );
    assert!(fixture.serve_pre());
    let new = read_snapshot(&fixture.target.instance_dir).unwrap();
    let peak = |history: &SpectrumHistory| {
        history
            .newest()
            .unwrap()
            .dbfs
            .iter()
            .copied()
            .fold(f32::NEG_INFINITY, f32::max)
    };
    assert!((peak(&old.history) - peak(&new.history) - 6.0206).abs() < 0.1);
    for (source, latency, amplitude, expected) in [
        (crate::PresentationLatencySource::Vst3, 2_048, 0.25, -6.0206),
        (
            crate::PresentationLatencySource::Vst3,
            4_096,
            0.125,
            -12.0412,
        ),
        (
            crate::PresentationLatencySource::AudioUnitV2,
            4_096,
            0.0625,
            -18.0618,
        ),
    ] {
        feed_level(
            &fixture.post_runtime,
            |p| known(p, source, latency),
            amplitude,
        );
        assert!(
            fixture.post.try_view().unwrap().difference.is_none(),
            "retired pair appeared between producer cutover and IO tick"
        );
        assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
        let view = fixture.post.try_view().unwrap();
        let difference = view.difference.as_ref().unwrap();
        let strongest = new
            .history
            .newest()
            .unwrap()
            .dbfs
            .iter()
            .enumerate()
            .max_by(|(_, a), (_, b)| a.total_cmp(b))
            .unwrap()
            .0;
        assert!((difference.raw_db[strongest] - expected).abs() < 0.1);
        assert!(view
            .spectrum_timeline
            .frames()
            .all(
                |frame| (frame.post_dbfs[strongest] - frame.pre_dbfs[strongest] - expected).abs()
                    < 0.1
            ));
    }
}

#[test]
fn clock_cutover_after_remote_read_cannot_publish_an_old_join() {
    let fixture = Fixture::new();
    fixture.paired();
    let session = fixture
        .post
        .post_session
        .lock()
        .unwrap()
        .as_ref()
        .unwrap()
        .clone();
    assert!(!fixture.post.join_post_view_after_read(
        &session,
        &fixture.target,
        Instant::now(),
        || {
            assert!(fixture.post_runtime.push_block_from_audio_with_clock(
                &[0.0; 512],
                2,
                Some(SpectrumInputClock::LocalProject(0))
            ));
        }
    ));
    assert!(fixture.post.try_view().unwrap().difference.is_none());
}

#[test]
#[cfg(not(windows))]
fn clock_cutover_during_pre_io_retires_completed_old_authority_publication() {
    let fixture = Fixture::new();
    fixture.paired();
    feed(&fixture.pre_runtime, |p| {
        known(p, crate::PresentationLatencySource::Vst3, 2_048)
    });
    fixture.renew_request();
    let pause =
        crate::atomic_file::AtomicWritePause::install(snapshot_path(&fixture.target.instance_dir));
    let pre = Arc::clone(&fixture.pre);
    let dir = fixture.target.instance_dir.clone();
    let pending = thread::spawn(move || pre.pre_tick("pre", &dir));
    pause.wait_until_entered();
    assert!(fixture.pre_runtime.push_block_from_audio_with_clock(
        &[0.0; 512],
        2,
        Some(SpectrumInputClock::LocalProject(0))
    ));
    pause.release();
    assert!(!pending.join().unwrap());
    drop(pause);
    assert!(read_snapshot(&fixture.target.instance_dir).is_none());
    feed(&fixture.pre_runtime, |p| {
        known(p, crate::PresentationLatencySource::Vst3, 2_048)
    });
    // Reproduce a worker/scheduler delay before the recovery publication deterministically.
    thread::sleep(Duration::from_millis(REQUEST_LEASE_MS as u64 + 50));
    assert!(fixture.serve_pre());
    assert!(read_snapshot(&fixture.target.instance_dir).is_some());
}

#[test]
fn cloned_view_cannot_borrow_a_concurrently_published_new_clock_revision() {
    let fixture = Fixture::new();
    fixture.paired();
    let retired = fixture
        .post
        .try_view_after_clone(|| {
            feed_level(
                &fixture.post_runtime,
                |p| known(p, crate::PresentationLatencySource::Vst3, 2_048),
                0.5,
            );
            assert!(fixture.post.post_tick("post", Some(fixture.target.clone())));
            assert!(fixture.post.try_view().unwrap().difference.is_some());
        })
        .unwrap();
    assert!(retired.difference.is_none());
    assert_eq!(retired.spectrum_timeline.frames().count(), 0);
}

#[test]
#[cfg(not(windows))]
fn clock_cutover_during_expired_pre_write_removes_old_bytes_before_early_return() {
    let fixture = Fixture::new();
    fixture.paired();
    feed(&fixture.pre_runtime, |p| {
        known(p, crate::PresentationLatencySource::Vst3, 2_048)
    });
    fixture.renew_request();
    let pause =
        crate::atomic_file::AtomicWritePause::install(snapshot_path(&fixture.target.instance_dir));
    let pre = Arc::clone(&fixture.pre);
    let dir = fixture.target.instance_dir.clone();
    let pending = thread::spawn(move || pre.pre_tick("pre", &dir));
    pause.wait_until_entered();
    assert!(fixture.pre_runtime.push_block_from_audio_with_clock(
        &[0.0; 512],
        2,
        Some(SpectrumInputClock::LocalProject(0))
    ));
    thread::sleep(PRESENTATION_HOLD + Duration::from_millis(25));
    pause.release();
    assert!(!pending.join().unwrap());
    drop(pause);
    assert!(read_snapshot(&fixture.target.instance_dir).is_none());
    // Renew after the deliberately expired request, then verify local cleanup cannot revive
    // the retired bytes and the same endpoint can be published under current known authority.
    assert!(fixture.serve_pre());
    assert!(read_snapshot(&fixture.target.instance_dir).is_none());
    feed(&fixture.pre_runtime, |p| {
        known(p, crate::PresentationLatencySource::Vst3, 2_048)
    });
    assert!(fixture.serve_pre());
    assert!(read_snapshot(&fixture.target.instance_dir).is_some());
}
