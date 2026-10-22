//! A delayed PCM chain, with independent node clocks and the real producer conversion.
use super::*;
use crate::{
    channel_layout::ChannelLayout, new_record_take_tracker, MeterClockStart, MeterSession,
    PresentationLatencySamples, PresentationLatencySource, RecordTakeTracker,
};

const RATE: u32 = 48_000;
const BLOCK: usize = 512;

fn signal(sample: i64) -> f64 {
    if sample < 0 {
        return 0.0;
    }
    // A changing envelope makes a wrong-time subtraction visible; a steady sine would hide it.
    let level = 0.025 + 0.15 * ((sample / 2053) % 7) as f64 / 6.0;
    level * (std::f64::consts::TAU * 997.0 * sample as f64 / f64::from(RATE)).sin()
}

fn push(
    session: &mut MeterSession,
    tracker: &RecordTakeTracker,
    received: u64,
    raw: i64,
    latency: PresentationLatencySamples,
    audio: &[f64],
) {
    if received == 0 {
        tracker.note_transport_start(raw);
    }
    tracker.note_capture_window_with_clocks_boundary(
        true,
        raw,
        BLOCK as u64,
        CaptureClockSource::ProjectTimeline,
        latency,
        crate::AuxiliaryClockSamples::default(),
        false,
    );
    let point = tracker
        .clock_point_for_captured_frame(received + 1)
        .unwrap();
    assert!(session.push_active_at(
        audio,
        MeterClockStart {
            playback_start: tracker.transport_start_before(received).and_then(|raw| {
                crate::record_take::wav_presentation_position_samples(raw, latency.output)
            }),
            position_samples: Some(point.position_samples - 1),
            epoch: Some(point.epoch),
            source: point.source,
            presentation_latency: point.presentation_latency,
            ..Default::default()
        }
    ));
}

#[test]
fn delayed_pcm_with_reported_latency_compares_the_same_audio_windows() {
    delayed_chain(true, 4096);
}

#[test]
fn shifted_node_clocks_without_latency_proof_do_not_make_delta() {
    for delay in [4096, 1, 4800] {
        delayed_chain(false, delay);
    }
}

#[test]
fn aligned_node_clocks_without_optional_latency_keep_their_delta() {
    delayed_chain(false, 0);
}

fn delayed_chain(reported: bool, delay: usize) {
    for layout in [ChannelLayout::mono(), ChannelLayout::stereo()] {
        let channels = layout.channel_count();
        let pre = Arc::new(Mutex::new(
            MeterSession::new_in_epoch(RATE, layout, 1).unwrap(),
        ));
        let post = Arc::new(Mutex::new(
            MeterSession::new_in_epoch(RATE, layout, 2).unwrap(),
        ));
        let pre_exchange = MeterDeltaHistoryExchange::new(RATE, Arc::clone(&pre));
        let post_exchange = MeterDeltaHistoryExchange::new(RATE, Arc::clone(&post));
        let fixture = tempfile::tempdir().unwrap();
        let instance = fixture.path().join("project/pre");
        std::fs::create_dir_all(&instance).unwrap();
        let pre_json = instance.join("pre.json");
        std::fs::write(&pre_json, br#"{"instance_id":"pre","watch_owner_id":"pre-owner","signal_state":"active","daw_session_id":"fixture"}"#).unwrap();
        let target = MeterHistoryTarget::from_pre_json("pre".into(), &pre_json).unwrap();
        let pre_tracker = new_record_take_tracker();
        let post_tracker = new_record_take_tracker();
        let mut delayed = VecDeque::from(vec![0.0; delay]);
        let latency = |output| {
            if reported {
                PresentationLatencySamples {
                    source: PresentationLatencySource::Vst3,
                    input: Some(0),
                    output: Some(output),
                }
            } else {
                PresentationLatencySamples::default()
            }
        };
        let mut state = TimeComparisonState::default();
        let mut history = MeterHistory::new();
        let mut comparisons = 0;
        for block in 0..420 {
            let received = block * BLOCK;
            let mut pre_audio = Vec::with_capacity(BLOCK * channels);
            let mut post_audio = Vec::with_capacity(BLOCK * channels);
            for sample in received..received + BLOCK {
                let input = signal(sample as i64);
                delayed.push_back(input);
                let output = delayed.pop_front().unwrap();
                pre_audio.extend(std::iter::repeat_n(input, channels));
                post_audio.extend(std::iter::repeat_n(output * 0.5, channels));
            }
            // PRE's downstream delay is part of its raw node clock. POST's position describes
            // the delayed content delivered now. Only the reported conversion may remove it.
            push(
                &mut pre.lock().unwrap(),
                &pre_tracker,
                received as u64,
                received as i64 + delay as i64,
                latency(delay as u32),
                &pre_audio,
            );
            push(
                &mut post.lock().unwrap(),
                &post_tracker,
                received as u64,
                received as i64 - delay as i64,
                latency(0),
                &post_audio,
            );
            let publication = {
                let session = pre.lock().unwrap();
                TimePublication {
                    span: session.time_source_span(),
                    points: session
                        .time_raw_tail(32)
                        .into_iter()
                        .map(|p| p.wire)
                        .collect(),
                }
            };
            state.ingest(
                Some(&publication),
                &post.lock().unwrap().time_raw_tail(64),
                &mut history,
            );
            if block % 16 == 0 {
                pre_exchange
                    .service_pre_endpoint("pre", "fixture", "pre-owner", &instance)
                    .unwrap();
                post_exchange.service_post_endpoint(Some(target.clone()));
                let exchanged = post_exchange.recent(MeterHistoryResolution::Hz10, 64);
                if !reported && delay != 0 {
                    assert!(
                        received < 192000 || exchanged.is_empty(),
                        "legacy history accepted an unproven offset"
                    );
                } else if received >= 192000 {
                    let point = exchanged.last().unwrap();
                    assert!((point.lufs_m.mean.unwrap() - 20.0 * 0.5_f64.log10()).abs() < 0.03);
                }
            }
            if !reported && delay != 0 {
                assert!(
                    state.point.is_none(),
                    "unproven {delay}-sample offset was paired"
                );
                assert!(
                    history
                        .recent(MeterHistoryResolution::Hz10, 64)
                        .iter()
                        .all(|point| point.lufs_m.mean.is_none() && point.true_peak.mean.is_none()),
                    "unproven {delay}-sample offset created finite history"
                );
            } else if received >= 192000 {
                let point = state
                    .point
                    .as_ref()
                    .expect("the qualified chain must compare");
                for metric in [0, 1, 2] {
                    let expected = 20.0 * 0.5_f64.log10();
                    assert!(
                        (point.wire.values[metric].unwrap() - expected).abs() < 0.03,
                        "metric {metric}, delay {delay}: {:?}",
                        point.wire.values
                    );
                }
                assert!(point.wire.values[3].unwrap().abs() < 0.03);
                assert!(point.wire.crest.unwrap().abs() < 0.03);
                comparisons += 1;
            }
        }
        if reported || delay == 0 {
            assert!(comparisons > 10);
        }
        assert!(post.lock().unwrap().snapshot().current.lufs_m.is_some());
        assert!(post.lock().unwrap().snapshot().summary.lufs_i.is_some());
    }
}
