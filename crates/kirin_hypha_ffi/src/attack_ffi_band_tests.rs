use std::mem::{offset_of, size_of};
use std::sync::Arc;
use std::thread;
use std::time::{Duration, Instant};

use kirin_measure::attack_perception::band::{
    AttackBandMeasure, BandArrival, BandEnvelope, BandRelease, BandSound, BandSpanEnd,
};
use kirin_measure::attack_runtime::{AttackBandDetail, AttackBandResults, AttackPreBand};
use kirin_measure::{
    AttackEvent, AttackPairEvent, AttackPairEventKind, CaptureClockSource, PluginDataRole,
    SpectrumViewStatus,
};

use super::map::sources;
use super::*;

#[path = "attack_ffi_band_identity_tests.rs"]
mod identity_tests;

#[test]
fn band_c_layout_is_fixed() {
    assert_eq!(size_of::<KirinAttackBandSide>(), 24);
    assert_eq!(offset_of!(KirinAttackBandSide, peak_ms), 4);
    assert_eq!(offset_of!(KirinAttackBandSide, level_dbfs), 20);
    assert_eq!(size_of::<KirinAttackBandHit>(), 72);
    assert_eq!(offset_of!(KirinAttackBandHit, measured_at_sample), 8);
    assert_eq!(offset_of!(KirinAttackBandHit, kind), 16);
    assert_eq!(offset_of!(KirinAttackBandHit, pre), 24);
    assert_eq!(offset_of!(KirinAttackBandHit, post), 48);
    assert_eq!(offset_of!(KirinAttackBandBatch, resolution_micros), 12);
    assert_eq!(offset_of!(KirinAttackBandBatch, generation), 16);
    assert_eq!(offset_of!(KirinAttackBandBatch, sample_rate), 24);
    assert_eq!(offset_of!(KirinAttackBandBatch, hits), 32);
    assert_eq!(size_of::<KirinAttackBandBatch>(), 32 + 240 * 72);
    assert_eq!(size_of::<KirinAttackBandEnvelope>(), 640);
    assert_eq!(offset_of!(KirinAttackBandHitEnvelope, band), 72);
    assert_eq!(offset_of!(KirinAttackBandHitEnvelope, pre), 80);
    assert_eq!(offset_of!(KirinAttackBandHitEnvelope, post), 720);
    assert_eq!(size_of::<KirinAttackBandHitEnvelope>(), 1_360);
    assert_eq!(
        KIRIN_ATTACK_BAND_BATCH_CAPACITY,
        KIRIN_ATTACK_PAIR_EVENT_BATCH_CAPACITY
    );
}

#[test]
fn only_post_chooses_a_band_and_only_one_of_the_eight() {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    assert!(!engine.set_attack_band(3));
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Pre);
    assert!(!engine.set_attack_band(3));
    assert!(engine.poll_attack_band().is_none());
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(!engine.set_attack_band(9));
    assert_eq!(engine.attack_band(), 0);
    assert!(engine.set_attack_band(3));
    assert_eq!(engine.attack_band(), 3);
    assert_eq!(
        engine.spectrum.post_attack_band().map(|band| band.index()),
        Some(3)
    );
    assert!(engine.set_attack_band(0));
    assert_eq!(engine.attack_band(), 0);
    assert_eq!(engine.spectrum.post_attack_band(), None);
    let unsupported = KirinHyphaEngine::new(
        12_345,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    *unsupported.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(!unsupported.set_attack_band(3));
}

fn band3() -> AttackBand {
    AttackBand::from_index(3).unwrap()
}

fn event(generation: u64, onset: i64) -> AttackEvent {
    AttackEvent {
        generation,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: [7; 32],
        event_sample: onset,
        decision_sample: onset + 2_048,
        value: 0.5,
    }
}

