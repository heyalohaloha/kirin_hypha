use std::thread;
use std::time::{Duration, Instant};

use super::super::*;
use crate::attack_perception::band::{AttackBand, BandSound, BandSpanEnd};

/// 250 Hz bursts at each onset (48 kHz stereo): a 6 ms rise, then a 45 ms decay.
fn bursts(total: usize, onsets: &[usize]) -> Vec<f32> {
    let mut samples = Vec::with_capacity(total * 2);
    for frame in 0..total {
        let value = onsets
            .iter()
            .map(|&onset| {
                let t = (frame as f64 - onset as f64) / 48_000.0;
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
        samples.push(value);
        samples.push(value);
    }
    samples
}

/// Pushes `samples` from content position `start` in 1 024-frame blocks, a little faster than
/// real time, so the 128-slot ingress ring never fills.
fn feed(runtime: &AttackRuntime, samples: &[f32], start: i64) {
    let frames = samples.len() / 2;
    let mut position = 0;
    while position < frames {
        let count = 1_024.min(frames - position);
        assert!(runtime.push_block_from_audio(
            &samples[position * 2..(position + count) * 2],
            2,
            Some(start + position as i64),
        ));
        position += count;
        thread::sleep(Duration::from_millis(1));
    }
}

fn wait_for<T>(deadline: Duration, mut probe: impl FnMut() -> Option<T>) -> Option<T> {
    let until = Instant::now() + deadline;
    while Instant::now() < until {
        if let Some(value) = probe() {
            return Some(value);
        }
        thread::sleep(Duration::from_millis(2));
    }
    None
}

fn band(index: u8) -> AttackBand {
    AttackBand::from_index(index).unwrap()
}

/// The own result nearest `onset` in the published band results, once there is one.
fn own_near(runtime: &AttackRuntime, onset: i64) -> Option<AttackBandDetail> {
    runtime
        .band_results()
        .own()
        .iter()
        .find(|detail| (detail.event.event_sample - onset).abs() < 600)
        .copied()
}

#[test]
fn missing_cache_refresh_requires_the_full_original_analysis_range() {
    let detail = AttackBandDetail {
        event: AttackEvent {
            generation: 1,
            sample_rate: 48_000,
            channels: 2,
            definition_hash: [1; 32],
            event_sample: 20_000,
            decision_sample: 21_000,
            value: 1.0,
        },
        band: band(3),
        span_end_sample: 34_400,
        measure: None,
    };
    // Independent 250 Hz contract: 960 lead + 4*192 settle + 97 half-RMS = 1825
    // samples before onset, and 97 after requested end; one missing sample is insufficient.
    let mut ring = crate::attack_perception::band::AttackBandRing::new(48_000, 2);
    ring.push_block(18_175, &vec![0.0; (34_496 - 18_175) * 2]);
    assert!(!super::needs_refresh(&detail, Some(&ring), true));
    ring.push_block(34_496, &[0.0, 0.0]);
    assert!(super::needs_refresh(&detail, Some(&ring), true));
    ring.clear();
    ring.push_block(18_176, &vec![0.0; (34_497 - 18_176) * 2]);
    assert!(!super::needs_refresh(&detail, Some(&ring), false));
    assert!(!super::needs_refresh(&detail, None, false));
}

#[test]
fn all_keeps_no_ring_and_measures_nothing() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    assert!(runtime.set_enabled(true));
    feed(&runtime, &bursts(48_000, &[20_000]), 0);
    wait_for(Duration::from_secs(4), || {
        runtime.try_history().and_then(|history| {
            history
                .details()
                .next_back()
                .filter(|detail| detail.features.complete)
                .copied()
        })
    })
    .expect("a complete whole-signal detail");
    let stats = runtime.stats();
    assert_eq!(stats.band_measurements, 0);
    assert_eq!(stats.band_ring_frames, 0);
    assert_eq!(*runtime.band_results(), AttackBandResults::default());
    runtime.shutdown_and_join();
}

#[test]
fn each_hit_is_measured_once_after_its_tail() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    runtime.set_band(Some(band(3)));
    assert!(runtime.set_enabled(true));
    feed(&runtime, &bursts(48_000, &[20_000]), 0);
    let detail = wait_for(Duration::from_secs(4), || own_near(&runtime, 20_000))
        .expect("the hit measured in 250 Hz");
    assert!(detail.has_valid_layout());
    let measure = detail.measure.expect("its audio was kept");
    assert!(matches!(measure.sound, BandSound::Rises { .. }));
    assert!(
        (measure.level_dbfs + 9.0).abs() < 1.5,
        "{}",
        measure.level_dbfs
    );
    assert_eq!(runtime.stats().band_ring_frames, 7 * 48_000);
    // More audio and more worker passes measure nothing twice.
    let measured = runtime.stats().band_measurements;
    assert_eq!(measured, 1);
    feed(&runtime, &bursts(24_000, &[]), 48_000);
    thread::sleep(Duration::from_millis(300));
    assert_eq!(runtime.stats().band_measurements, measured);
    runtime.shutdown_and_join();
}

