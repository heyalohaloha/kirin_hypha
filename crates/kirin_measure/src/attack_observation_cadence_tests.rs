use super::*;
use crate::attack_perception::band::AttackBand;
use std::thread;

fn wait<T>(mut probe: impl FnMut() -> Option<T>) -> T {
    let until = Instant::now() + Duration::from_secs(4);
    while Instant::now() < until {
        if let Some(value) = probe() {
            return value;
        }
        thread::sleep(Duration::from_millis(2));
    }
    panic!("bounded observation publication");
}

#[test]
fn observation_gate_preserves_latest_coverage_and_retirement() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    assert!(runtime.set_enabled(true));
    // Full real producer path: PCM coverage is newer than the latest complete ODF hop.
    for start in (0..4_800).step_by(32) {
        assert!(runtime.push_block_from_audio(&[0.1; 64], 2, Some(start)));
        thread::sleep(Duration::from_millis(1));
    }
    let snapshot = wait(|| {
        runtime
            .try_observation_snapshot()
            .filter(|s| s.source.is_some_and(|s| s.pcm_end >= 4_800))
    });
    let mut worker = BandWorker::new();
    worker.observation_revision = snapshot.revision;
    worker.observation_published_at = Some(Instant::now());
    let published = Arc::clone(&runtime.observations.lock().unwrap());
    // An unchanged idle poll within 30 ms does not replace the immutable publication.
    runtime.publish_observations(&mut worker);
    assert!(Arc::ptr_eq(
        &published,
        &runtime.observations.lock().unwrap()
    ));
    runtime.note_clock_policy_from_audio(7, true);
    assert!(
        runtime.try_observation_snapshot().is_none(),
        "retirement never waits for the gate"
    );
    runtime.shutdown_and_join();
}

/// Real 32-sample producer at 48 kHz, full rolling history and two band modes. Run optimized:
/// `cargo test -p kirin_measure --release observation_worker_keeps_up -- --ignored --nocapture`.
/// A wall-clock fixture is kept explicit to avoid labeling unoptimized/scheduled debug runs PASS.
#[test]
#[ignore]
fn observation_worker_keeps_up_with_32_sample_blocks_and_preserves_cadence() {
    const SECONDS: u64 = 8;
    const FRAMES: u64 = SECONDS * 48_000;
    for band in [None, AttackBand::from_index(3), AttackBand::from_index(1)] {
        let runtime = AttackRuntime::new(48_000, 2).unwrap();
        runtime.set_band(band);
        assert!(runtime.set_enabled(true));
        wait(|| runtime.stats().worker_running.then_some(()));
        let started = Instant::now();
        let mut next_poll = started;
        let mut last_revision = None;
        let mut observed_publications = 0;
        let mut max_gap = Duration::ZERO;
        let mut last_publication = None;
        for start in (0..FRAMES).step_by(32) {
            let mut samples = [0.0; 64];
            for frame in 0..32 {
                let t = ((start + frame) % 24_000) as f64 / 48_000.0;
                let envelope = if t < 0.006 {
                    t / 0.006
                } else {
                    (-(t - 0.006) / 0.045).exp()
                };
                let value = (0.5 * envelope * (std::f64::consts::TAU * 250.0 * t).sin()) as f32;
                samples[frame as usize * 2] = value;
                samples[frame as usize * 2 + 1] = value;
            }
            assert!(runtime.push_block_from_audio(&samples, 2, Some(start as i64)));
            let now = Instant::now();
            if now >= next_poll {
                next_poll = now + Duration::from_millis(5);
                if let Some(snapshot) = runtime.try_observation_snapshot() {
                    if last_revision != Some(snapshot.revision) {
                        if let Some(previous) = last_publication {
                            max_gap = max_gap.max(now - previous);
                        }
                        last_publication = Some(now);
                        last_revision = Some(snapshot.revision);
                        observed_publications += 1;
                    }
                }
            }
            let due = started + Duration::from_nanos((start + 32) * 1_000_000_000 / 48_000);
            if let Some(remaining) = due.checked_duration_since(Instant::now()) {
                thread::sleep(remaining);
            }
        }
        let final_snapshot = wait(|| {
            runtime
                .try_observation_snapshot()
                .filter(|s| s.source.is_some_and(|s| s.pcm_end == FRAMES as i64))
        });
        let stats = runtime.stats();
        assert_eq!(stats.pushed_blocks, FRAMES / 32);
        assert_eq!(stats.dropped_blocks, 0);
        // ALL keeps onsets in the raw history; band observation `own` is band-only.
        let detected_hits = runtime.try_history().unwrap().events().count();
        assert!(detected_hits > 0, "real transients must be detected");
        let measured_hits = final_snapshot
            .own
            .iter()
            .filter(|hit| hit.measure.is_some())
            .count();
        if band.is_some() {
            assert!(
                stats.band_measurements > 0,
                "selected band must measure real hits"
            );
            assert!(
                measured_hits > 0,
                "selected band must publish successful measurements"
            );
        }
        assert!(
            stats.analyzed_frames >= 1_400,
            "all ODF hops keep advancing: {stats:?}"
        );
        assert!(
            (160..=267).contains(&observed_publications),
            "30 ms publication cadence: {observed_publications}"
        );
        assert!(
            max_gap < Duration::from_millis(150),
            "observation stalled: {max_gap:?}"
        );
        println!("band {band:?}: {} 32-sample blocks, {} ODF frames, {detected_hits} detected hits, {} band attempts, {measured_hits} measured hits, {} observed publications, max gap {max_gap:?}, elapsed {:?}, final PCM {}", stats.pushed_blocks, stats.analyzed_frames, stats.band_measurements, observed_publications, started.elapsed(), final_snapshot.source.unwrap().pcm_end);
        runtime.shutdown_and_join();
    }
}