fn rises(generation: u64, onset: i64, level_dbfs: f32) -> AttackBandDetail {
    AttackBandDetail {
        event: event(generation, onset),
        band: band3(),
        span_end_sample: onset + 14_400,
        measure: Some(AttackBandMeasure {
            band: band3(),
            sample_rate: 48_000,
            channels: 2,
            event_sample: onset,
            span_end_sample: onset + 14_400,
            span_end: BandSpanEnd::Window,
            peak_frames: 480.0,
            level_dbfs,
            sound: BandSound::Rises {
                arrival: BandArrival::At {
                    arrival_frames: 48.0,
                    attack_frames: 240.0,
                },
                release: BandRelease::AtLeast(13_920.0),
            },
            envelope: BandEnvelope::default(),
        }),
    }
}

fn pair(
    kind: AttackPairEventKind,
    common: i64,
    pre: Option<i64>,
    post: Option<i64>,
) -> AttackPairEvent {
    AttackPairEvent {
        pair_generation: 1,
        pre_generation: 5,
        post_generation: 7,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: [7; 32],
        event_sample: common,
        decision_sample: common + 2_048,
        kind,
        pre_event_sample: pre,
        post_event_sample: post,
        pre_value: pre.map(|_| 0.5),
        post_value: post.map(|_| 0.4),
        delta_value: pre.zip(post).map(|_| -0.1),
    }
}

/// Four pairs whose PRE, POST and common onsets all differ, as a real chain gives them.
fn shifted_view(pre_band: AttackPreBand) -> (AttackPairViewSnapshot, AttackBandResults) {
    let mut pre_results = AttackBandResults::new(Some(band3()), 5);
    assert!(pre_results.put_own(rises(5, 10_000, -12.0)));
    assert!(pre_results.put_own(rises(5, 30_000, -14.0)));
    let mut post_results = AttackBandResults::new(Some(band3()), 7);
    // POST at the PRE onsets, and POST's own hits at its own onsets.
    assert!(post_results.put_anchored(rises(7, 10_000, -18.0)));
    assert!(post_results.put_own(rises(7, 10_300, -18.5)));
    assert!(post_results.put_own(rises(7, 50_450, -20.0)));
    let view = AttackPairViewSnapshot {
        status: SpectrumViewStatus::Active,
        pair_events: vec![
            pair(
                AttackPairEventKind::Matched,
                10_150,
                Some(10_000),
                Some(10_300),
            ),
            pair(AttackPairEventKind::PreOnly, 30_100, Some(30_000), None),
            pair(AttackPairEventKind::PostOnly, 50_400, None, Some(50_450)),
            pair(AttackPairEventKind::Ambiguous, 70_000, None, None),
        ],
        band: Some(band3()),
        pre_band,
        pre_band_results: Some(Arc::new(pre_results)),
        ..Default::default()
    };
    (view, post_results)
}

