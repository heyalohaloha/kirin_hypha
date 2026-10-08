use super::*;
use crate::{AttackOdfFrame, SpectrumRuntime};

fn history(generation: u64, value: f32) -> AttackHistory {
    let mut history = AttackHistory::with_capacity();
    for index in 0..32_i64 {
        let event_sample = 1_024 + index * 256;
        history.push(AttackOdfFrame {
            generation,
            sample_rate: 48_000,
            channels: 2,
            definition_hash: [9; 32],
            window_samples: 2_048,
            hop_samples: 256,
            support_start_samples: event_sample - 1_024,
            support_end_samples: event_sample + 1_024,
            event_sample,
            value: if index == 12 { value } else { 0.0 },
        });
    }
    history.push_waveform(crate::AttackWaveformPoint {
        generation,
        sample_rate: 48_000,
        channels: 2,
        start_sample: 0,
        end_sample: 480,
        peak_linear: 0.5,
        rms_dbfs: -12.0,
    });
    history
}

#[test]
fn exact_histories_publish_active_pair_without_time_shifting() {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let coordinator = SpectrumCoordinator::new(48_000, runtime);
    let mut session = coordinator.new_post_session();
    session.started_at = Some(Instant::now());
    store_joined_attack(
        &coordinator,
        &mut session,
        Instant::now(),
        Some(history(2, 0.8)),
        Some((history(4, 0.6), None, None)),
    );
    let view = coordinator.try_attack_view().unwrap();
    assert_eq!(view.status, SpectrumViewStatus::Active);
    assert!(view.pre.is_some());
    assert!(view.post.is_some());
    assert!(session.last_presented_end_samples.is_some());
}

#[test]
fn mismatched_content_stays_unavailable_instead_of_correlating() {
    let runtime = SpectrumRuntime::new(48_000, crate::channel_layout::ChannelLayout::stereo());
    let coordinator = SpectrumCoordinator::new(48_000, runtime);
    let mut pre = history(2, 0.6);
    let mut post = history(4, 0.8);
    let shifted = post.frames().copied().collect::<Vec<_>>();
    post = AttackHistory::with_capacity();
    for mut frame in shifted {
        frame.support_start_samples += 1;
        frame.support_end_samples += 1;
        frame.event_sample += 1;
        post.push(frame);
    }
    let mut session = coordinator.new_post_session();
    session.started_at = Some(Instant::now() - WARMUP_LIMIT);
    store_joined_attack(
        &coordinator,
        &mut session,
        Instant::now(),
        Some(post),
        Some((std::mem::take(&mut pre), None, None)),
    );
    assert_eq!(
        coordinator.try_attack_view().unwrap().status,
        SpectrumViewStatus::Unavailable
    );
}

#[test]
fn pre_band_state_tells_waiting_from_a_pre_that_predates_bands() {
    use super::pre_band_state;
    use crate::attack_perception::band::AttackBand;
    use crate::attack_runtime::{AttackBandResults, AttackPreBand};
    use std::time::Duration;

    let band = AttackBand::from_index(3);
    let other = AttackBandResults::new(AttackBand::from_index(5), 1);
    let same = AttackBandResults::new(band, 1);
    let soon = Some(Duration::from_millis(100));
    let late = Some(Duration::from_secs(3));
    assert_eq!(pre_band_state(None, Some(&same), late), AttackPreBand::Off);
    assert_eq!(pre_band_state(band, Some(&same), soon), AttackPreBand::Same);
    // A PRE on another band follows the request, however long it takes.
    assert_eq!(
        pre_band_state(band, Some(&other), late),
        AttackPreBand::Waiting
    );
    // No band section at all: waiting at first, then a PRE that predates bands.
    assert_eq!(pre_band_state(band, None, None), AttackPreBand::Waiting);
    assert_eq!(pre_band_state(band, None, soon), AttackPreBand::Waiting);
    assert_eq!(pre_band_state(band, None, late), AttackPreBand::Predates);
}
