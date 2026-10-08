use super::*;

fn assert_same(actual: SessionSummary, canonical: SessionSummary) {
    for (actual, canonical) in [
        (actual.lufs_i, canonical.lufs_i),
        (actual.lra, canonical.lra),
    ] {
        match (actual, canonical) {
            (Some(actual), Some(canonical)) => assert!((actual - canonical).abs() < 1e-10),
            (None, None) => {}
            values => panic!("cached/canonical availability differs: {values:?}"),
        }
    }
    assert_eq!(actual.max_true_peak, canonical.max_true_peak);
    assert_eq!(actual.layout, canonical.layout);
}

fn tone(rate: u32, frames: usize, offset: usize, level: f64) -> Vec<f64> {
    (offset..offset + frames)
        .flat_map(|frame| {
            let phase = frame as f64 * std::f64::consts::TAU / f64::from(rate);
            [
                level * (phase * 997.0).sin(),
                level * (phase * 1103.0).sin(),
            ]
        })
        .collect()
}

#[test]
fn intermediate_record_summary_matches_native_canonical_at_every_drain() {
    let layout = ChannelLayout::by_id(LayoutId::Stereo);
    for rate in [44_100, 48_000, 96_000] {
        let mut engines = RecordMeasureEngines::new(rate, layout).unwrap();
        let mut frame = 0;
        for section in 0..5 {
            let samples = tone(rate, rate as usize, frame, 0.03 * f64::from(section + 1));
            for chunk in samples.chunks(256) {
                engines.summary().push(chunk);
                assert_same(engines.intermediate_summary(), engines.summary().finalize());
            }
            frame += rate as usize;
        }
        let prior = engines.intermediate_summary();
        // Pending <10 ms input cannot invent a processed-prefix TP or advance I/LRA.
        engines
            .summary()
            .push(&vec![0.9; (rate as usize / 100 - 1) * 2]);
        assert_same(engines.intermediate_summary(), engines.summary().finalize());
        // The exact maximum is still read from the same EBU engine, never a stale cached scalar.
        assert_same(engines.intermediate_summary(), prior);
        engines.summary().push(&[0.9, 0.9]);
        assert_same(engines.intermediate_summary(), engines.summary().finalize());
    }
}

#[test]
fn record_summary_reset_drops_previous_values_and_cache_history() {
    for layout in [LayoutId::Mono, LayoutId::Stereo] {
        let layout = ChannelLayout::by_id(layout);
        let mut engines = RecordMeasureEngines::new(48_000, layout).unwrap();
        engines
            .summary()
            .push(&vec![0.2; 192_000 * layout.channel_count()]);
        assert!(engines.intermediate_summary().lufs_i.is_some());
        engines.reset();
        let empty = engines.intermediate_summary();
        assert_same(empty, engines.summary().finalize());
        assert!(empty.lufs_i.is_none());
        assert!(empty.max_true_peak.is_none());
        // The first incomplete analysis chunk after RESET cannot revive the old peak.
        engines
            .summary()
            .push(&vec![0.05; 479 * layout.channel_count()]);
        assert_same(engines.intermediate_summary(), engines.summary().finalize());
        assert!(engines.intermediate_summary().max_true_peak.is_none());
    }
}
