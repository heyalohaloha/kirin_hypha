//! Real producer fixtures: silence emits ODF and retains PCM without inventing detected events.
use super::super::*;
use crate::attack_perception::band::{AttackBand, BandSound, BandSpanEnd};
use crate::attack_runtime::single::{AttackSingleReason, AttackSingleRequest};
use crate::attack_runtime::snapshot::{AttackFinish, AttackObservationAnchor};
use std::thread;
use std::time::{Duration, Instant};

fn wait<T>(mut probe: impl FnMut() -> Option<T>) -> T {
    let deadline = Instant::now() + Duration::from_secs(4);
    while Instant::now() < deadline {
        if let Some(value) = probe() {
            return value;
        }
        thread::sleep(Duration::from_millis(2));
    }
    panic!("bounded fixture producer response");
}
fn feed(runtime: &AttackRuntime, start: i64, frames: usize, burst: bool) {
    feed_onsets(runtime, start, frames, if burst { &[20_000] } else { &[] });
}
fn feed_onsets(runtime: &AttackRuntime, start: i64, frames: usize, onsets: &[i64]) {
    for offset in (0..frames).step_by(512) {
        let count = 512.min(frames - offset);
        let mut audio = Vec::with_capacity(count * 2);
        for frame in offset..offset + count {
            let value = onsets
                .iter()
                .map(|onset| {
                    let t = (start + frame as i64 - onset) as f64 / 48_000.0;
                    if t < 0.0 {
                        return 0.0;
                    }
                    let envelope = if t < 0.006 {
                        t / 0.006
                    } else {
                        (-(t - 0.006) / 0.045).exp()
                    };
                    0.5 * envelope * (std::f64::consts::TAU * 250.0 * t).sin()
                })
                .sum::<f64>() as f32;
            audio.extend_from_slice(&[value, value]);
        }
        assert!(runtime.push_block_from_audio(&audio, 2, Some(start + offset as i64)));
        thread::sleep(Duration::from_millis(1));
    }
}

fn request(
    source: snapshot::AttackSourceKey,
    event: AttackEvent,
    band: Option<AttackBand>,
) -> AttackSingleRequest {
    AttackSingleRequest {
        key_source: source,
        key_event_sample: event.event_sample,
        key_token: 1,
        local_source: source,
        pre_source: None,
        event,
        band,
        requested_end: event.event_sample + 14_400,
        measurement_end: event.event_sample + 14_400,
        span_end: BandSpanEnd::Window,
        pre: None,
        pre_detail: None,
        pair_kind: 4,
        target: 0,
        pair_authority_revision: 1,
        proof_token: [0; 32],
    }
}

#[test]
fn pre_only_hit_measures_silent_post_with_no_post_detector_event() {
    let band = AttackBand::from_index(3).unwrap();
    let pre = AttackRuntime::new(48_000, 2).unwrap();
    let post = AttackRuntime::new(48_000, 2).unwrap();
    pre.set_band(Some(band));
    post.set_band(Some(band));
    assert!(pre.set_enabled(true) && post.set_enabled(true));
    // Deliberately different PRE/local generations: a PRE-owned key is not a local event key.
    pre.note_clock_policy_from_audio(1, true);
    feed(&pre, 0, 48_000, true);
    feed(&post, 0, 48_000, false);
    let pre_snapshot = wait(|| {
        pre.try_observation_snapshot()
            .filter(|snapshot| snapshot.own.iter().any(|o| o.finish == AttackFinish::Full))
    });
    let pre_observation = *pre_snapshot
        .own
        .iter()
        .find(|o| o.finish == AttackFinish::Full)
        .unwrap();
    let post_snapshot = wait(|| {
        post.try_observation_snapshot()
            .filter(|snapshot| snapshot.source.is_some_and(|s| s.pcm_end >= 48_000))
    });
    let post_source = post_snapshot.source.unwrap();
    assert!(post.try_history().unwrap().frames().len() > 0);
    assert_eq!(post.try_history().unwrap().events().len(), 0);
    assert_ne!(
        pre_snapshot.source.unwrap().source.generation,
        post_source.source.generation
    );
    let mut local_event = pre_observation.event;
    local_event.generation = post_source.source.generation;
    local_event.definition_hash = post_source.source.odf_hash;
    let mut selected = request(post_source.source, local_event, Some(band));
    selected.key_source = pre_snapshot.source.unwrap().source;
    selected.key_event_sample = pre_observation.event.event_sample;
    selected.pair_kind = 1;
    selected.pre = Some(pre_observation);
    selected.requested_end = pre_observation.requested_end;
    selected.measurement_end = pre_observation.actual_end;
    let token = post.request_single(selected).unwrap();
    let reply = wait(|| {
        post.poll_single(token)
            .filter(|reply| reply.finish != AttackFinish::Acquiring)
    });
    assert_eq!(reply.finish, AttackFinish::Full);
    let observed = reply.band_observation.unwrap();
    assert_eq!(observed.measure.unwrap().sound, BandSound::Silent);
    assert_eq!(observed.requested_end, pre_observation.requested_end);
    assert_eq!(observed.actual_end, pre_observation.actual_end);
    assert!(observed.head_valid.iter().all(|v| *v == 1));
    assert_eq!(
        reply.request.key_source,
        pre_snapshot.source.unwrap().source
    );
    assert_eq!(reply.request.local_source, post_source.source);
    pre.shutdown_and_join();
    post.shutdown_and_join();
}

