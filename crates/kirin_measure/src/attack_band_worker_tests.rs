use std::thread;
use std::time::{Duration, Instant};

use super::super::*;
use crate::attack_perception::band::AttackBand;

/// A 250 Hz burst at `onset` (48 kHz stereo): silence before it, a 6 ms rise, a 45 ms decay.
fn burst_frames(total: usize, onset: usize) -> Vec<f32> {
    let mut samples = Vec::with_capacity(total * 2);
    for frame in 0..total {
        let t = (frame as f64 - onset as f64) / 48_000.0;
        let value = if t < 0.0 {
            0.0
        } else {
            let envelope = if t < 0.006 {
                t / 0.006
            } else {
                (-(t - 0.006) / 0.045).exp()
            };
            0.5 * envelope * (std::f64::consts::TAU * 250.0 * t).sin()
        } as f32;
        samples.push(value);
        samples.push(value);
    }
    samples
}

fn feed(runtime: &AttackRuntime, samples: &[f32], block: usize) {
    let mut position = 0;
    let frames = samples.len() / 2;
    while position < frames {
        let count = block.min(frames - position);
        assert!(runtime.push_block_from_audio(
            &samples[position * 2..(position + count) * 2],
            2,
            Some(position as i64),
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

#[test]
fn a_chosen_band_measures_each_hit_after_its_tail_and_all_costs_no_ring() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    assert!(runtime.set_enabled(true));
    assert_eq!(runtime.band(), None);
    // Phase D settles 300 ms into a run; the hit comes after that and its tail needs 300 ms
    // more, then the onset decisions past it.
    // Blocks of 1 024 frames: 47 of them fit the 128-slot ingress ring while the worker warms up.
    let samples = burst_frames(48_000, 20_000);
    feed(&runtime, &samples, 1_024);
    let plain = wait_for(Duration::from_secs(4), || {
        runtime.try_history().and_then(|history| {
            history
                .details()
                .next_back()
                .copied()
                .filter(|detail| detail.features.complete)
        })
    })
    .expect("a complete detail");
    assert!(
        runtime.band_ring.lock().unwrap().is_none(),
        "ALL keeps no ring"
    );
    assert_eq!(runtime.try_history().unwrap().band_details().len(), 0);

    // Choosing a band after the hit measures nothing retroactively (D5); the next hit is measured.
    runtime.set_band(AttackBand::from_index(3));
    assert_eq!(runtime.band().map(AttackBand::index), Some(3));
    let second = burst_frames(48_000, 20_000);
    let mut position = 48_000_i64;
    for chunk in second.chunks(2_048) {
        assert!(runtime.push_block_from_audio(chunk, 2, Some(position)));
        position += (chunk.len() / 2) as i64;
        thread::sleep(Duration::from_millis(1));
    }
    let detail = wait_for(Duration::from_secs(4), || {
        runtime
            .try_history()
            .and_then(|history| history.band_details().next_back().copied())
    })
    .expect("the band detail of the second hit");
    assert!(detail.has_valid_layout());
    assert_eq!(detail.measure.band.index(), 3);
    // The onset decision sits within one ODF window of the burst's start.
    assert!(
        (detail.event.event_sample - (48_000 + 20_000)).abs() < 600,
        "onset {}",
        detail.event.event_sample
    );
    assert!(detail.measure.release_frames.is_some());
    assert!(detail.measure.arrival_frames.is_some());
    assert!(
        (detail.measure.level_dbfs + 9.0).abs() < 1.5,
        "{}",
        detail.measure.level_dbfs
    );
    assert!(runtime.band_ring.lock().unwrap().is_some());
    // The plain detail is what it was: the band adds and changes nothing.
    let history = runtime.try_history().unwrap();
    let first = history
        .details()
        .find(|current| current.event == plain.event)
        .copied();
    assert_eq!(first, Some(plain));

    // Changing the band drops the ring and the band history; ALL frees the ring.
    runtime.set_band(AttackBand::from_index(4));
    assert_eq!(runtime.try_history().unwrap().band_details().len(), 0);
    runtime.set_band(None);
    assert!(runtime.band_ring.lock().unwrap().is_none());
    runtime.shutdown_and_join();
}

#[test]
fn post_measures_a_pre_onset_from_its_ring_once_the_audio_is_there() {
    let runtime = AttackRuntime::new(48_000, 2).unwrap();
    runtime.set_band(AttackBand::from_index(3));
    assert!(runtime.set_enabled(true));
    let samples = burst_frames(40_000, 20_000);
    feed(&runtime, &samples, 1_024);
    let identity = wait_for(Duration::from_secs(3), || {
        runtime
            .try_history()
            .and_then(|history| history.newest().copied())
    })
    .expect("frames");
    let anchor = AttackEvent {
        generation: identity.generation,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: identity.definition_hash,
        event_sample: 20_000,
        decision_sample: 21_000,
        value: 0.0,
    };
    let band = AttackBand::from_index(3).unwrap();
    // The span ends at +300 ms: 34 400. The ring holds 40 000 frames once every block is in.
    let measured = wait_for(Duration::from_secs(3), || {
        let details = runtime.band_details_at(band, &[(anchor, 20_000 + 14_400)]);
        details.first().copied()
    })
    .expect("POST band detail at the PRE onset");
    assert_eq!(measured.event, anchor);
    assert_eq!(measured.measure.span_end_sample, 34_400);
    assert!(measured.measure.release_frames.is_some());
    // Another band, or an onset whose audio is not retained, gives nothing.
    assert!(runtime
        .band_details_at(AttackBand::from_index(4).unwrap(), &[(anchor, 34_400)])
        .is_empty());
    let early = AttackEvent {
        event_sample: -10_000,
        ..anchor
    };
    assert!(runtime.band_details_at(band, &[(early, 4_400)]).is_empty());
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
/// today's main pay) against the 63 Hz and 8 kHz bands. Twenty seconds of two hits per second
/// are pushed at their real pace; the process's CPU time over each run is the cost.
#[cfg(unix)]
#[test]
#[ignore]
fn reports_the_worker_cost_with_and_without_a_band() {
    let seconds = 20_usize;
    let frames = 48_000 * seconds;
    let mut samples = Vec::with_capacity(frames * 2);
    for frame in 0..frames {
        let since_hit = (frame % 24_000) as f64 / 48_000.0;
        let envelope = if since_hit < 0.006 {
            since_hit / 0.006
        } else {
            (-(since_hit - 0.006) / 0.045).exp()
        };
        let value = (0.5 * envelope * (std::f64::consts::TAU * 250.0 * since_hit).sin()) as f32;
        samples.push(value);
        samples.push(value);
    }
    for band in [
        None,
        AttackBand::from_index(1),
        AttackBand::from_index(8),
        None,
    ] {
        let runtime = AttackRuntime::new(48_000, 2).unwrap();
        runtime.set_band(band);
        assert!(runtime.set_enabled(true));
        let cpu_before = process_cpu_seconds();
        let started = Instant::now();
        let mut position = 0_usize;
        while position < frames {
            let count = 1_024.min(frames - position);
            assert!(runtime.push_block_from_audio(
                &samples[position * 2..(position + count) * 2],
                2,
                Some(position as i64),
            ));
            position += count;
            let due = started + Duration::from_micros((position as u64 * 1_000_000) / 48_000);
            if let Some(remaining) = due.checked_duration_since(Instant::now()) {
                thread::sleep(remaining);
            }
        }
        let analysed = wait_for(Duration::from_secs(10), || {
            runtime
                .try_history()
                .and_then(|history| history.newest().copied())
                .filter(|newest| newest.event_sample >= frames as i64 - 4_096)
        });
        let cpu = process_cpu_seconds() - cpu_before;
        let (hits, band_hits) = runtime.try_history().map_or((0, 0), |history| {
            (history.details().len(), history.band_details().len())
        });
        assert_eq!(runtime.stats().dropped_blocks, 0);
        println!(
            "band {:>4}: {cpu:.3} s CPU for {seconds} s of audio = {:.2} % of one core; {hits} hits, {band_hits} band hits; analysed {}",
            band.map_or("none", AttackBand::nominal_label),
            cpu / seconds as f64 * 100.0,
            analysed.is_some()
        );
        runtime.shutdown_and_join();
    }
}
