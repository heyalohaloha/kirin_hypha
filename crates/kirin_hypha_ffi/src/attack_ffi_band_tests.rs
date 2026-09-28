use std::mem::{offset_of, size_of};
use std::thread;
use std::time::{Duration, Instant};

use kirin_measure::{CaptureClockSource, PluginDataRole};

use super::*;

#[test]
fn band_c_layout_is_fixed() {
    assert_eq!(size_of::<KirinAttackBandSide>(), 680);
    assert_eq!(offset_of!(KirinAttackBandSide, span_end_sample), 8);
    assert_eq!(offset_of!(KirinAttackBandSide, peak_ms), 16);
    assert_eq!(offset_of!(KirinAttackBandSide, head_dbfs), 40);
    assert_eq!(offset_of!(KirinAttackBandSide, tail_dbfs), 424);
    assert_eq!(size_of::<KirinAttackBandHit>(), 1_392);
    assert_eq!(offset_of!(KirinAttackBandHit, event_sample), 16);
    assert_eq!(offset_of!(KirinAttackBandHit, resolution_micros), 24);
    assert_eq!(offset_of!(KirinAttackBandHit, delay_ms), 28);
    assert_eq!(offset_of!(KirinAttackBandHit, pre), 32);
    assert_eq!(offset_of!(KirinAttackBandHit, post), 712);
    assert_eq!(size_of::<KirinAttackBandBatch>(), 16 + 64 * 1_392);
    assert_eq!(offset_of!(KirinAttackBandBatch, hits), 16);
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
    // A rate ATTACK does not support has no band either.
    let unsupported = KirinHyphaEngine::new(
        12_345,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    *unsupported.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(!unsupported.set_attack_band(3));
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
fn a_chosen_band_reports_the_post_hit_with_its_own_times() {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(engine.set_attack_enabled(true));
    assert!(engine.set_attack_band(4));
    feed_burst(&engine);
    let deadline = Instant::now() + Duration::from_secs(4);
    let batch = loop {
        if let Some(batch) = engine.poll_attack_band().filter(|batch| batch.count > 0) {
            break batch;
        }
        if Instant::now() >= deadline {
            let details = engine.poll_attack_details();
            panic!(
                "no band hit within four seconds: band {}, stats {:?}, details {:?} of which complete {:?}, band details {:?}",
                engine.attack_band(),
                engine.attack_stats(),
                details.map(|batch| batch.count),
                details.map(|batch| batch.details[..batch.count as usize]
                    .iter()
                    .filter(|detail| detail.complete == 1)
                    .count()),
                engine
                    .attack_runtime
                    .as_ref()
                    .and_then(|runtime| runtime.try_history())
                    .map(|history| history.band_details().len())
            );
        }
        thread::sleep(Duration::from_millis(5));
    };
    assert_eq!(batch.band, 4);
    assert_eq!(batch.pre_band_available, 0);
    let hit = batch.hits[0];
    assert_eq!(hit.kind, 2, "no PRE: POST only");
    assert_eq!(hit.band, 4);
    assert_eq!(hit.resolution_micros, 2_000);
    assert_eq!(hit.delay_available, 0);
    assert_eq!(hit.pre.available, 0);
    assert_eq!(hit.post.available, 1);
    // The onset decision sits within one ODF window of the burst's start at 19 200.
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
    assert_eq!(hit.post.arrival_available, 1);
    assert_eq!(hit.post.release_available, 1);
    assert!(
        hit.post.release_ms > 80.0 && hit.post.release_ms < 130.0,
        "{}",
        hit.post.release_ms
    );
    assert!(
        hit.post.attack_ms > 2.0 && hit.post.attack_ms < 8.0,
        "{}",
        hit.post.attack_ms
    );
    // ALL: the batch empties and stays valid.
    assert!(engine.set_attack_band(0));
    let cleared = engine.poll_attack_band().unwrap();
    assert_eq!(cleared.band, 0);
    assert_eq!(cleared.count, 0);
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
    assert!(!unsafe { kirin_hypha_set_attack_band(std::ptr::null_mut(), 1) });
    assert!(unsafe { kirin_hypha_set_attack_band(handle, 2) });
    assert!(!unsafe { kirin_hypha_poll_attack_band(handle, std::ptr::null_mut()) });
    assert!(unsafe { kirin_hypha_poll_attack_band(handle, &mut out) });
    assert_eq!(out.band, 2);
    assert_eq!(out.count, 0);
    assert_eq!(out.capacity, KIRIN_ATTACK_BAND_BATCH_CAPACITY as u32);
    drop(unsafe { Box::from_raw(handle) });
}
