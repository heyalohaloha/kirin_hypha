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
    assert_eq!(offset_of!(KirinAttackDetail, transient_db), 84);
    assert_eq!(offset_of!(KirinAttackDetail, body_end_sample), 104);
    assert_eq!(offset_of!(KirinAttackDetail, sharpness_acum), 112);
    assert_eq!(offset_of!(KirinAttackDetail, bin_frames), 116);
    assert_eq!(offset_of!(KirinAttackDetail, shape), 128);
    assert_eq!(size_of::<KirinAttackDetailBatch>(), 122_888);
    assert_eq!(size_of::<KirinAttackPairEvent>(), 112);
    assert_eq!(size_of::<KirinAttackPairEventBatch>(), 26_896);
    assert_eq!(size_of::<KirinAttackStats>(), 32);
}

#[test]
fn default_is_off_and_only_post_can_enable_attack() {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    assert_eq!(
        engine.attack_stats(),
        KirinAttackStats {
            available: 1,
            channels: 2,
            ..KirinAttackStats::default()
        }
    );
    assert!(!engine.set_attack_enabled(true));
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Pre);
    assert!(!engine.set_attack_enabled(true));
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(engine.set_attack_enabled(true));
    assert_eq!(engine.attack_stats().enabled, 1);
    assert!(engine.set_attack_enabled(false));
    assert_eq!(engine.attack_stats().enabled, 0);
}

#[test]
fn selecting_another_analysis_view_stops_the_attack_worker_without_closing_analysis() {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(engine.set_attack_enabled(true));
    assert_eq!(engine.attack_stats().enabled, 1);
    assert!(engine.set_spectrum_visible(true));
    assert_eq!(engine.attack_stats().enabled, 0);
    assert!(engine.spectrum.post_visible());
}

