//! Every output shares the same qualified record: keys, sides, envelopes and the summary.

use super::super::map::{to_c_envelope, Source};
use super::super::summary::summarise;
use super::*;

fn flat(mut detail: AttackBandDetail) -> AttackBandDetail {
    let measure = detail.measure.as_mut().unwrap();
    let level = (measure.level_dbfs * 100.0) as i16;
    measure.envelope.head.fill(level);
    measure.envelope.tail.fill(level);
    detail
}

fn fixture() -> (AttackPairViewSnapshot, AttackBandResults) {
    let (mut view, mut post) = shifted_view(AttackPreBand::Same);
    let pre = Arc::make_mut(view.pre_band_results.as_mut().unwrap());
    assert!(pre.put_own(flat(rises(5, 10_000, -12.0))));
    assert!(post.put_anchored(flat(rises(7, 10_000, -18.0))));
    (view, post)
}

fn summary(hits: &[Source<'_>]) -> KirinAttackBandSummary {
    let mut result = KirinAttackBandSummary::default();
    summarise(
        &mut result,
        hits,
        true,
        band3().resolution_micros() as f32 / 1_000.0,
    );
    result
}

#[test]
fn same_run_keeps_the_exact_keys_values_envelopes_and_summary() {
    let (view, post) = fixture();
    let hits = sources(&view, None, &post, band3());
    assert_eq!(hits.len(), view.pair_events.len());
    for (hit, pair) in hits.iter().zip(&view.pair_events) {
        assert_eq!(hit.hit.event_sample, pair.event_sample);
    }
    assert_eq!(hits[0].hit.measured_at_sample, 10_000);
    assert_eq!(
        (hits[0].hit.pre.level_dbfs, hits[0].hit.post.level_dbfs),
        (-12.0, -18.0)
    );
    assert_eq!(to_c_envelope(hits[0].pre).head_dbfs, [-12.0; 96]);
    assert_eq!(to_c_envelope(hits[0].post).tail_dbfs, [-18.0; 64]);
    let result = summary(&hits);
    assert_eq!((result.count, result.event_samples[0]), (1, 10_150));
    assert_eq!(result.lanes[3].median, -6.0);
    assert_eq!(result.pre.head_dbfs, [-12.0; 96]);
    assert_eq!(result.post.tail_dbfs, [-18.0; 64]);
}

#[test]
fn another_post_generation_never_fills_the_same_keys() {
    for generation in [6, 8] {
        let (mut view, post) = fixture();
        for pair in &mut view.pair_events {
            pair.post_generation = generation;
        }
        let hits = sources(&view, None, &post, band3());
        assert_eq!(hits[0].hit.pre.state, KIRIN_ATTACK_BAND_SIDE_RISES);
        for index in [0, 2] {
            assert_eq!(hits[index].hit.post.state, KIRIN_ATTACK_BAND_SIDE_PENDING);
            assert!(hits[index].post.is_none());
            assert_eq!(to_c_envelope(hits[index].post).head_dbfs, [0.0; 96]);
        }
        assert_eq!(summary(&hits).count, 0);
    }
}

#[test]
fn another_pre_generation_cannot_supply_a_post_anchor_tail() {
    for generation in [4, 6] {
        let (mut view, post) = fixture();
        for pair in &mut view.pair_events {
            pair.pre_generation = generation;
        }
        let hits = sources(&view, None, &post, band3());
        for index in [0, 1] {
            assert_eq!(hits[index].hit.pre.state, KIRIN_ATTACK_BAND_SIDE_PENDING);
            assert!(hits[index].pre.is_none());
        }
        assert_eq!(hits[0].hit.post.state, KIRIN_ATTACK_BAND_SIDE_PENDING);
        assert!(
            hits[0].post.is_none(),
            "no POST result through the old PRE tail"
        );
        assert_eq!(hits[2].hit.post.state, KIRIN_ATTACK_BAND_SIDE_RISES);
        assert_eq!(hits[2].hit.post.level_dbfs, -20.0);
        assert_eq!(summary(&hits).count, 0);
    }
}

#[test]
fn another_rate_layout_or_definition_cannot_fill_either_side() {
    for mismatch in 0..3 {
        let (mut view, post) = fixture();
        for pair in &mut view.pair_events {
            match mismatch {
                0 => pair.sample_rate = 96_000,
                1 => pair.channels = 1,
                _ => pair.definition_hash = [8; 32],
            }
        }
        let hits = sources(&view, None, &post, band3());
        assert_eq!(hits.len(), 4, "the lanes' keys do not disappear");
        assert_eq!(hits[0].hit.event_sample, 10_150);
        assert_eq!(hits[0].hit.pre.state, KIRIN_ATTACK_BAND_SIDE_PENDING);
        assert_eq!(hits[0].hit.post.state, KIRIN_ATTACK_BAND_SIDE_PENDING);
        assert_eq!(hits[2].hit.post.state, KIRIN_ATTACK_BAND_SIDE_PENDING);
        assert!(hits
            .iter()
            .all(|hit| hit.pre.is_none() && hit.post.is_none()));
        assert_eq!(summary(&hits).count, 0);
    }
}

#[test]
fn post_own_fallback_also_qualifies_the_post_run() {
    let (mut view, post) = shifted_view(AttackPreBand::Waiting);
    let hits = sources(&view, None, &post, band3());
    assert_eq!(hits[0].hit.measured_at_sample, 10_300);
    assert_eq!(hits[0].hit.post.level_dbfs, -18.5);
    for pair in &mut view.pair_events {
        pair.post_generation = 8;
    }
    let hits = sources(&view, None, &post, band3());
    assert_eq!(hits[0].hit.measured_at_sample, 10_300);
    assert_eq!(hits[0].hit.post.state, KIRIN_ATTACK_BAND_SIDE_PENDING);
    assert!(hits[0].post.is_none());
}

#[test]
fn unpaired_seek_same_onset_refuses_the_previously_read_results_arc() {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    let band = AttackBand::from_index(4).unwrap();
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(engine.set_attack_enabled(true));
    assert!(engine.set_attack_band(band.index()));
    feed_burst(&engine);
    let deadline = Instant::now() + Duration::from_secs(4);
    let runtime = engine.attack_runtime.as_ref().unwrap();
    let (old_history, old_results, old_detail) = loop {
        let results = runtime.band_results();
        let found = runtime.try_history().and_then(|history| {
            let event = history.details().next()?.event;
            let detail = *results.own_at(event.event_sample)?;
            (detail.measure.is_some() && detail.event.generation == event.generation)
                .then_some((history, results, detail))
        });
        if let Some(found) = found {
            break found;
        }
        assert!(
            Instant::now() < deadline,
            "no old band record within four seconds"
        );
        thread::sleep(Duration::from_millis(5));
    };
    let view = AttackPairViewSnapshot {
        status: SpectrumViewStatus::NoPair,
        ..Default::default()
    };
    let old = sources(&view, Some(&old_history), &old_results, band);
    assert_eq!(old[0].hit.post.state, KIRIN_ATTACK_BAND_SIDE_RISES);
    assert_eq!(
        old[0].hit.post.level_dbfs,
        old_detail.measure.unwrap().level_dbfs
    );
    // The reader kept its old Arc, then a project seek produced new whole-signal history.
    feed_burst(&engine);
    let history = loop {
        if let Some(history) = runtime.try_history() {
            if history
                .newest()
                .is_some_and(|frame| frame.generation > old_detail.event.generation)
                && history.details().next().is_some()
            {
                break history;
            }
        }
        assert!(
            Instant::now() < deadline,
            "no seek history within four seconds"
        );
        thread::sleep(Duration::from_millis(5));
    };
    let identity = history.details().next().unwrap().event;
    assert_eq!(
        identity.event_sample, old_detail.event.event_sample,
        "the onset repeats"
    );
    let hits = sources(&view, Some(&history), &old_results, band);
    assert_eq!(hits[0].hit.event_sample, old[0].hit.event_sample);
    assert!(hits
        .iter()
        .all(|hit| hit.post.is_none() && hit.hit.post.state == KIRIN_ATTACK_BAND_SIDE_PENDING));
    let mut result = KirinAttackBandSummary::default();
    summarise(&mut result, &hits, false, 2.0);
    assert_eq!(result.count, 0);
    assert_eq!(to_c_envelope(hits[0].post).head_dbfs, [0.0; 96]);
    // Same key and values become usable only under the new run's identity.
    let mut renewed = old_detail;
    renewed.event = identity;
    let mut current = AttackBandResults::new(Some(band), identity.generation);
    assert!(current.put_own(renewed));
    let hits = sources(&view, Some(&history), &current, band);
    assert_eq!(hits[0].hit.post.state, KIRIN_ATTACK_BAND_SIDE_RISES);
    assert_eq!(
        hits[0].hit.post.level_dbfs,
        old_detail.measure.unwrap().level_dbfs
    );
}
