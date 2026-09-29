use std::mem::{offset_of, size_of};

use kirin_measure::attack_perception::band::{
    AttackBand, AttackBandMeasure, BandArrival, BandEnvelope, BandRelease, BandSound, BandSpanEnd,
};
use kirin_measure::attack_runtime::AttackBandDetail;
use kirin_measure::{AttackEvent, PluginDataRole};

use super::super::map::Source;
use super::*;

#[test]
fn summary_c_layout_is_fixed() {
    assert_eq!(size_of::<KirinAttackBandLaneSummary>(), 52);
    assert_eq!(offset_of!(KirinAttackBandLaneSummary, withheld), 3);
    assert_eq!(offset_of!(KirinAttackBandLaneSummary, median), 4);
    assert_eq!(offset_of!(KirinAttackBandLaneSummary, values), 20);
    assert_eq!(offset_of!(KirinAttackBandSummary, count), 4);
    assert_eq!(offset_of!(KirinAttackBandSummary, generation), 16);
    assert_eq!(offset_of!(KirinAttackBandSummary, event_samples), 32);
    assert_eq!(offset_of!(KirinAttackBandSummary, lanes), 96);
    assert_eq!(offset_of!(KirinAttackBandSummary, pre_arrival_ms), 304);
    assert_eq!(offset_of!(KirinAttackBandSummary, pre), 320);
    assert_eq!(offset_of!(KirinAttackBandSummary, post_high), 320 + 3 * 640);
    assert_eq!(size_of::<KirinAttackBandSummary>(), 320 + 4 * 640);
}

fn band() -> AttackBand {
    AttackBand::from_index(1).unwrap()
}

/// A side that rises: arrival, ATT, peak, release (AT) and level as given.
fn side(arrival: f32, attack: f32, release: f32, level: f32) -> KirinAttackBandSide {
    KirinAttackBandSide {
        state: KIRIN_ATTACK_BAND_SIDE_RISES,
        arrival_state: KIRIN_ATTACK_BAND_ARRIVAL_AT,
        release_state: KIRIN_ATTACK_BAND_RELEASE_AT,
        reserved: 0,
        peak_ms: arrival + attack,
        arrival_ms: arrival,
        attack_ms: attack,
        release_ms: release,
        level_dbfs: level,
    }
}

fn state(state: u8) -> KirinAttackBandSide {
    KirinAttackBandSide {
        state,
        ..Default::default()
    }
}

/// A band detail whose envelope is flat at `level` dBFS, for the averages.
fn detail(onset: i64, level: f32) -> AttackBandDetail {
    let centi = (level * 100.0) as i16;
    AttackBandDetail {
        event: AttackEvent {
            generation: 7,
            sample_rate: 48_000,
            channels: 2,
            definition_hash: [7; 32],
            event_sample: onset,
            decision_sample: onset + 2_048,
            value: 0.5,
        },
        band: band(),
        span_end_sample: onset + 14_400,
        measure: Some(AttackBandMeasure {
            band: band(),
            sample_rate: 48_000,
            channels: 2,
            event_sample: onset,
            span_end_sample: onset + 14_400,
            span_end: BandSpanEnd::Window,
            peak_frames: 480.0,
            level_dbfs: level,
            sound: BandSound::Rises {
                arrival: BandArrival::At {
                    arrival_frames: 48.0,
                    attack_frames: 240.0,
                },
                release: BandRelease::At(4_800.0),
            },
            envelope: BandEnvelope {
                head: [centi; 96],
                tail: [centi; 64],
            },
        }),
    }
}

struct Fixture {
    details: Vec<(AttackBandDetail, AttackBandDetail)>,
    hits: Vec<KirinAttackBandHit>,
}

impl Fixture {
    fn sources(&self) -> Vec<Source<'_>> {
        self.hits
            .iter()
            .zip(&self.details)
            .map(|(hit, (pre, post))| Source {
                hit: *hit,
                pre: Some(pre),
                post: Some(post),
            })
            .collect()
    }
}

fn matched(onset: i64, pre: KirinAttackBandSide, post: KirinAttackBandSide) -> KirinAttackBandHit {
    KirinAttackBandHit {
        event_sample: onset,
        measured_at_sample: onset,
        kind: 0,
        reserved: [0; 7],
        pre,
        post,
    }
}