#[test]
fn partial_pre_window_keeps_logical_end_and_actual_post_end_separate() {
    let band = AttackBand::from_index(3).unwrap();
    let post = AttackRuntime::new(48_000, 2).unwrap();
    post.set_band(Some(band));
    assert!(post.set_enabled(true));
    feed(&post, 0, 48_000, false);
    let snapshot = wait(|| {
        post.try_observation_snapshot()
            .filter(|s| s.source.is_some_and(|s| s.pcm_end >= 48_000))
    });
    let source = snapshot.source.unwrap().source;
    let event = AttackEvent {
        generation: source.generation,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: source.odf_hash,
        event_sample: 20_000,
        decision_sample: 21_000,
        value: 0.0,
    };
    post.request_observation_anchors(
        Some(band),
        vec![AttackObservationAnchor {
            event,
            requested_end: 34_400,
            measurement_end: 27_200,
            span_end: BandSpanEnd::AudioEnd,
        }],
    );
    let observed = wait(|| {
        post.try_observation_snapshot().and_then(|s| {
            s.anchored_at(20_000, 34_400)
                .filter(|o| o.finish == AttackFinish::AudioEnd)
                .copied()
        })
    });
    assert_eq!(observed.actual_end, 27_200);
    assert_eq!(observed.requested_end, 34_400);
    assert!(observed.tail_valid.contains(&1));
    assert!(observed.tail_valid.contains(&0));
    post.shutdown_and_join();
}

#[test]
fn clock_policy_force_invalid_and_same_sample_seek_retire_old_single() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    let band = AttackBand::from_index(3).unwrap();
    runtime.set_band(Some(band));
    assert!(runtime.set_enabled(true));
    feed(&runtime, 0, 48_000, true);
    let snapshot = wait(|| {
        runtime
            .try_observation_snapshot()
            .filter(|s| !s.own.is_empty())
    });
    let old = snapshot.source.unwrap().source;
    let token = runtime
        .request_single(request(old, snapshot.own[0].event, Some(band)))
        .unwrap();
    wait(|| {
        runtime
            .poll_single(token)
            .filter(|s| s.finish != AttackFinish::Acquiring)
    });
    for (policy, force) in [(2, false), (2, true), (0, false), (1, false)] {
        let before = runtime.generation.load(Ordering::Acquire);
        runtime.note_clock_policy_from_audio(policy, force);
        assert!(runtime.generation.load(Ordering::Acquire) > before);
        assert!(runtime.try_observation_snapshot().is_none());
        let retired = runtime.poll_single(token).unwrap();
        assert_eq!(retired.finish, AttackFinish::Retired);
        assert_eq!(retired.reason, AttackSingleReason::SourceChanged);
        assert!(retired.band_observation.is_none());
    }
    feed(&runtime, 0, 48_000, true); // same content position after a forced source epoch
    let new = wait(|| {
        runtime.try_observation_snapshot().filter(|s| {
            s.source
                .is_some_and(|s| s.source.generation != old.generation)
        })
    });
    assert_eq!(new.source.unwrap().source.incarnation, old.incarnation);
    assert_ne!(new.source.unwrap().source, old);
    assert!(runtime
        .poll_single(token)
        .unwrap()
        .band_observation
        .is_none());
    let before_seek = new.source.unwrap().source.generation;
    feed(&runtime, 0, 4_096, false); // backward seek to an already-used sample
    let seek = wait(|| {
        runtime
            .try_observation_snapshot()
            .filter(|s| s.source.is_some_and(|s| s.source.generation > before_seek))
    });
    assert!(seek.source.unwrap().source.generation > before_seek);
    assert!(runtime.poll_single(token).unwrap().detail.is_none());
    runtime.shutdown_and_join();
}

