use super::*;

#[test]
fn stop_tail_reports_confirmed_scope_without_inventing_true_peak_and_resume_completes_it() {
    let mut session =
        MeterSession::new(48_000, crate::channel_layout::ChannelLayout::stereo()).unwrap();
    let mut record =
        MeasureEngine::new(48_000, crate::channel_layout::ChannelLayout::stereo()).unwrap();
    let prefix: Vec<_> = (0..48_000)
        .flat_map(|i| {
            let value = 0.01 * (std::f64::consts::TAU * 997.0 * i as f64 / 48_000.0).sin();
            [value, value]
        })
        .collect();
    session.push_active(&prefix);
    record.push(&prefix);
    let before = session.snapshot_v2();
    assert_eq!(before.processed_frames, 48_000);
    assert_eq!(before.pending_frames, 0);
    let tail = vec![0.8; 479 * 2];
    session.push_active(&tail);
    record.push(&tail);
    let active = session.snapshot_v2();
    assert_eq!(
        active.summary_status,
        MeterSessionSummaryStatus::PendingTail
    );
    assert_eq!(active.processed_frames, 48_000);
    assert_eq!(active.pending_frames, 479);
    assert_eq!(
        active.session.summary.max_true_peak,
        before.session.summary.max_true_peak
    );
    assert_eq!(
        record.finalize().max_true_peak,
        before.session.summary.max_true_peak
    );
    session.pause(); // Stop and bypass use the same non-RT Session pause path.
    let stopped = session.snapshot_v2();
    assert_eq!(stopped.session.state, MeterSessionState::Paused);
    assert_eq!(
        stopped.summary_status,
        MeterSessionSummaryStatus::PendingTail
    );
    assert_eq!(stopped.pending_frames, 479);
    assert_eq!(stopped.session.generation, before.session.generation);
    session.push_active(&[0.8; 2]);
    record.push(&[0.8; 2]);
    let resumed = session.snapshot_v2();
    assert_eq!(resumed.summary_status, MeterSessionSummaryStatus::Complete);
    assert_eq!(resumed.processed_frames, 48_480);
    assert_eq!(resumed.pending_frames, 0);
    assert!(
        resumed.session.summary.max_true_peak.unwrap()
            > before.session.summary.max_true_peak.unwrap() + 30.0
    );
    assert_eq!(
        resumed.session.summary.max_true_peak,
        record.finalize().max_true_peak
    );
    assert_eq!(
        resumed.session.current.true_peak,
        before.session.current.true_peak
    );
    assert_eq!(resumed.session.max_lufs_m, before.session.max_lufs_m);
    session.reset();
    let reset = session.snapshot_v2();
    assert_eq!(reset.summary_status, MeterSessionSummaryStatus::Empty);
    assert_eq!((reset.processed_frames, reset.pending_frames), (0, 0));
    assert_ne!(reset.session.generation, before.session.generation);
}

#[test]
fn partial_input_provenance_is_frame_exact_at_round_and_nonround_rates() {
    for rate in [44_117, 48_000] {
        let mut session =
            MeterSession::new(rate, crate::channel_layout::ChannelLayout::stereo()).unwrap();
        for frames in [1, 31, 479, 2, 433, 441, 123] {
            assert!(session.push_active(&vec![0.1; frames * 2]));
            let snapshot = session.snapshot_v2();
            assert_eq!(
                snapshot.processed_frames + snapshot.pending_frames,
                snapshot.session.active_frames
            );
            assert!(snapshot.pending_frames < u64::from(((rate + 5) / 10).div_ceil(10)));
        }
        let before = session.snapshot_v2();
        assert!(!session.push_active(&[f64::NAN; 2]));
        let after = session.snapshot_v2();
        assert_eq!(
            (after.processed_frames, after.pending_frames),
            (before.processed_frames, before.pending_frames)
        );
    }
}

#[test]
fn repeated_reset_rebases_session_coverage_without_rewinding_engine_clock() {
    let mut session =
        MeterSession::new(48_000, crate::channel_layout::ChannelLayout::stereo()).unwrap();
    session.push_active(&vec![0.1; 48_000 * 2]);
    let logical_before = session.engine.session_processed_frames();
    let generation = session.snapshot().generation;
    session.reset();
    assert_eq!(session.engine.session_processed_frames(), logical_before);
    assert_eq!(
        (
            session.snapshot_v2().processed_frames,
            session.snapshot_v2().pending_frames
        ),
        (0, 0)
    );
    session.push_active(&vec![0.2; 491 * 2]);
    let partial = session.snapshot_v2();
    assert_eq!(
        (
            partial.processed_frames,
            partial.pending_frames,
            partial.session.active_frames
        ),
        (480, 11, 491)
    );
    assert_eq!(partial.session.observed_frames, 0);
    session.push_active(&vec![0.2; (4_800 - 491) * 2]);
    let complete = session.snapshot_v2();
    assert_eq!(
        (
            complete.processed_frames,
            complete.pending_frames,
            complete.session.active_frames
        ),
        (4_800, 0, 4_800)
    );
    assert_eq!(complete.summary_status, MeterSessionSummaryStatus::Complete);
    assert_eq!(
        session.engine.session_processed_frames(),
        logical_before + 4_800
    );
    assert!(complete.session.current.lufs_m.is_some());
    assert_eq!(session.time_raw_tail(1).len(), 1);
    session.reset();
    let logical_second = session.engine.session_processed_frames();
    session.push_active(&vec![0.1; 31 * 2]);
    assert_eq!(session.snapshot_v2().pending_frames, 31);
    session.reset(); // Pending input is discarded; it does not enter the processed origin.
    assert_eq!(session.engine.session_processed_frames(), logical_second);
    session.push_active(&vec![0.1; 480 * 2]);
    let latest = session.snapshot_v2();
    assert_eq!(
        (
            latest.processed_frames,
            latest.pending_frames,
            latest.session.active_frames
        ),
        (480, 0, 480)
    );
    assert_eq!(latest.session.generation, generation + 3);
}