/// Twelve hits, oldest first: nine kicks whose band POST delays about 2.4 ms, lengthens 35 ms and
/// lowers 0.8 dB, two hats whose band is silent or only rings on, and a last hit still being
/// measured.
fn kick_and_hats() -> Fixture {
    let delays = [2.1, 2.6, 2.3, 2.5, 2.2, 2.7, 2.4, 2.4, 9.9];
    let mut hits = Vec::new();
    let mut details = Vec::new();
    let mut kick = 0;
    for index in 0..12_i64 {
        let onset = 10_000 + index * 12_000;
        let hit = match index {
            3 => matched(
                onset,
                state(KIRIN_ATTACK_BAND_SIDE_SILENT),
                state(KIRIN_ATTACK_BAND_SIDE_SILENT),
            ),
            6 => matched(
                onset,
                state(KIRIN_ATTACK_BAND_SIDE_RINGS_ON),
                side(1.0, 7.0, 140.0, -7.0),
            ),
            11 => matched(
                onset,
                side(1.0, 7.0, 140.0, -7.0),
                state(KIRIN_ATTACK_BAND_SIDE_PENDING),
            ),
            _ => {
                let delay = delays[kick];
                kick += 1;
                matched(
                    onset,
                    side(1.0, 7.2, 138.0, -7.0),
                    side(1.0 + delay, 7.8, 173.0 + (kick as f32 - 5.5), -7.8),
                )
            }
        };
        hits.push(hit);
        details.push((
            detail(onset, -7.0),
            detail(onset, -7.8 - 0.1 * index as f32),
        ));
    }
    Fixture { details, hits }
}

#[test]
fn the_newest_hits_whose_band_rises_are_summed_and_the_others_counted() {
    let fixture = kick_and_hats();
    let sources = fixture.sources();
    let mut summary = KirinAttackBandSummary::default();
    summarise(&mut summary, &sources, true, 16.0);
    // Of the twelve: the one still measured is neither, the silent and the rings-on hats are
    // left out, and the newest eight kicks are summed (the oldest kick falls outside the eight).
    assert_eq!(summary.count, 8);
    assert_eq!(summary.left_out, 2);
    assert_eq!(summary.delta, 1);
    let keys = summary.event_samples;
    assert_eq!(keys[0], 10_000 + 12_000);
    assert_eq!(keys[7], 10_000 + 10 * 12_000);
    assert!(
        keys.windows(2).all(|pair| pair[0] < pair[1]),
        "oldest first"
    );

    let [delay, attack, release, level] = summary.lanes;
    // DELAY: the median of 2.6, 2.3, 2.5, 2.2, 2.7, 2.4, 2.4, 9.9 is 2.45; all eight later.
    assert_eq!(delay.state, KIRIN_ATTACK_BAND_LANE_VALUE);
    assert_eq!((delay.count, delay.agree), (8, 8));
    assert!((delay.median - 2.45).abs() < 1e-4);
    assert!((delay.low - 2.2).abs() < 1e-4 && (delay.high - 9.9).abs() < 1e-4);
    assert!(
        (delay.values[7] - 9.9).abs() < 1e-4,
        "each hit's value, oldest first"
    );
    assert!(
        (delay.within - 0.5).abs() < 1e-6,
        "63 Hz: a 32nd of a period"
    );
    // ATT: 0.6 ms, inside the 16 ms period: no difference the band can tell.
    assert_eq!(attack.state, KIRIN_ATTACK_BAND_LANE_WITHIN);
    assert_eq!(attack.agree, 0);
    assert!((attack.median - 0.6).abs() < 1e-4);
    // REL: 35 ms longer on every hit.
    assert_eq!(release.state, KIRIN_ATTACK_BAND_LANE_VALUE);
    assert_eq!(release.agree, 8);
    assert!((release.median - 35.0).abs() < 1.0);
    assert!(
        summary
            .lanes
            .iter()
            .all(|lane| lane.withheld == KIRIN_ATTACK_BAND_HELD_NONE),
        "every summed hit has every value"
    );
    // LEVEL: 0.8 dB lower on every hit.
    assert_eq!(level.state, KIRIN_ATTACK_BAND_LANE_VALUE);
    assert_eq!(level.agree, 8);
    assert!((level.median + 0.8).abs() < 1e-4);
    // The panes: the medians of the marks and the average envelopes.
    assert!((summary.pre_arrival_ms - 1.0).abs() < 1e-4);
    assert!((summary.post_arrival_ms - 3.45).abs() < 1e-4);
    assert!(summary
        .pre
        .head_dbfs
        .iter()
        .all(|value| (value + 7.0).abs() < 0.02));
    let summed = [1, 2, 4, 5, 7, 8, 9, 10];
    let expected = summed
        .iter()
        .map(|index| -7.8 - 0.1 * *index as f32)
        .sum::<f32>()
        / 8.0;
    assert!((summary.post.tail_dbfs[10] - expected).abs() < 0.02);
    assert!(summary.post_low.head_dbfs[0] < summary.post.head_dbfs[0]);
    assert!(summary.post_high.head_dbfs[0] > summary.post.head_dbfs[0]);
}