#[test]
fn the_band_hits_are_the_lanes_hits_with_the_lanes_keys_and_no_duplicate() {
    let (view, post_results) = shifted_view(AttackPreBand::Same);
    let hits = sources(&view, None, &post_results, band3());
    assert_eq!(hits.len(), view.pair_events.len(), "one hit per pair event");
    for (source, pair) in hits.iter().zip(&view.pair_events) {
        assert_eq!(source.hit.event_sample, pair.event_sample, "the lanes' key");
    }
    let [matched, pre_only, post_only, ambiguous] = [0, 1, 2, 3].map(|index| hits[index].hit);
    // Matched: PRE's own measure and POST measured at the PRE onset, never POST's own hit.
    assert_eq!(matched.kind, 0);
    assert_eq!(matched.measured_at_sample, 10_000);
    assert_eq!(matched.pre.state, KIRIN_ATTACK_BAND_SIDE_RISES);
    assert_eq!(matched.post.state, KIRIN_ATTACK_BAND_SIDE_RISES);
    assert_eq!(
        (matched.pre.level_dbfs, matched.post.level_dbfs),
        (-12.0, -18.0)
    );
    assert_eq!(
        matched.pre.release_state,
        KIRIN_ATTACK_BAND_RELEASE_AT_LEAST
    );
    assert!((matched.pre.arrival_ms - 1.0).abs() < 1e-4);
    assert_eq!(
        (pre_only.kind, pre_only.post.state),
        (1, KIRIN_ATTACK_BAND_SIDE_ABSENT)
    );
    assert_eq!(pre_only.pre.state, KIRIN_ATTACK_BAND_SIDE_RISES);
    // POST only: POST's own hit at its own onset.
    assert_eq!(
        (post_only.kind, post_only.pre.state),
        (2, KIRIN_ATTACK_BAND_SIDE_ABSENT)
    );
    assert_eq!(post_only.measured_at_sample, 50_450);
    assert_eq!(post_only.post.level_dbfs, -20.0);
    assert_eq!(
        (ambiguous.kind, ambiguous.pre.state, ambiguous.post.state),
        (
            3,
            KIRIN_ATTACK_BAND_SIDE_ABSENT,
            KIRIN_ATTACK_BAND_SIDE_ABSENT
        )
    );

    // PRE has not declared the band: its sides wait, and POST's own hit stands for the pair.
    let (waiting, post_results) = shifted_view(AttackPreBand::Waiting);
    let hits = sources(&waiting, None, &post_results, band3());
    assert_eq!(hits.len(), 4);
    assert_eq!(hits[0].hit.pre.state, KIRIN_ATTACK_BAND_SIDE_PENDING);
    assert_eq!(hits[0].hit.measured_at_sample, 10_300);
    assert_eq!(hits[0].hit.post.level_dbfs, -18.5);
    // Results of another band are never shown as this band's.
    let hits = sources(
        &view,
        None,
        &post_results,
        AttackBand::from_index(4).unwrap(),
    );
    assert!(hits
        .iter()
        .all(|source| source.hit.post.state != KIRIN_ATTACK_BAND_SIDE_RISES));
}

/// 1.2 s of stereo audio with a 500 Hz burst 400 ms in, in 57 blocks of 1 024 frames so the
/// 128-slot ingress ring takes them all while the worker warms up.
fn feed_burst(engine: &KirinHyphaEngine) {
    let mut position = 0_i64;
    for block_index in 0..57 {
        let mut block = vec![0.0_f32; 1_024 * 2];
        for (frame, pair) in block.chunks_mut(2).enumerate() {
            let t = (block_index * 1_024 + frame) as f64 / 48_000.0 - 0.4;
            if t >= 0.0 {
                let envelope = if t < 0.006 {
                    t / 0.006
                } else {
                    (-(t - 0.006) / 0.045).exp()
                };
                let value = (0.5 * envelope * (std::f64::consts::TAU * 500.0 * t).sin()) as f32;
                pair[0] = value;
                pair[1] = value;
            }
        }
        engine.note_capture_window(true, position, 1_024, CaptureClockSource::ProjectTimeline);
        assert!(engine.push_samples_transaction(&block, 2));
        position += 1_024;
    }
}

