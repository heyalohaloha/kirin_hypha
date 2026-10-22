#[path = "attack_exchange_fixture.rs"]
mod fixture;
use std::sync::Arc;

use super::*;
use crate::AttackPairEventKind;

fn push_impulse_pair(
    pre: &AttackRuntime,
    post: &AttackRuntime,
    frames: usize,
    impulse_at: usize,
    renew: &mut dyn FnMut(),
) {
    const BLOCK_FRAMES: usize = 256;
    let mut position = 0;
    while position < frames {
        let count = BLOCK_FRAMES.min(frames - position);
        let mut pre_samples = Vec::with_capacity(count * 2);
        let mut post_samples = Vec::with_capacity(count * 2);
        for offset in 0..count {
            let absolute = position + offset;
            let pre_value = if absolute == impulse_at { 0.9 } else { 0.0 };
            let post_value = if absolute == impulse_at { 0.45 } else { 0.0 };
            pre_samples.extend_from_slice(&[pre_value, pre_value]);
            post_samples.extend_from_slice(&[post_value, post_value]);
        }
        assert!(pre.push_block_from_audio(&pre_samples, 2, Some(position as i64)));
        assert!(post.push_block_from_audio(&post_samples, 2, Some(position as i64)));
        position += count;
        pre.wait_for_input_for_test();
        post.wait_for_input_for_test();
        renew();
    }
}

fn histories_are_ready(pre: &AttackRuntime, post: &AttackRuntime) -> bool {
    [pre, post].into_iter().all(|runtime| {
        runtime.try_history().is_some_and(|history| {
            history.details().next_back().is_some()
                && history
                    .waveform()
                    .next_back()
                    .is_some_and(|point| point.end_sample >= 24_000)
        })
    })
}

#[test]
fn exact_pair_transports_real_pre_and_post_attack_histories_end_to_end() {
    let fixture = fixture::Fixture::new();
    let fixture::Fixture {
        pre,
        post,
        pre_attack,
        post_attack,
        clock,
        pre_dir,
        target,
        ..
    } = &fixture;
    let _ = clock;

    assert!(post.set_post_analysis_mode(AnalysisViewMode::Attack));
    post.set_post_visible(true);
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", pre_dir));
    assert!(pre_attack.is_enabled());
    assert!(post_attack.is_enabled());

    // A detail waits for its 130 ms windows and for every onset before the body end.
    let mut renew = || {
        assert!(post.post_tick("post", Some(target.clone())));
        assert!(pre.pre_tick("pre", pre_dir));
    };
    push_impulse_pair(pre_attack, post_attack, 24_000, 8_000, &mut renew);
    post_attack.wait_for_state_for_test(|| histories_are_ready(pre_attack, post_attack));
    assert!(histories_are_ready(pre_attack, post_attack));

    assert!(pre.pre_tick("pre", pre_dir));
    assert!(post.post_tick("post", Some(target.clone())));
    let view = post.try_attack_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::Active);
    let pre_history = view.pre.expect("transported PRE ATTACK history");
    let post_history = view.post.expect("local POST ATTACK history");
    assert_eq!(
        pre_history.waveform().next_back().unwrap().end_sample,
        post_history.waveform().next_back().unwrap().end_sample
    );
    assert!(
        pre_history
            .details()
            .next_back()
            .unwrap()
            .features
            .sample_peak_dbfs
            > post_history
                .details()
                .next_back()
                .unwrap()
                .features
                .sample_peak_dbfs
    );
    assert!(view
        .pair_events
        .iter()
        .any(|event| event.kind == crate::AttackPairEventKind::Matched));
    // POST is measured at the PRE onset over the PRE detail's exact windows (B-1016).
    let pre_detail = *pre_history.details().next_back().unwrap();
    let anchored = view
        .post_anchored
        .iter()
        .find(|detail| detail.event.event_sample == pre_detail.event.event_sample)
        .expect("POST measured at the PRE onset");
    assert_eq!(
        anchored.features.window_start_sample,
        pre_detail.features.window_start_sample
    );
    assert_eq!(
        anchored.features.body_end_sample,
        pre_detail.features.body_end_sample
    );
    assert_eq!(
        anchored.event.generation,
        post_history.newest().unwrap().generation
    );
    assert!(
        (anchored.features.attack_rms_dbfs - pre_detail.features.attack_rms_dbfs + 6.020_6).abs()
            < 1e-3
    );
}