#[test]
fn without_pre_the_summary_is_posts_own_values() {
    let fixture = kick_and_hats();
    let sources = fixture.sources();
    let mut summary = KirinAttackBandSummary::default();
    summarise(&mut summary, &sources, false, 16.0);
    // POST alone: the rings-on hat's POST rises, so it is summed; the silent hat is left out.
    assert_eq!((summary.count, summary.left_out, summary.delta), (8, 1, 0));
    let [delay, attack, release, level] = summary.lanes;
    assert_eq!(delay.state, KIRIN_ATTACK_BAND_LANE_NONE, "DELAY needs PRE");
    // A POST ATT of 7.8 ms is shorter than the 16 ms period: stated as that bound.
    assert_eq!(attack.state, KIRIN_ATTACK_BAND_LANE_WITHIN);
    assert_eq!(release.state, KIRIN_ATTACK_BAND_LANE_VALUE);
    assert_eq!(level.state, KIRIN_ATTACK_BAND_LANE_VALUE);
    assert!(level.median < -7.0);
    assert_eq!(level.agree, 0, "agreement is for POST - PRE");
    assert!(summary.pre_arrival_ms.is_nan() && summary.pre.head_dbfs[0].is_nan());
}

#[test]
fn a_lane_without_values_says_why_its_hits_have_none() {
    let mut fixture = kick_and_hats();
    // Every kick's fall is cut by the next hit on one side; three starts are hidden by the ring-out
    // before them, one more tail runs past the window.
    for (index, hit) in fixture.hits.iter_mut().enumerate() {
        if hit.pre.state != KIRIN_ATTACK_BAND_SIDE_RISES
            || hit.post.state != KIRIN_ATTACK_BAND_SIDE_RISES
        {
            continue;
        }
        hit.post.release_state = KIRIN_ATTACK_BAND_RELEASE_NEXT_HIT;
        if index % 4 == 1 {
            hit.pre.arrival_state = KIRIN_ATTACK_BAND_ARRIVAL_RINGING;
        }
    }
    fixture.hits[10].post.release_state = KIRIN_ATTACK_BAND_RELEASE_AT;
    fixture.hits[10].pre.release_state = KIRIN_ATTACK_BAND_RELEASE_AT_LEAST;
    let sources = fixture.sources();
    let mut summary = KirinAttackBandSummary::default();
    summarise(&mut summary, &sources, true, 16.0);
    let [delay, attack, release, level] = summary.lanes;
    assert_eq!(summary.count, 8);
    assert_eq!(
        (release.state, release.count, release.withheld),
        (
            KIRIN_ATTACK_BAND_LANE_NONE,
            0,
            KIRIN_ATTACK_BAND_HELD_NEXT_HIT
        ),
        "cut by the next hit outnumbers a long tail"
    );
    // Kicks 1, 5 and 9 have hidden starts: the other five still give DELAY and ATT.
    assert_eq!(
        (delay.count, delay.withheld),
        (5, KIRIN_ATTACK_BAND_HELD_RINGING)
    );
    assert_eq!(
        (attack.count, attack.withheld),
        (5, KIRIN_ATTACK_BAND_HELD_RINGING)
    );
    assert!(delay.values[0].is_nan() && delay.values[1].is_finite());
    assert_eq!(level.withheld, KIRIN_ATTACK_BAND_HELD_NONE);
    // POST alone: POST's own fall decides.
    summarise(&mut summary, &sources, false, 16.0);
    assert_eq!(summary.lanes[2].withheld, KIRIN_ATTACK_BAND_HELD_NEXT_HIT);
    assert_eq!(
        summary.lanes[0].withheld, KIRIN_ATTACK_BAND_HELD_NONE,
        "DELAY without PRE is the view's to explain"
    );
}

#[test]
fn nothing_summed_leaves_every_lane_empty_and_nothing_left_out() {
    let mut summary = KirinAttackBandSummary::default();
    let pending = [matched(
        10_000,
        state(KIRIN_ATTACK_BAND_SIDE_PENDING),
        state(KIRIN_ATTACK_BAND_SIDE_PENDING),
    )];
    let sources = pending
        .iter()
        .map(|hit| Source {
            hit: *hit,
            pre: None,
            post: None,
        })
        .collect::<Vec<_>>();
    summarise(&mut summary, &sources, true, 16.0);
    assert_eq!((summary.count, summary.left_out), (0, 0));
    assert!(summary
        .lanes
        .iter()
        .all(|lane| lane.state == KIRIN_ATTACK_BAND_LANE_NONE && lane.count == 0));
    assert!(summary.post.head_dbfs[0].is_nan());
}

#[test]
fn the_summary_is_polled_by_post_only() {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    let mut summary = KirinAttackBandSummary::default();
    assert!(!unsafe { kirin_hypha_poll_attack_band_summary(std::ptr::null_mut(), &mut summary) });
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Pre);
    assert!(engine.poll_attack_band_summary().is_none());
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    let handle = Box::into_raw(Box::new(engine));
    assert!(unsafe { kirin_hypha_poll_attack_band_summary(handle, &mut summary) });
    assert_eq!(
        (summary.band, summary.count),
        (0, 0),
        "no band chosen: nothing summed"
    );
    drop(unsafe { Box::from_raw(handle) });
}
