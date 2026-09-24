use super::*;
use crate::channel_layout::ChannelLayout;

fn session() -> MeterSession {
    MeterSession::new_in_epoch(48_000, ChannelLayout::stereo(), 27).unwrap()
}
fn advance(session: &mut MeterSession, start: i64, epoch: u64, samples: usize) {
    let audio: Vec<_> = (0..samples)
        .flat_map(|i| {
            let v = 0.25 * (i as f64 * std::f64::consts::TAU / 48.0).sin();
            [v, v]
        })
        .collect();
    assert!(session.push_active_at(
        &audio,
        crate::MeterClockStart {
            position_samples: Some(start),
            epoch: Some(epoch),
            source: CaptureClockSource::ProjectTimeline,
            ..crate::MeterClockStart::default()
        }
    ));
}

#[test]
fn four_hundred_ms_requires_the_whole_contiguous_aperture() {
    let mut s = session();
    advance(&mut s, 0, 10, 19_200);
    let tail = wire_tail(&s, 48_000);
    assert_eq!(tail.len(), 4);
    assert_eq!(
        tail.iter()
            .map(|p| p.window.unwrap().complete_400ms)
            .collect::<Vec<_>>(),
        [false, false, false, true]
    );
    assert!(tail
        .iter()
        .all(|p| p.window.unwrap().measurement_epoch == 27));
    advance(&mut s, 19_201, 10, 19_200); // one-sample gap, even with unchanged producer epoch
    let tail = wire_tail(&s, 48_000);
    assert_eq!(
        tail[4..]
            .iter()
            .map(|p| p.window.unwrap().complete_400ms)
            .collect::<Vec<_>>(),
        [false, false, false, true]
    );
}

#[test]
fn pause_preserves_statistics_but_does_not_bridge_a_window() {
    let mut s = session();
    advance(&mut s, 0, 10, 19_200);
    let before = s.snapshot();
    s.pause();
    assert_eq!(s.snapshot().max_lufs_m, before.max_lufs_m);
    advance(&mut s, 19_200, 10, 19_200);
    let tail = wire_tail(&s, 48_000);
    assert_ne!(tail[3].run_id, tail[4].run_id);
    assert_eq!(
        tail[4..]
            .iter()
            .map(|p| p.window.unwrap().complete_400ms)
            .collect::<Vec<_>>(),
        [false, false, false, true]
    );
}

#[test]
fn publisher_carries_warmup_context_before_the_bounded_tail() {
    let mut s = session();
    advance(&mut s, 0, 10, 48_000 * 4);
    let tail = wire_tail(&s, 48_000);
    assert_eq!(tail.len(), 32);
    assert!(tail.iter().all(|p| p.window.unwrap().complete_400ms));
    let bytes = serde_json::to_vec(&tail).unwrap().len();
    eprintln!("32 point provenance payload: {bytes} bytes");
    assert!(bytes < MAX_EXCHANGE_BYTES as usize - 1_024);
    s.reset();
    advance(&mut s, 0, 10, 4_800);
    assert!(!wire_tail(&s, 48_000)[0].window.unwrap().complete_400ms);
}

#[test]
fn broken_middle_unknown_clock_and_cross_epoch_are_never_complete() {
    let mut s = session();
    advance(&mut s, 0, 10, 19_200);
    let valid = s.recent_history(MeterHistoryResolution::Hz10, 4);
    for kind in 0..5 {
        let mut broken = valid.clone();
        match kind {
            0 => broken[1].measurement_epoch += 1,
            1 => broken[1].generation += 1,
            2 => broken[1].run_id += 1,
            3 => broken[1].last_observed_frames += 1,
            _ => broken[1].timeline_source = CaptureClockSource::Unknown,
        }
        assert!(!complete_window(&broken, 3, 48_000));
    }
}