#[test]
fn a_pairing_flicker_keeps_the_post_attack_history() {
    // With the transport stopped nothing new is measured, so HOLD shows the last six seconds only
    // if a pairing that drops for one tick and comes back does not restart POST's own ATTACK run.
    let fixture = fixture::Fixture::new();
    let fixture::Fixture {
        pre,
        post,
        pre_attack,
        post_attack,
        clock,
        pre_dir,
        target,
        ..
    } = &fixture;
    let _ = clock;
    assert!(post.set_post_analysis_mode(AnalysisViewMode::Attack));
    post.set_post_visible(true);
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", pre_dir));
    push_impulse_pair(pre_attack, post_attack, 24_000, 8_000, &mut || {});
    post_attack.wait_for_state_for_test(|| histories_are_ready(pre_attack, post_attack));
    assert!(histories_are_ready(pre_attack, post_attack));
    let before = post_attack.try_history().unwrap();

    assert!(
        post.post_tick("post", None),
        "the pairing drops for one tick"
    );
    assert!(
        post.post_tick("post", Some(target.clone())),
        "and comes back"
    );
    let after = post_attack.try_history().unwrap();
    assert_eq!(
        after.frames().count(),
        before.frames().count(),
        "POST keeps its measured ATTACK history"
    );
    assert_eq!(after.details().count(), before.details().count());
    assert_eq!(after.newest(), before.newest());

    // The new pair session joins the kept POST history with PRE again.
    let mut status = SpectrumViewStatus::WarmingUp;
    post_attack.wait_for_state_for_test(|| {
        assert!(pre.pre_tick("pre", pre_dir));
        let _ = post.post_tick("post", Some(target.clone()));
        status = post.try_attack_view().map_or(status, |view| view.status);
        status == SpectrumViewStatus::Active
    });
    assert_eq!(status, SpectrumViewStatus::Active);
}

/// A 500 Hz burst at `onset` on PRE, and `post_delay` frames later on POST at half the amplitude:
/// the two detectors then report different onsets for the same hit, as a real chain makes them.
fn push_burst_pair(
    pre: &AttackRuntime,
    post: &AttackRuntime,
    frames: usize,
    onset: usize,
    post_delay: usize,
    renew: &mut dyn FnMut(),
) {
    // 1 024-frame blocks: a second of audio fits the 128-slot ingress ring while the worker warms up.
    const BLOCK_FRAMES: usize = 1_024;
    let value_at = |absolute: usize, start: usize, amplitude: f64| -> f32 {
        let t = (absolute as f64 - start as f64) / 48_000.0;
        if t < 0.0 {
            return 0.0;
        }
        let envelope = if t < 0.006 {
            t / 0.006
        } else {
            (-(t - 0.006) / 0.045).exp()
        };
        (amplitude * envelope * (std::f64::consts::TAU * 500.0 * t).sin()) as f32
    };
    let mut position = 0;
    while position < frames {
        let count = BLOCK_FRAMES.min(frames - position);
        let mut pre_samples = Vec::with_capacity(count * 2);
        let mut post_samples = Vec::with_capacity(count * 2);
        for offset in 0..count {
            let pre_value = value_at(position + offset, onset, 0.5);
            let post_value = value_at(position + offset, onset + post_delay, 0.25);
            pre_samples.extend_from_slice(&[pre_value, pre_value]);
            post_samples.extend_from_slice(&[post_value, post_value]);
        }
        assert!(pre.push_block_from_audio(&pre_samples, 2, Some(position as i64)));
        assert!(post.push_block_from_audio(&post_samples, 2, Some(position as i64)));
        position += count;
        pre.wait_for_input_for_test();
        post.wait_for_input_for_test();
        renew();
    }
}

