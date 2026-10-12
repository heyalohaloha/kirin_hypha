use super::*;
use std::time::{Duration, Instant};

#[test]
fn post_only_selection_uses_the_next_local_hit_for_bands_and_all() {
    let runtime = kirin_measure::AttackRuntime::new(48000, 2).unwrap();
    assert!(runtime.set_enabled(true));
    for position in (0..48000).step_by(512) {
        let audio: Vec<_> = (position..position + 512)
            .flat_map(|sample| {
                let value: f64 = [20000, 24000]
                    .into_iter()
                    .map(|onset| {
                        let t = (sample as f64 - f64::from(onset)) / 48000.0;
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
                    .sum();
                [value as f32, value as f32]
            })
            .collect();
        assert!(runtime.push_block_from_audio(&audio, 2, Some(position)));
        std::thread::sleep(Duration::from_millis(1));
    }
    let deadline = Instant::now() + Duration::from_secs(3);
    let history = loop {
        if let Some(history) = runtime.try_history().filter(|h| h.events().len() >= 2) {
            break history;
        }
        assert!(Instant::now() < deadline, "two local fixture hits");
        std::thread::sleep(Duration::from_millis(2));
    };
    let events: Vec<_> = history.events().copied().collect();
    let source = runtime
        .try_observation_snapshot()
        .unwrap()
        .source
        .unwrap()
        .source;
    for band in [None, AttackBand::from_index(3)] {
        let (end, reason) = post_only_window(&history, source, events[0].event_sample, band);
        assert!(end > events[0].event_sample && end <= events[1].event_sample);
        assert_eq!(reason, BandSpanEnd::NextHit);
        let foreign = AttackSourceKey {
            generation: source.generation + 1,
            ..source
        };
        let (end, reason) = post_only_window(&history, foreign, events[0].event_sample, band);
        assert!(end > events[1].event_sample);
        assert_eq!(reason, BandSpanEnd::Window);
    }
    runtime.shutdown_and_join();
}
