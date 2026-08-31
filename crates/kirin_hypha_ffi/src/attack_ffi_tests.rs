use std::mem::{offset_of, size_of};
use std::thread;
use std::time::{Duration, Instant};

use kirin_measure::{
    CaptureClockSource, PluginDataRole, PresentationLatencySamples, PresentationLatencySource,
};

use super::*;

#[test]
fn attack_c_layout_is_fixed_without_changing_existing_abi() {
    assert_eq!(size_of::<KirinAttackOdfFrame>(), 88);
    assert_eq!(offset_of!(KirinAttackOdfFrame, definition_hash), 16);
    assert_eq!(offset_of!(KirinAttackOdfFrame, support_start_samples), 56);
    assert_eq!(offset_of!(KirinAttackOdfFrame, value), 80);
    assert_eq!(size_of::<KirinAttackBatch>(), 5_640);
    assert_eq!(offset_of!(KirinAttackBatch, frames), 8);
    assert_eq!(size_of::<KirinAttackEvent>(), 72);
    assert_eq!(offset_of!(KirinAttackEvent, event_sample), 48);
    assert_eq!(offset_of!(KirinAttackEvent, value), 64);
    assert_eq!(size_of::<KirinAttackEventBatch>(), 17_288);
    assert_eq!(size_of::<KirinAttackWaveformPoint>(), 40);
    assert_eq!(size_of::<KirinAttackWaveformBatch>(), 24_008);
    assert_eq!(size_of::<KirinAttackDetail>(), 512);
    assert_eq!(offset_of!(KirinAttackDetail, shape), 128);
    assert_eq!(size_of::<KirinAttackDetailBatch>(), 122_888);
    assert_eq!(size_of::<KirinAttackPairEvent>(), 112);
    assert_eq!(size_of::<KirinAttackPairEventBatch>(), 26_896);
    assert_eq!(size_of::<KirinAttackStats>(), 32);
}

#[test]
fn default_is_off_and_only_post_can_enable_internal_attack() {
    let engine = KirinHyphaEngine::new(48_000, 2);
    assert_eq!(
        engine.internal_attack_stats(),
        KirinAttackStats {
            available: 1,
            channels: 2,
            ..KirinAttackStats::default()
        }
    );
    assert!(!engine.set_internal_attack_enabled(true));
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Pre);
    assert!(!engine.set_internal_attack_enabled(true));
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(engine.set_internal_attack_enabled(true));
    assert_eq!(engine.internal_attack_stats().enabled, 1);
    assert!(engine.set_internal_attack_enabled(false));
    assert_eq!(engine.internal_attack_stats().enabled, 0);
}

#[test]
fn selecting_another_analysis_view_stops_the_attack_worker_without_closing_analysis() {
    let engine = KirinHyphaEngine::new(48_000, 2);
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(engine.set_internal_attack_enabled(true));
    assert_eq!(engine.internal_attack_stats().enabled, 1);
    assert!(engine.set_spectrum_visible(true));
    assert_eq!(engine.internal_attack_stats().enabled, 0);
    assert!(engine.spectrum.post_visible());
}

#[test]
fn unsupported_host_rate_stays_unavailable_without_failing_engine() {
    let engine = KirinHyphaEngine::new(12_345, 2);
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert_eq!(engine.internal_attack_stats().available, 0);
    assert!(!engine.set_internal_attack_enabled(true));
    assert!(engine.poll_internal_attack_batch().is_none());
}

fn feed_shipping_audio(engine: &KirinHyphaEngine, with_presentation: bool) {
    let mut position = 0_i64;
    for block_index in 0..24 {
        let mut block = vec![0.0_f32; 256 * 2];
        if block_index == 8 {
            block[0] = 1.0;
            block[1] = 1.0;
        }
        if with_presentation {
            engine.note_capture_window_with_presentation(
                true,
                position,
                256,
                CaptureClockSource::ProjectTimeline,
                PresentationLatencySamples {
                    source: PresentationLatencySource::Vst3,
                    input: Some(0),
                    output: Some(0),
                },
                false,
            );
        } else {
            engine.note_capture_window(true, position, 256, CaptureClockSource::ProjectTimeline);
        }
        assert!(engine.push_samples_transaction(&block, 2));
        position += 256;
    }
}