#[test]
fn a_band_switch_measures_the_kept_hits_again_even_when_stopped() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    assert!(runtime.set_enabled(true));
    // Hit A with no band chosen: nothing kept (D5).
    feed(&runtime, &bursts(48_000, &[20_000]), 0);
    wait_for(Duration::from_secs(4), || {
        runtime
            .try_history()
            .and_then(|history| history.details().next_back().copied())
            .filter(|detail| detail.features.complete)
    })
    .expect("hit A");
    runtime.set_band(Some(band(3)));
    // Hit B after the choice.
    feed(&runtime, &bursts(48_000, &[20_000]), 48_000);
    let b =
        wait_for(Duration::from_secs(4), || own_near(&runtime, 68_000)).expect("hit B in 250 Hz");
    assert!(b.measure.is_some());
    let a = own_near(&runtime, 20_000).expect("hit A is stated");
    assert_eq!(a.measure, None, "hit A came before the band was chosen");
    let measured = runtime.stats().band_measurements;

    // The transport has stopped. Choosing another band measures B again at once.
    thread::sleep(Duration::from_millis(250));
    runtime.set_band(Some(band(4)));
    // Newest first, within a budget per pass: B, then A, each stated in the new band.
    let (switched, before) = wait_for(Duration::from_secs(2), || {
        let results = runtime.band_results();
        (results.band == Some(band(4)))
            .then(|| own_near(&runtime, 68_000).zip(own_near(&runtime, 20_000)))
            .flatten()
    })
    .expect("hits B and A in 500 Hz without more audio");
    assert_eq!((switched.band, before.band), (band(4), band(4)));
    assert!(switched.measure.is_some());
    assert_eq!(before.measure, None);
    assert_eq!(runtime.stats().band_measurements, measured + 1);

    // ALL frees the ring and the results.
    runtime.set_band(None);
    wait_for(Duration::from_secs(1), || {
        (runtime.stats().band_ring_frames == 0).then_some(())
    })
    .expect("the ring freed");
    assert_eq!(runtime.band_results().band, None);
    runtime.shutdown_and_join();
}

#[test]
fn the_last_hits_before_the_audio_stops_are_finished_with_what_was_kept() {
    // A hit 200 ms before the audio ends: its 300 ms tail never comes, so once the audio has
    // stopped it is measured over what was kept, past its peak.
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    runtime.set_band(Some(band(3)));
    assert!(runtime.set_enabled(true));
    feed(&runtime, &bursts(49_600, &[40_000]), 0);
    let hit = wait_for(Duration::from_secs(3), || own_near(&runtime, 40_000))
        .expect("the hit finished once the audio stopped");
    let measure = hit.measure.expect("kept past its peak");
    assert_eq!(measure.span_end, BandSpanEnd::AudioEnd);
    assert!(measure.span_end_sample < 40_000 + 14_400);
    assert!(measure.has_valid_layout());
    runtime.shutdown_and_join();

    // A hit 100 ms before the end has not reached past its peak search: it is stated as not
    // kept rather than left waiting. The hit before it is measured over its whole window.
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    runtime.set_band(Some(band(3)));
    assert!(runtime.set_enabled(true));
    feed(&runtime, &bursts(49_600, &[20_000, 44_800]), 0);
    let early =
        wait_for(Duration::from_secs(3), || own_near(&runtime, 20_000)).expect("the earlier hit");
    assert_eq!(early.measure.unwrap().span_end, BandSpanEnd::Window);
    let late = wait_for(Duration::from_secs(2), || own_near(&runtime, 44_800))
        .expect("the late hit is stated");
    assert_eq!(late.measure, None);
    runtime.shutdown_and_join();
}