#[test]
fn invalid_pcm_and_worker_stop_cannot_reuse_a_terminal_publication() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    let band = AttackBand::from_index(3).unwrap();
    runtime.set_band(Some(band));
    assert!(runtime.set_enabled(true));
    feed(&runtime, 0, 48_000, true);
    let published = wait(|| {
        runtime
            .try_observation_snapshot()
            .filter(|s| s.own.iter().any(|o| o.finish == AttackFinish::Full))
    });
    let source = published.source.unwrap().source;
    let token = runtime
        .request_single(request(
            source,
            published
                .own
                .iter()
                .find(|o| o.finish == AttackFinish::Full)
                .unwrap()
                .event,
            Some(band),
        ))
        .unwrap();
    wait(|| {
        runtime
            .poll_single(token)
            .filter(|s| s.finish == AttackFinish::Full)
    });
    let before = runtime.generation.load(Ordering::Acquire);
    assert!(runtime.push_block_from_audio(&[f32::NAN; 1_024], 2, Some(48_000)));
    wait(|| (runtime.generation.load(Ordering::Acquire) > before).then_some(()));
    assert!(runtime.try_observation_snapshot().is_none());
    assert_eq!(
        runtime.poll_single(token).unwrap().finish,
        AttackFinish::Retired
    );
    feed(&runtime, 0, 48_000, true);
    let fresh = wait(|| {
        runtime.try_observation_snapshot().filter(|s| {
            s.source
                .is_some_and(|s| s.source.generation > source.generation)
                && s.own.iter().any(|o| o.finish == AttackFinish::Full)
        })
    });
    assert_eq!(
        fresh
            .own
            .iter()
            .find(|o| o.finish == AttackFinish::Full)
            .unwrap()
            .event
            .event_sample,
        published
            .own
            .iter()
            .find(|o| o.finish == AttackFinish::Full)
            .unwrap()
            .event
            .event_sample,
        "after invalid PCM, a new clean source must preserve exact PCM-to-clock association"
    );
    let token = runtime
        .request_single(request(
            fresh.source.unwrap().source,
            fresh
                .own
                .iter()
                .find(|o| o.finish == AttackFinish::Full)
                .unwrap()
                .event,
            Some(band),
        ))
        .unwrap();
    wait(|| {
        runtime
            .poll_single(token)
            .filter(|s| s.finish == AttackFinish::Full)
    });
    // Stop the actual disposable worker without disabling the runtime. Production observes the
    // same running=false edge after an isolated worker failure; this does not touch a DAW.
    runtime.shutdown.store(true, Ordering::Release);
    runtime.wake.1.notify_all();
    wait(|| (!runtime.worker_running.load(Ordering::Acquire)).then_some(()));
    wait(|| {
        runtime
            .worker
            .lock()
            .ok()?
            .as_ref()?
            .is_finished()
            .then_some(())
    });
    assert!(runtime.try_observation_snapshot().is_none());
    let retired = runtime.poll_single(token).unwrap();
    assert_eq!(retired.finish, AttackFinish::Retired);
    assert_eq!(retired.reason, AttackSingleReason::WorkerUnavailable);
    runtime.shutdown.store(false, Ordering::Release);
    let before = runtime.generation.load(Ordering::Acquire);
    assert!(runtime.set_enabled(true));
    assert!(runtime.generation.load(Ordering::Acquire) > before);
    assert!(runtime.try_observation_snapshot().is_none());
    feed(&runtime, 0, 48_000, true);
    wait(|| {
        runtime
            .try_observation_snapshot()
            .filter(|s| s.source.is_some_and(|s| s.source.generation > before))
    });
    assert!(runtime
        .poll_single(token)
        .unwrap()
        .band_observation
        .is_none());
    runtime.shutdown_and_join();
}

#[test]
fn next_hit_after_acceptance_limits_physical_measurement_without_changing_key() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    let band = AttackBand::from_index(3).unwrap();
    runtime.set_band(Some(band));
    assert!(runtime.set_enabled(true));
    feed_onsets(&runtime, 0, 24_000, &[20_000, 24_000]);
    let snapshot = wait(|| {
        runtime
            .try_observation_snapshot()
            .filter(|s| !s.own.is_empty())
    });
    let event = snapshot.own[0].event;
    let accepted = request(snapshot.source.unwrap().source, event, Some(band));
    let logical_end = accepted.requested_end;
    let key = (
        accepted.key_source,
        accepted.key_event_sample,
        accepted.key_token,
    );
    let token = runtime.request_single(accepted).unwrap();
    feed_onsets(&runtime, 24_000, 24_000, &[20_000, 24_000]);
    let reply = wait(|| {
        runtime
            .poll_single(token)
            .filter(|s| s.finish == AttackFinish::Full)
    });
    assert_eq!(reply.token, token);
    assert_eq!(
        (
            reply.request.key_source,
            reply.request.key_event_sample,
            reply.request.key_token
        ),
        key
    );
    assert_eq!(reply.request.requested_end, logical_end);
    assert!(reply.request.measurement_end < logical_end);
    assert_eq!(reply.request.span_end, BandSpanEnd::NextHit);
    let observation = reply.band_observation.unwrap();
    assert_eq!(observation.requested_end, logical_end);
    assert_eq!(observation.actual_end, reply.request.measurement_end);
    assert!(observation.tail_valid.contains(&0));
    assert!(matches!(
        observation.measure.unwrap().sound,
        BandSound::Rises {
            release: crate::attack_perception::band::BandRelease::CutByNextHit,
            ..
        }
    ));
    let own = wait(|| {
        runtime
            .band_results()
            .own_at(event.event_sample)
            .copied()
            .filter(|detail| detail.span_end_sample == reply.request.measurement_end)
    });
    assert_eq!(own.measure.unwrap().span_end, BandSpanEnd::NextHit);
    wait(|| (runtime.band_results().own().len() == 2).then_some(()));
    assert_eq!(runtime.stats().band_measurements, 2);
    let immutable = runtime.poll_single(token).unwrap();
    assert_eq!(immutable, reply);
    runtime.shutdown_and_join();
}
