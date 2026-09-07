use super::*;

fn feed(session: &mut MeterSession, amplitude: f64, count: usize) {
    for block in 0..count {
        let input: Vec<_> = (0..4_800)
            .flat_map(|i| {
                let value = amplitude
                    * (std::f64::consts::TAU * 1_000.0 * (block * 4_800 + i) as f64 / 48_000.0)
                        .sin();
                [value, value]
            })
            .collect();
        assert!(session.push_active(&input));
    }
}

#[test]
fn session_maximum_is_producer_owned_survives_pause_and_resets_explicitly() {
    let mut session = MeterSession::new(48_000, 2).unwrap();
    feed(&mut session, 0.8, 40);
    let high = session.snapshot();
    feed(&mut session, 0.01, 40);
    session.pause();
    let paused = session.snapshot();
    assert_eq!(paused.state, MeterSessionState::Paused);
    assert_eq!(paused.maximum.lufs_m, high.maximum.lufs_m);
    assert_eq!(paused.maximum.true_peak, high.maximum.true_peak);
    assert!(paused.maximum.lufs_s.unwrap() >= high.maximum.lufs_s.unwrap());
    assert!(paused.maximum.crest.unwrap() >= high.maximum.crest.unwrap());
    assert!(paused.maximum.lufs_m.unwrap() > paused.current.lufs_m.unwrap() + 20.0);
    let publication = MeterSessionPublication::new(paused.clone());
    assert_eq!(
        publication.try_snapshot().unwrap().maximum.true_peak,
        paused.maximum.true_peak
    );
    session.reset();
    let reset = session.snapshot();
    assert_eq!(reset.generation, paused.generation + 1);
    assert!(reset.maximum.lufs_m.is_none() && reset.maximum.true_peak.is_none());
    assert!(reset.maximum.lufs_s.is_none() && reset.maximum.crest.is_none());
}