#[test]
fn without_a_pair_the_band_hits_are_posts_details_with_the_envelope_behind_them() {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(engine.set_attack_enabled(true));
    assert!(engine.set_attack_band(4));
    feed_burst(&engine);
    let deadline = Instant::now() + Duration::from_secs(4);
    let (batch, hit) = loop {
        let found = engine.poll_attack_band().and_then(|batch| {
            let hit = batch.hits[..batch.count as usize]
                .iter()
                .find(|hit| hit.post.state == KIRIN_ATTACK_BAND_SIDE_RISES)
                .copied()?;
            Some((batch, hit))
        });
        if let Some(found) = found {
            break found;
        }
        assert!(Instant::now() < deadline, "no band hit within four seconds");
        thread::sleep(Duration::from_millis(5));
    };
    assert_eq!((batch.band, batch.pre_band), (4, KIRIN_ATTACK_BAND_PRE_OFF));
    assert_eq!(batch.resolution_micros, 2_000);
    assert_eq!(batch.sample_rate, 48_000);
    // The same hits, in the same order, as the details the lanes read without a pair.
    let details = engine.poll_attack_details().unwrap();
    assert_eq!(batch.count, details.count);
    for (band_hit, detail) in batch
        .hits
        .iter()
        .zip(&details.details[..details.count as usize])
    {
        assert_eq!(band_hit.event_sample, detail.event_sample);
    }
    assert_eq!(hit.kind, KIRIN_ATTACK_BAND_KIND_POST_ALONE);
    assert_eq!(hit.pre.state, KIRIN_ATTACK_BAND_SIDE_ABSENT);
    assert!(
        (hit.event_sample - 19_200).abs() < 600,
        "{}",
        hit.event_sample
    );
    assert!(
        (hit.post.level_dbfs + 9.0).abs() < 1.5,
        "{}",
        hit.post.level_dbfs
    );
    assert_eq!(hit.post.arrival_state, KIRIN_ATTACK_BAND_ARRIVAL_AT);
    assert_eq!(hit.post.release_state, KIRIN_ATTACK_BAND_RELEASE_AT);
    assert!(hit.post.release_ms > 80.0 && hit.post.release_ms < 130.0);
    assert!(hit.post.attack_ms > 2.0 && hit.post.attack_ms < 8.0);
    // The envelope poll gives the very record the batch has, with its envelope.
    let envelope = engine.poll_attack_band_envelope(hit.event_sample).unwrap();
    assert_eq!((envelope.hit, envelope.band), (hit, 4));
    let head_peak = envelope
        .post
        .head_dbfs
        .iter()
        .fold(f32::MIN, |peak, value| peak.max(*value));
    assert!((head_peak - hit.post.level_dbfs).abs() < 1.0);
    assert!(engine
        .poll_attack_band_envelope(hit.event_sample + 1)
        .is_none());
    // ALL: the batch empties and stays valid.
    assert!(engine.set_attack_band(0));
    assert_eq!(engine.attack_band(), 0);
    assert_eq!(engine.spectrum.post_attack_band(), None);
    // The public read closure holds the view's publication lock. A contended poll means
    // "keep the previous display", not a malformed empty batch or a missing measurement.
    loop {
        if engine
            .spectrum
            .with_attack_view(|_| {
                assert!(engine.poll_attack_band().is_none());
            })
            .is_some()
        {
            break;
        }
        assert!(
            Instant::now() < deadline,
            "no view read within four seconds"
        );
        thread::sleep(Duration::from_millis(5));
    }
    let cleared = loop {
        if let Some(batch) = engine.poll_attack_band() {
            break batch; // The first available batch must be correct; do not skip wrong data.
        }
        assert!(
            Instant::now() < deadline,
            "no ALL batch within four seconds"
        );
        thread::sleep(Duration::from_millis(5));
    };
    assert_eq!((cleared.band, cleared.count), (0, 0));
}

#[test]
fn band_polls_answer_through_the_c_entry_points() {
    let engine = Box::new(KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    ));
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    let handle = Box::into_raw(engine);
    let mut out = KirinAttackBandBatch::default();
    let mut envelope = KirinAttackBandHitEnvelope::default();
    assert!(!unsafe { kirin_hypha_set_attack_band(std::ptr::null_mut(), 1) });
    assert!(unsafe { kirin_hypha_set_attack_band(handle, 2) });
    assert!(!unsafe { kirin_hypha_poll_attack_band(handle, std::ptr::null_mut()) });
    assert!(unsafe { kirin_hypha_poll_attack_band(handle, &mut out) });
    assert_eq!((out.band, out.count), (2, 0));
    assert_eq!(out.capacity, KIRIN_ATTACK_BAND_BATCH_CAPACITY as u32);
    assert!(!unsafe { kirin_hypha_poll_attack_band_envelope(handle, 0, std::ptr::null_mut()) });
    assert!(!unsafe { kirin_hypha_poll_attack_band_envelope(handle, 0, &mut envelope) });
    drop(unsafe { Box::from_raw(handle) });
}
