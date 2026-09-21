//! One test that spans every link from audio to the struct the plug-in reads.
//!
//! The pieces each have their own coverage. That is exactly how the six-second field came to be
//! wired to an entry point nothing ships and still looked done, so this file exists to join them.

use super::super::meter_session_ffi::to_c_meter_session;
use super::super::*;
use kirin_measure::MeterSession;

/// The whole chain, from audio to the struct the plug-in reads: MeterSession, its snapshot, and
/// `to_c_meter_session`. The pieces are each covered on their own, which is how the six-second
/// field came to be wired to an entry point nothing ships; one test that spans them all is what
/// catches a link that was never joined.
#[test]
fn mono_sum_travels_from_audio_to_the_c_struct() {
    use kirin_measure::mono_sum::{MONO_SUM_BAND_COUNT, MONO_SUM_MAX_HZ, MONO_SUM_MIN_HZ};

    const SR: u32 = 48_000;
    let band_tones = |index: usize| -> f64 {
        (0..MONO_SUM_BAND_COUNT)
            .map(|band| {
                let (low, high) = kirin_measure::log_bands::log_band_edges(
                    band,
                    MONO_SUM_BAND_COUNT,
                    MONO_SUM_MIN_HZ,
                    MONO_SUM_MAX_HZ,
                );
                let centre = (low as f64 * high as f64).sqrt();
                (std::f64::consts::TAU * centre * index as f64 / SR as f64).sin() * 0.02
            })
            .sum()
    };

    let mut session = MeterSession::new(SR, kirin_measure::channel_layout::ChannelLayout::stereo())
        .expect("session");
    // Hard panned left, so every measured band has to read exactly -3.01 dB.
    let mut interleaved = Vec::with_capacity(4_800 * 2 * 3);
    for index in 0..(4_800 * 3) {
        interleaved.push(band_tones(index));
        interleaved.push(0.0);
    }
    assert!(session.push_active(&interleaved));

    let mapped = to_c_meter_session(&session.snapshot());
    assert_eq!(
        mapped.mono_sum_band_count, KIRIN_MONO_SUM_BAND_COUNT as u8,
        "MONO never reached the struct the plug-in reads"
    );
    assert!((mapped.mono_sum_approximate_below_hz - 30.0).abs() < 0.001);

    let measured = mapped
        .mono_sum_db
        .iter()
        .filter(|value| value.is_finite())
        .count();
    assert!(measured >= 26, "only {measured} bands crossed the boundary");
    for value in mapped.mono_sum_db.iter().filter(|value| value.is_finite()) {
        assert!(
            (value - (-3.0103)).abs() < 0.1,
            "hard panned audio arrived as {value} dB"
        );
    }
}