fn wait_for_event(engine: &KirinHyphaEngine) {
    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline
        && (engine
            .poll_internal_attack_events()
            .is_none_or(|batch| batch.count == 0)
            || engine
                .poll_internal_attack_details()
                .is_none_or(|batch| batch.count == 0))
    {
        thread::sleep(Duration::from_millis(5));
    }
}

#[test]
fn shipping_vst_clock_and_audio_transaction_reaches_attack_worker() {
    let engine = KirinHyphaEngine::new(48_000, 2);
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(engine.set_internal_attack_enabled(true));
    feed_shipping_audio(&engine, true);
    wait_for_event(&engine);

    let batch = engine.poll_internal_attack_batch().unwrap();
    assert!(batch.count > 0);
    assert!(batch.count as usize <= KIRIN_ATTACK_BATCH_CAPACITY);
    let frames = &batch.frames[..batch.count as usize];
    assert!(frames.iter().all(|frame| frame.sample_rate == 48_000));
    assert!(frames.iter().all(|frame| frame.channels == 2));
    assert!(frames.iter().all(|frame| frame.window_samples == 2_048));
    assert!(frames.iter().all(|frame| frame.hop_samples == 256));
    assert!(frames.iter().any(|frame| frame.value > 0.0));
    let events = engine.poll_internal_attack_events().unwrap();
    assert!(events.count > 0);
    assert!(events.events[..events.count as usize]
        .iter()
        .all(|event| event.decision_sample > event.event_sample));
    let waveform = engine.poll_internal_attack_waveform().unwrap();
    assert!(waveform.count > 0);
    assert!(waveform.points[..waveform.count as usize]
        .windows(2)
        .all(|pair| pair[0].end_sample == pair[1].start_sample));
    let details = engine.poll_internal_attack_details().unwrap();
    assert!(details.count > 0);
    let detail = details.details[details.count as usize - 1];
    assert_eq!(detail.shape_count as usize, KIRIN_ATTACK_SHAPE_CAPACITY);
    assert!(detail.shape.iter().any(|value| *value > 0.0));
}

#[test]
fn studio_project_clock_without_optional_presentation_callback_reaches_attack_worker() {
    let engine = KirinHyphaEngine::new(48_000, 2);
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(engine.set_internal_attack_enabled(true));
    feed_shipping_audio(&engine, false);
    wait_for_event(&engine);

    let frames = engine.poll_internal_attack_batch().unwrap();
    assert!(frames.count > 0);
    assert!(frames.frames[..frames.count as usize]
        .iter()
        .any(|frame| frame.value > 0.0));
    assert!(engine.poll_internal_attack_events().unwrap().count > 0);
}

#[test]
fn c_functions_are_null_safe() {
    let mut stats = KirinAttackStats::default();
    let mut batch = KirinAttackBatch::default();
    let mut events = KirinAttackEventBatch::default();
    let mut waveform = KirinAttackWaveformBatch::default();
    let mut details = KirinAttackDetailBatch::default();
    let mut pair_events = KirinAttackPairEventBatch::default();
    unsafe {
        assert!(!kirin_hypha_set_internal_attack_enabled(
            std::ptr::null_mut(),
            true
        ));
        assert!(!kirin_hypha_internal_attack_stats(
            std::ptr::null_mut(),
            &mut stats
        ));
        assert!(!kirin_hypha_poll_internal_attack_batch(
            std::ptr::null_mut(),
            &mut batch
        ));
        assert!(!kirin_hypha_poll_internal_attack_events(
            std::ptr::null_mut(),
            &mut events
        ));
        assert!(!kirin_hypha_poll_internal_attack_waveform(
            std::ptr::null_mut(),
            &mut waveform
        ));
        assert!(!kirin_hypha_poll_internal_attack_details(
            std::ptr::null_mut(),
            &mut details
        ));
        assert!(!kirin_hypha_poll_internal_attack_pair_events(
            std::ptr::null_mut(),
            &mut pair_events
        ));
    }
}
