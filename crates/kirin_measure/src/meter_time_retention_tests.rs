use super::*;
use crate::meter_session::time_observation::TimeSourceSpan;

fn publication() -> TimePublication {
    TimePublication {
        span: TimeSourceSpan {
            epoch: 1,
            incarnation: 2,
            generation: 3,
            token: 4,
            sample_rate: 48_000,
            channels: 2,
        },
        points: vec![],
    }
}

#[test]
fn retention_allocation_keeps_delta_readable_and_reset_rejects_the_old_ticket() {
    let session = Arc::new(Mutex::new(
        MeterSession::new(48_000, crate::channel_layout::ChannelLayout::stereo()).unwrap(),
    ));
    let exchange = MeterDeltaHistoryExchange::new(48_000, session);
    let pre = publication();
    let mut prepared = exchange.prepare_time_history_with(Some(&pre), || {
        assert!(
            exchange.delta.try_lock().is_ok(),
            "the UI mutex must stay available during the actual retention allocation"
        );
        exchange.reset();
        Box::new(MeterHistory::new())
    });
    {
        let mut delta = exchange.delta.lock().unwrap();
        delta.ingest_time(Some(&pre), &[], &mut prepared);
        assert!(delta.time_history.is_none());
    }
    assert!(
        prepared.is_some(),
        "stale storage is released after the mutex"
    );
    let mut current = exchange.prepare_time_history(Some(&pre));
    {
        let mut delta = exchange.delta.lock().unwrap();
        delta.ingest_time(Some(&pre), &[], &mut current);
        assert!(delta.time_history.is_some());
    }
    assert!(current.is_none());
    assert!(exchange
        .prepare_time_history_with(Some(&pre), || panic!(
            "unchanged generation must reuse storage"
        ))
        .is_none());
}

#[test]
fn missing_or_invalid_time_publication_never_allocates_retention() {
    let session = Arc::new(Mutex::new(
        MeterSession::new(48_000, crate::channel_layout::ChannelLayout::stereo()).unwrap(),
    ));
    let exchange = MeterDeltaHistoryExchange::new(48_000, session);
    let mut pre = publication();
    pre.span.token = 0;
    for pre in [None, Some(&pre)] {
        assert!(exchange
            .prepare_time_history_with(pre, || panic!("invalid publication must not allocate"))
            .is_none());
    }
}
