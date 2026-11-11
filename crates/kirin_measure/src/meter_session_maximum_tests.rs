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
    let mut session = MeterSession::new(48_000, ChannelLayout::stereo()).unwrap();
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

#[test]
fn song_psr_reads_the_loudest_part_and_ignores_a_sudden_drop() {
    let mut session = MeterSession::new(48_000, ChannelLayout::stereo()).unwrap();
    assert!(session.snapshot().song_psr.is_none());
    feed(&mut session, 0.8, 40);
    let loud = session.snapshot();
    let steady = loud.current.psr.unwrap();
    let song = loud.song_psr.unwrap();
    // A steady loud part: the song's PSR is the PSR heard there.
    assert!((song - steady).abs() < 0.05, "song {song} steady {steady}");
    // The drop: the 400 ms peak falls before the 3 s loudness does, so PSR of the moment dips far
    // below anything the song held. The song's figure stays where the loud part put it.
    let mut lowest_now = f64::INFINITY;
    for _ in 0..30 {
        feed(&mut session, 0.05, 1);
        if let Some(now) = session.snapshot().current.psr {
            lowest_now = lowest_now.min(now);
        }
    }
    let after = session.snapshot().song_psr.unwrap();
    assert!(
        lowest_now < song - 3.0,
        "the drop dips: {lowest_now} vs {song}"
    );
    assert!(
        (after - song).abs() < 0.05,
        "song {song} after the drop {after}"
    );
    session.pause();
    assert_eq!(session.snapshot().song_psr, Some(after));
    session.reset();
    assert!(session.snapshot().song_psr.is_none());
}

#[test]
fn song_psr_counts_a_peak_too_quiet_for_loudness() {
    let mut session = MeterSession::new(48_000, ChannelLayout::stereo()).unwrap();
    // One sample at -74 dBFS in silence: below the loudness floor, so neither S nor PSR exists,
    // yet it is the highest peak of this quiet piece.
    let impulse = 2.0e-4;
    let mut spike = vec![0.0; 4_800 * 2];
    spike[100] = impulse;
    spike[101] = impulse;
    assert!(session.push_active(&spike));
    let during = session.snapshot();
    assert!(during.current.psr.is_none() && during.current.lufs_s.is_none());
    feed(&mut session, 0.0, 10);
    // Then a steady part whose peak is lower (-80 dBFS) and whose loudness is measured.
    feed(&mut session, 1.0e-4, 40);
    let snapshot = session.snapshot();
    assert!(snapshot.current.psr.is_some());
    let loudest = snapshot.maximum.lufs_s.unwrap();
    let song = snapshot.song_psr.unwrap();
    let expected = 20.0 * impulse.log10() - loudest;
    let from_the_steady_part = 20.0 * 1.0e-4_f64.log10() - loudest;
    assert!(
        (song - expected).abs() < 0.05,
        "song {song} expected {expected}"
    );
    assert!(
        song > from_the_steady_part + 5.0,
        "the impulse, not the steady part, sets the peak"
    );
}