#[test]
fn post_measures_each_pre_onset_once_and_states_one_it_did_not_keep() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    runtime.set_band(Some(band(3)));
    assert!(runtime.set_enabled(true));
    feed(&runtime, &bursts(40_000, &[20_000]), 0);
    let identity = wait_for(Duration::from_secs(3), || {
        runtime
            .try_history()
            .and_then(|history| history.newest().copied())
    })
    .expect("frames");
    let anchor = |onset: i64| BandAnchor {
        event: AttackEvent {
            generation: identity.generation,
            sample_rate: 48_000,
            channels: 2,
            definition_hash: identity.definition_hash,
            event_sample: onset,
            decision_sample: onset + 1_000,
            value: 0.0,
        },
        span_end_sample: onset + 14_400,
        span_end: BandSpanEnd::Window,
    };
    let anchors = vec![anchor(-10_000), anchor(20_000)];
    runtime.request_band_anchors(Some(band(3)), anchors.clone());
    let measured = wait_for(Duration::from_secs(3), || {
        runtime.band_results().anchored_at(20_000, 34_400).copied()
    })
    .expect("POST at the PRE onset");
    assert_eq!(measured.event, anchor(20_000).event);
    assert_eq!(measured.measure.unwrap().span_end_sample, 34_400);
    let early = wait_for(Duration::from_secs(1), || {
        runtime.band_results().anchored_at(-10_000, 4_400).copied()
    })
    .expect("the onset before the ring is stated");
    assert_eq!(early.measure, None);
    // POST's own hit is measured too, in its own pass.
    wait_for(Duration::from_secs(2), || own_near(&runtime, 20_000)).expect("the own hit");
    // Asking again, as the exchange does every tick, measures nothing again.
    let count = runtime.stats().band_measurements;
    for _ in 0..20 {
        runtime.request_band_anchors(Some(band(3)), anchors.clone());
        thread::sleep(Duration::from_millis(5));
    }
    assert_eq!(runtime.stats().band_measurements, count);
    // Anchors for another band than the chosen one are not measured.
    runtime.request_band_anchors(Some(band(5)), vec![anchor(21_000)]);
    thread::sleep(Duration::from_millis(50));
    assert!(runtime.band_results().anchored_at(21_000, 35_400).is_none());
    runtime.shutdown_and_join();
}

/// CPU seconds this process has used so far (unix), for the cost report below.
#[cfg(unix)]
fn process_cpu_seconds() -> f64 {
    let mut usage: libc::rusage = unsafe { std::mem::zeroed() };
    // SAFETY: rusage is a plain C struct and RUSAGE_SELF is always valid.
    let result = unsafe { libc::getrusage(libc::RUSAGE_SELF, &mut usage) };
    assert_eq!(result, 0);
    let seconds = |time: libc::timeval| time.tv_sec as f64 + time.tv_usec as f64 / 1e6;
    seconds(usage.ru_utime) + seconds(usage.ru_stime)
}

