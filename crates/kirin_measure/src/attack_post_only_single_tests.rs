use super::super::*;
use super::tests::*;
use crate::attack_perception::band::{AttackBand, BandSpanEnd};
use crate::attack_runtime::snapshot::AttackFinish;

#[test]
fn paired_post_only_single_updates_its_end_when_the_next_hit_is_detected() {
    for selected_band in [None, AttackBand::from_index(3)] {
        let runtime = AttackRuntime::new(48000, 2).unwrap();
        runtime.set_band(selected_band);
        assert!(runtime.set_enabled(true));
        feed_onsets(&runtime, 0, 23000, &[20000]);
        let (source, event) = wait(|| {
            let snapshot = runtime.try_observation_snapshot()?;
            let history = runtime.try_history()?;
            let event = history.events().next().copied()?;
            Some((snapshot.source?.source, event))
        });
        let mut selected = request(source, event, selected_band);
        if selected_band.is_none() {
            let end = runtime
                .bins
                .lock()
                .unwrap()
                .body_end_sample(event.event_sample, None);
            selected.requested_end = end;
            selected.measurement_end = end;
        }
        selected.pair_kind = 2;
        selected.proof_token = [1; 32];
        let token = runtime.request_single(selected).unwrap();
        feed_onsets(&runtime, 23000, 33000, &[20000, 24000]);
        let deadline = std::time::Instant::now() + std::time::Duration::from_secs(3);
        let complete = wait(|| {
            assert!(
                std::time::Instant::now() < deadline,
                "band={selected_band:?}, reply={:?}, events={:?}",
                runtime.poll_single(token),
                runtime
                    .try_history()
                    .map(|h| h.events().copied().collect::<Vec<_>>())
            );
            runtime
                .poll_single(token)
                .filter(|reply| reply.finish == AttackFinish::Full)
        });
        let history = runtime.try_history().unwrap();
        let next = history
            .events()
            .find(|hit| hit.event_sample > event.event_sample)
            .unwrap();
        assert!(complete.request.measurement_end <= next.event_sample);
        assert_eq!(complete.request.span_end, BandSpanEnd::NextHit);
        runtime.shutdown_and_join();
    }
}