#[test]
fn unsupported_host_rate_stays_unavailable_without_failing_engine() {
    let engine = KirinHyphaEngine::new(
        12_345,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert_eq!(engine.attack_stats().available, 0);
    assert!(!engine.set_attack_enabled(true));
    assert!(engine.poll_attack_batch().is_none());
}

fn feed_shipping_audio(engine: &KirinHyphaEngine, with_presentation: bool) {
    // A detail waits for its 130 ms windows and every onset before the body end: 256 ms of audio.
    let mut position = 0_i64;
    for block_index in 0..48 {
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

fn wait_for_event(
    engine: &KirinHyphaEngine,
) -> (
    KirinAttackBatch,
    KirinAttackEventBatch,
    KirinAttackWaveformBatch,
    KirinAttackDetailBatch,
) {
    let deadline = Instant::now() + Duration::from_secs(2);
    while Instant::now() < deadline {
        // Polls deliberately use try_read. Consume the successful observations instead of
        // discarding them and unwrapping another poll while the producer owns the lock.
        if let (Some(raw), Some(events), Some(waveform), Some(details)) = (
            engine.poll_attack_batch(),
            engine.poll_attack_events(),
            engine.poll_attack_waveform(),
            engine.poll_attack_details(),
        ) {
            if raw.count > 0 && events.count > 0 && waveform.count > 0 && details.count > 0 {
                return (raw, events, waveform, details);
            }
        }
        thread::sleep(Duration::from_millis(5));
    }
    panic!("ATTACK observations did not become available within two seconds");
}

#[test]
fn shipping_vst_clock_and_audio_transaction_reaches_attack_worker() {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(engine.set_attack_enabled(true));
    feed_shipping_audio(&engine, true);
    let (batch, events, waveform, details) = wait_for_event(&engine);
    assert!(batch.count > 0);
    assert!(batch.count as usize <= KIRIN_ATTACK_BATCH_CAPACITY);
    let frames = &batch.frames[..batch.count as usize];
    assert!(frames.iter().all(|frame| frame.sample_rate == 48_000));
    assert!(frames.iter().all(|frame| frame.channels == 2));
    assert!(frames.iter().all(|frame| frame.window_samples == 2_048));
    assert!(frames.iter().all(|frame| frame.hop_samples == 256));
    assert!(frames.iter().any(|frame| frame.value > 0.0));
    assert!(events.count > 0);
    assert!(events.events[..events.count as usize]
        .iter()
        .all(|event| event.decision_sample > event.event_sample));
    assert!(waveform.count > 0);
    assert!(waveform.points[..waveform.count as usize]
        .windows(2)
        .all(|pair| pair[0].end_sample == pair[1].start_sample));
    assert!(details.count > 0);
    let detail = details.details[details.count as usize - 1];
    assert_eq!(detail.shape_count as usize, KIRIN_ATTACK_SHAPE_CAPACITY);
    assert!(detail.shape.iter().any(|value| *value > 0.0));
}

#[test]
fn studio_project_clock_without_optional_presentation_callback_reaches_attack_worker() {
    let engine = KirinHyphaEngine::new(
        48_000,
        kirin_measure::channel_layout::ChannelLayout::stereo(),
    );
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(engine.set_attack_enabled(true));
    feed_shipping_audio(&engine, false);
    let (frames, events, _, _) = wait_for_event(&engine);
    assert!(frames.count > 0);
    assert!(frames.frames[..frames.count as usize]
        .iter()
        .any(|frame| frame.value > 0.0));
    assert!(events.count > 0);
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
        assert!(!kirin_hypha_set_attack_enabled(std::ptr::null_mut(), true));
        assert!(!kirin_hypha_attack_stats(std::ptr::null_mut(), &mut stats));
        assert!(!kirin_hypha_poll_attack_batch(
            std::ptr::null_mut(),
            &mut batch
        ));
        assert!(!kirin_hypha_poll_attack_events(
            std::ptr::null_mut(),
            &mut events
        ));
        assert!(!kirin_hypha_poll_attack_waveform(
            std::ptr::null_mut(),
            &mut waveform
        ));
        assert!(!kirin_hypha_poll_attack_details(
            std::ptr::null_mut(),
            &mut details
        ));
        assert!(!kirin_hypha_poll_attack_pair_events(
            std::ptr::null_mut(),
            &mut pair_events
        ));
    }
}

fn measured_detail(onset: i64, attack_rms_dbfs: f32) -> kirin_measure::AttackDetailedEvent {
    let start = onset.div_euclid(48) * 48;
    kirin_measure::AttackDetailedEvent {
        event: kirin_measure::AttackEvent {
            generation: 3,
            sample_rate: 48_000,
            channels: 2,
            definition_hash: [7; 32],
            event_sample: onset,
            decision_sample: onset + 1_600,
            value: 0.4,
        },
        features: kirin_measure::AttackPerceptualFeatures {
            sample_rate: 48_000,
            channels: 2,
            bin_frames: 48,
            window_start_sample: start,
            attack_rms_dbfs,
            sample_peak_dbfs: attack_rms_dbfs + 10.0,
            crest_db: 10.0,
            body_end_sample: start + 60 * 48,
            body_rms_dbfs: Some(attack_rms_dbfs - 8.0),
            transient_db: Some(8.0),
            sharpness_acum: None,
        },
        shape: kirin_measure::AttackEventShape {
            start_sample: start - 20 * 48,
            end_sample: start + 130 * 48,
            event_sample: onset,
            points: [0.1; ATTACK_SHAPE_POINT_CAPACITY],
        },
    }
}

fn pair(
    kind: kirin_measure::AttackPairEventKind,
    pre: Option<i64>,
    post: Option<i64>,
) -> kirin_measure::AttackPairEvent {
    kirin_measure::AttackPairEvent {
        pair_generation: 1,
        pre_generation: 2,
        post_generation: 3,
        sample_rate: 48_000,
        channels: 2,
        definition_hash: [7; 32],
        event_sample: pre.or(post).unwrap_or(0),
        decision_sample: 22_000,
        kind,
        pre_event_sample: pre,
        post_event_sample: post,
        pre_value: pre.map(|_| 0.3),
        post_value: post.map(|_| 0.4),
        delta_value: pre.zip(post).map(|_| 0.4 - 0.3),
    }
}

fn active_view(
    pair_events: Vec<kirin_measure::AttackPairEvent>,
    post_anchored: Vec<kirin_measure::AttackDetailedEvent>,
) -> kirin_measure::AttackPairViewSnapshot {
    kirin_measure::AttackPairViewSnapshot {
        status: kirin_measure::SpectrumViewStatus::Active,
        pair_events,
        post_anchored,
        ..Default::default()
    }
}

#[test]
fn paired_post_details_are_measured_at_the_pre_onset() {
    use kirin_measure::AttackPairEventKind::{Matched, PostOnly};
    // The POST detector found its own onsets at 10_000, 20_200 and 30_000; POST was also
    // measured at PRE 10_000 and 20_150. 30_000 is POST only.
    let own = [
        measured_detail(10_000, -20.0),
        measured_detail(20_200, -18.0),
        measured_detail(30_000, -16.0),
    ];
    let view = active_view(
        vec![
            pair(Matched, Some(10_000), Some(10_000)),
            pair(Matched, Some(20_150), Some(20_200)),
            pair(PostOnly, None, Some(30_000)),
        ],
        vec![
            measured_detail(10_000, -21.0),
            measured_detail(20_150, -17.0),
        ],
    );
    let batch = to_c_paired_post_detail_batch(&own, &view);
    let samples = batch.details[..batch.count as usize]
        .iter()
        .map(|detail| (detail.event_sample, detail.attack_rms_dbfs))
        .collect::<Vec<_>>();
    assert_eq!(
        samples,
        [(10_000, -21.0), (20_150, -17.0), (30_000, -16.0)],
        "the own detail at the matched POST onset 20_200 is replaced"
    );
    let detail = batch.details[1];
    assert_eq!(
        (detail.transient_available, detail.sharpness_available),
        (1, 0)
    );
    assert_eq!((detail.transient_db, detail.bin_frames), (8.0, 48));
    assert_eq!(detail.body_end_sample, 20_112 + 60 * 48);

    let pairs = to_c_attack_pair_event_batch(view);
    assert_eq!(pairs.events[1].post_event_sample, 20_150);
    assert_eq!(pairs.events[2].post_event_sample, 30_000);
}

#[test]
fn a_full_window_of_matched_hits_keeps_every_anchored_detail() {
    use kirin_measure::AttackPairEventKind::Matched;
    // 200 matched hits whose POST onsets are 40 samples after PRE: 400 details before the
    // replacement, 200 after it, so none of the anchored details is cut.
    let pre = (0..200).map(|hit| 10_000 + hit * 1_440).collect::<Vec<_>>();
    let own = pre
        .iter()
        .map(|onset| measured_detail(onset + 40, -18.0))
        .collect::<Vec<_>>();
    let view = active_view(
        pre.iter()
            .map(|onset| pair(Matched, Some(*onset), Some(onset + 40)))
            .collect(),
        pre.iter()
            .map(|onset| measured_detail(*onset, -17.0))
            .collect(),
    );
    let batch = to_c_paired_post_detail_batch(&own, &view);
    assert_eq!(batch.count, 200);
    assert!(batch.details[..200]
        .iter()
        .zip(&pre)
        .all(|(detail, onset)| detail.event_sample == *onset && detail.attack_rms_dbfs == -17.0));
}