/// Run with `--release -- --ignored --nocapture`: the worker's cost with no band (what ALL and
/// the main line pay) against the 63 Hz and 8 kHz bands, with POST's PRE onsets asked for as the
/// exchange asks for them, 30 times a second. Twenty seconds of two hits per second are pushed at
/// their real pace; the process's CPU time over each run is the cost.
#[cfg(unix)]
#[test]
#[ignore]
fn reports_the_worker_cost_with_and_without_a_band() {
    let seconds = 20_usize;
    let frames = 48_000 * seconds;
    let onsets = (0..seconds * 2).map(|hit| hit * 24_000).collect::<Vec<_>>();
    let samples = bursts(frames, &onsets);
    for chosen in [None, Some(band(1)), Some(band(8)), None] {
        let runtime = AttackRuntime::new(48_000, 2).unwrap();
        runtime.set_band(chosen);
        assert!(runtime.set_enabled(true));
        let cpu_before = process_cpu_seconds();
        let started = Instant::now();
        let mut position = 0_usize;
        let mut next_request = Instant::now();
        while position < frames {
            let count = 1_024.min(frames - position);
            assert!(runtime.push_block_from_audio(
                &samples[position * 2..(position + count) * 2],
                2,
                Some(position as i64),
            ));
            position += count;
            if chosen.is_some() && Instant::now() >= next_request {
                // What the POST exchange asks for: every confirmed onset so far, again.
                next_request += Duration::from_millis(33);
                if let Some(history) = runtime.try_history() {
                    let anchors = history
                        .events()
                        .map(|event| BandAnchor {
                            event: *event,
                            span_end_sample: event.event_sample + 14_400,
                            span_end: BandSpanEnd::Window,
                        })
                        .collect();
                    runtime.request_band_anchors(chosen, anchors);
                }
            }
            let due = started + Duration::from_micros((position as u64 * 1_000_000) / 48_000);
            if let Some(remaining) = due.checked_duration_since(Instant::now()) {
                thread::sleep(remaining);
            }
        }
        thread::sleep(Duration::from_millis(500));
        let cpu = process_cpu_seconds() - cpu_before;
        let stats = runtime.stats();
        let results = runtime.band_results();
        assert_eq!(stats.dropped_blocks, 0);
        println!(
            "band {:>4}: {cpu:.3} s CPU for {seconds} s of audio = {:.2} % of one core; {} measurements for {} own and {} anchored results",
            chosen.map_or("none", AttackBand::nominal_label),
            cpu / seconds as f64 * 100.0,
            stats.band_measurements,
            results.own().len(),
            results.anchored().len(),
        );
        runtime.shutdown_and_join();
    }
}

#[test]
fn audio_end_background_hit_refreshes_only_its_original_window_after_contiguous_resume() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    runtime.set_band(Some(band(3)));
    assert!(runtime.set_enabled(true));
    // Split one immutable waveform so the resumed boundary cannot invent a second onset.
    let audio = bursts(73_600, &[40_000]);
    feed(&runtime, &audio[..49_600 * 2], 0);
    let partial = wait_for(Duration::from_secs(3), || {
        own_near(&runtime, 40_000).filter(|d| {
            d.measure
                .is_some_and(|m| m.span_end == BandSpanEnd::AudioEnd)
        })
    })
    .expect("partial background measurement at audio end");
    let measured = runtime.stats().band_measurements;
    feed(&runtime, &audio[49_600 * 2..61_600 * 2], 49_600);
    let complete = wait_for(Duration::from_secs(3), || {
        own_near(&runtime, 40_000)
            .filter(|d| d.measure.is_some_and(|m| m.span_end == BandSpanEnd::Window))
    })
    .unwrap_or_else(|| {
        panic!(
            "same hit must complete; partial {partial:?}, latest {:?}, stats {:?}",
            own_near(&runtime, 40_000),
            runtime.stats()
        )
    });
    assert_eq!(complete.event, partial.event);
    assert_eq!(complete.span_end_sample, partial.span_end_sample);
    assert_eq!(
        complete.measure.unwrap().span_end_sample,
        complete.span_end_sample
    );
    assert_eq!(runtime.stats().band_measurements, measured + 1);
    feed(&runtime, &audio[61_600 * 2..], 61_600);
    thread::sleep(Duration::from_millis(150));
    assert_eq!(own_near(&runtime, 40_000).unwrap(), complete);
    assert_eq!(runtime.stats().band_measurements, measured + 1);
    runtime.shutdown_and_join();
}