#[test]
fn a_chosen_band_rides_the_request_and_post_measures_each_pre_onset_once() {
    let fixture = fixture::Fixture::new();
    let fixture::Fixture {
        pre,
        post,
        pre_attack,
        post_attack,
        clock,
        pre_dir,
        target,
        ..
    } = &fixture;
    let _ = clock;
    let band = crate::attack_perception::band::AttackBand::from_index(4).unwrap();

    assert!(post.set_post_analysis_mode(AnalysisViewMode::Attack));
    post.set_post_visible(true);
    post_attack.set_band(Some(band));
    post.set_post_attack_band(Some(band));
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", pre_dir));
    assert_eq!(
        pre_attack.band(),
        Some(band),
        "the request carried the band to PRE"
    );

    // POST hears the hit 15 ms after PRE: more than one detector hop, within the 25 ms match, so
    // its own onset differs from PRE's while the two still pair.
    push_burst_pair(pre_attack, post_attack, 48_000, 20_000, 720, &mut || {
        assert!(post.post_tick("post", Some(target.clone())));
        assert!(pre.pre_tick("pre", pre_dir));
    });
    let mut found = None;
    post_attack.wait_for_state_for_test(|| {
        assert!(pre.pre_tick("pre", pre_dir));
        assert!(post.post_tick("post", Some(target.clone())));
        found = post
            .try_attack_view()
            .filter(|view| view.pre_band == AttackPreBand::Same)
            .and_then(|view| {
                let pair = *view
                    .pair_events
                    .iter()
                    .find(|pair| pair.kind == AttackPairEventKind::Matched)?;
                let pre_detail = *view
                    .pre_band_results
                    .as_ref()?
                    .own_at(pair.pre_event_sample?)?;
                let measure = pre_detail.measure?;
                let post_results = post_attack.band_results();
                if post_results.band != Some(band)
                    || post_results.generation != pair.post_generation
                {
                    return None;
                }
                // The pair chooses a candidate; the own worker measures its emitted event.
                let own_event = {
                    let mut own_events = view.post.as_ref()?.events();
                    let event = *own_events.next()?;
                    if own_events.next().is_some() {
                        return None;
                    }
                    event
                };
                if own_event.generation != pair.post_generation {
                    return None;
                }
                // Both kinds share the counter; an anchor can be published before the own hit.
                post_results.own_at(own_event.event_sample)?.measure?;
                let post_detail =
                    *post_results.anchored_at(pair.pre_event_sample?, measure.span_end_sample)?;
                Some((view, pair, pre_detail, post_detail))
            });
        found.is_some()
    });
    let (view, pair, pre_detail, post_detail) = found.expect("a paired band measure");
    assert_eq!(view.status, SpectrumViewStatus::Active);
    assert_eq!(view.band, Some(band));
    assert_ne!(
        pair.pre_event_sample, pair.post_event_sample,
        "the two sides report their own onsets"
    );
    // POST is measured at the PRE onset over the PRE tail: the same content samples.
    let pre_measure = pre_detail.measure.unwrap();
    let post_measure = post_detail.measure.unwrap();
    assert_eq!(post_measure.event_sample, pre_measure.event_sample);
    assert_eq!(post_measure.span_end_sample, pre_measure.span_end_sample);
    assert!((post_measure.level_dbfs - pre_measure.level_dbfs + 6.02).abs() < 0.3);
    let delay_ms = crate::attack_perception::band::band_delay_frames(&pre_measure, &post_measure)
        .unwrap()
        * 1_000.0
        / 48_000.0;
    assert!(
        (delay_ms - 15.0).abs() < 0.5,
        "POST 15 ms late: {delay_ms} ms"
    );

    // Every further tick asks for the same onsets again; none is measured twice.
    let measured = post_attack.stats().band_measurements;
    assert_eq!(measured, 2, "one own hit and one PRE-anchored hit");
    for _ in 0..30 {
        assert!(pre.pre_tick("pre", pre_dir));
        assert!(post.post_tick("post", Some(target.clone())));
        post_attack.wait_for_cycle_for_test();
    }
    assert_eq!(post_attack.stats().band_measurements, measured);

    // ALL on POST: the next request drops the band and PRE stops measuring it.
    post_attack.set_band(None);
    post.set_post_attack_band(None);
    clock.advance(REQUEST_RENEW_INTERVAL);
    assert!(post.post_tick("post", Some(target.clone())));
    assert!(pre.pre_tick("pre", pre_dir));
    assert_eq!(pre_attack.band(), None);
}
