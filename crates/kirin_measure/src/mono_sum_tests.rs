//! MONO golden values. Every anchor is derived from the definition, not from a previous run.

use super::*;
use crate::channel_layout::ChannelLayout;
use crate::log_bands::log_band_edges;

const SR: u32 = 48_000;
const FRAMES: usize = 4_800; // one exact 100 ms observation

fn analyzer(sample_rate: u32, frames: usize) -> MonoSumAnalyzer {
    MonoSumAnalyzer::new(sample_rate, frames).expect("layout")
}

/// Interleaved stereo built from a per-sample generator.
fn stereo(frames: usize, mut generate: impl FnMut(usize) -> (f64, f64)) -> Vec<f64> {
    let mut out = Vec::with_capacity(frames * 2);
    for index in 0..frames {
        let (left, right) = generate(index);
        out.push(left);
        out.push(right);
    }
    out
}

fn band_centre_hz(band: usize) -> f64 {
    let (low, high) = log_band_edges(band, MONO_SUM_BAND_COUNT, MONO_SUM_MIN_HZ, MONO_SUM_MAX_HZ);
    (low as f64 * high as f64).sqrt()
}

fn tone(index: usize, hz: f64, sample_rate: u32) -> f64 {
    (std::f64::consts::TAU * hz * index as f64 / sample_rate as f64).sin()
}

/// One tone at every band centre, so no band is left without content to measure.
///
/// An earlier version of these tests used a hash-based "broadband noise" generator. It turned out
/// to carry almost nothing below about 150 Hz, so every band down there was undefined and the
/// golden values were only ever checked over the upper half of the spectrum. Tones at the band
/// centres remove the guesswork: each band has a known level at a known frequency.
fn band_tones(index: usize, sample_rate: u32) -> f64 {
    (0..MONO_SUM_BAND_COUNT)
        .map(|band| tone(index, band_centre_hz(band), sample_rate) * 0.02)
        .sum()
}

/// Bands whose low edge is under the three-cycle boundary cannot be asserted on: the observation
/// does not hold enough cycles to place their content, which is why the readout marks them.
fn resolved_bands(analyzer: &MonoSumAnalyzer) -> impl Iterator<Item = usize> + '_ {
    (0..MONO_SUM_BAND_COUNT).filter(move |band| {
        let (low, _) = log_band_edges(*band, MONO_SUM_BAND_COUNT, MONO_SUM_MIN_HZ, MONO_SUM_MAX_HZ);
        low >= analyzer.approximate_below_hz()
    })
}

#[test]
fn identical_channels_lose_nothing() {
    let mut analyzer = analyzer(SR, FRAMES);
    let samples = stereo(FRAMES, |index| {
        let value = band_tones(index, SR);
        (value, value)
    });
    let bands = analyzer.analyze(&samples).expect("analyzed");
    let mut checked = 0;
    for band in resolved_bands(&analyzer) {
        let value = bands[band].unwrap_or_else(|| panic!("band {band} has no content"));
        assert!(
            value.abs() < 0.1,
            "band {band} read {value} dB, expected 0.00"
        );
        checked += 1;
    }
    assert!(checked >= 26, "only {checked} bands were resolved");
}

#[test]
fn one_channel_alone_loses_three_decibels() {
    // L = x, R = 0 gives M = S = x/2, so exactly half the energy survives: 10*log10(0.5).
    let mut analyzer = analyzer(SR, FRAMES);
    let samples = stereo(FRAMES, |index| (band_tones(index, SR), 0.0));
    let bands = analyzer.analyze(&samples).expect("analyzed");
    for band in resolved_bands(&analyzer) {
        let value = bands[band].unwrap_or_else(|| panic!("band {band} has no content"));
        assert!(
            (value - (-3.0103)).abs() < 0.1,
            "band {band} read {value} dB, expected -3.01"
        );
    }
}

#[test]
fn a_three_to_one_balance_loses_one_decibel() {
    let mut analyzer = analyzer(SR, FRAMES);
    let samples = stereo(FRAMES, |index| {
        let value = band_tones(index, SR);
        (value, value / 3.0)
    });
    let bands = analyzer.analyze(&samples).expect("analyzed");
    for band in resolved_bands(&analyzer) {
        let value = bands[band].unwrap_or_else(|| panic!("band {band} has no content"));
        assert!(
            (value - (-0.9691)).abs() < 0.1,
            "band {band} read {value} dB, expected -0.97"
        );
    }
}

#[test]
fn an_inverted_pair_reaches_the_floor_and_is_not_undefined() {
    // The band vanishing in mono is the finding this measurement exists for. Reporting it as
    // undefined would hide exactly the case a user needs to see.
    let mut analyzer = analyzer(SR, FRAMES);
    let samples = stereo(FRAMES, |index| {
        let value = band_tones(index, SR);
        (value, -value)
    });
    let bands = analyzer.analyze(&samples).expect("analyzed");
    for band in resolved_bands(&analyzer) {
        let value =
            bands[band].unwrap_or_else(|| panic!("band {band} must stay measured, not undefined"));
        assert!(
            (value - MONO_SUM_FLOOR_DB).abs() < 0.001,
            "band {band} read {value} dB, expected the {MONO_SUM_FLOOR_DB} dB floor"
        );
    }
}

#[test]
fn silence_is_undefined_and_never_zero() {
    let mut analyzer = analyzer(SR, FRAMES);
    let samples = stereo(FRAMES, |_| (0.0, 0.0));
    let bands = analyzer.analyze(&samples).expect("analyzed");
    assert!(
        bands.iter().all(Option::is_none),
        "silence must not report a survival figure"
    );
}

#[test]
fn only_the_inverted_band_drops() {
    // Every band carries its own tone; one band's tone is inverted between the channels. That band
    // has to fall to the floor and its neighbours have to stay put. Localising a cancellation is
    // the whole point of splitting the measurement into bands.
    let mut analyzer = analyzer(SR, FRAMES);
    let target = (0..MONO_SUM_BAND_COUNT)
        .find(|band| {
            let (low, high) =
                log_band_edges(*band, MONO_SUM_BAND_COUNT, MONO_SUM_MIN_HZ, MONO_SUM_MAX_HZ);
            (low..high).contains(&200.0)
        })
        .expect("a band holds 200 Hz");

    let samples = stereo(FRAMES, |index| {
        let common: f64 = (0..MONO_SUM_BAND_COUNT)
            .filter(|band| *band != target)
            .map(|band| tone(index, band_centre_hz(band), SR) * 0.02)
            .sum();
        let inverted = tone(index, band_centre_hz(target), SR) * 0.02;
        (common + inverted, common - inverted)
    });
    let bands = analyzer.analyze(&samples).expect("analyzed");

    let at_target = bands[target].expect("the inverted band stays measured");
    assert!(
        at_target < -20.0,
        "the inverted band read {at_target} dB, expected it to collapse"
    );
    for band in resolved_bands(&analyzer) {
        if band == target {
            continue;
        }
        let value = bands[band].unwrap_or_else(|| panic!("band {band} has no content"));
        assert!(
            value > -1.0,
            "band {band} read {value} dB; one band's cancellation must not move its neighbours"
        );
    }
}

#[test]
fn a_loud_cancellation_does_not_move_a_quieter_band() {
    // The band that cancels is usually the loud one. If its window leakage reaches a quieter band
    // it lands in the Side spectrum there, and that band reports a loss it does not have.
    //
    // What limits this is always the band next door, so the main lobe decides it. Measured at
    // 100 ms: a cancellation 20 dB above its neighbour moves it 0.07 dB, 30 dB moves it 0.66 dB,
    // 40 dB moves it 4.2 dB, 50 dB moves it 12.4 dB. The gate is set at 30 dB, which is where the
    // reading is still worth trusting; the numbers above are the honest limit beyond it.
    let mut analyzer = analyzer(SR, FRAMES);
    let target = (0..MONO_SUM_BAND_COUNT)
        .find(|band| {
            let (low, high) =
                log_band_edges(*band, MONO_SUM_BAND_COUNT, MONO_SUM_MIN_HZ, MONO_SUM_MAX_HZ);
            (low..high).contains(&200.0)
        })
        .expect("a band holds 200 Hz");

    let samples = stereo(FRAMES, |index| {
        let quiet: f64 = (0..MONO_SUM_BAND_COUNT)
            .filter(|band| *band != target)
            .map(|band| tone(index, band_centre_hz(band), SR) * 0.0127)
            .sum();
        let loud = tone(index, band_centre_hz(target), SR) * 0.4;
        (quiet + loud, quiet - loud)
    });
    let bands = analyzer.analyze(&samples).expect("analyzed");

    for band in resolved_bands(&analyzer) {
        if band == target {
            continue;
        }
        let value = bands[band].unwrap_or_else(|| panic!("band {band} has no content"));
        assert!(
            value > -1.0,
            "band {band} read {value} dB; a 30 dB louder cancellation must not reach it"
        );
    }
}

#[test]
fn the_same_signal_reads_the_same_at_any_host_rate() {
    // The observation is 100 ms at whatever rate the host runs. The values must not depend on it.
    let mut at_48 = analyzer(48_000, 4_800);
    let mut at_96 = analyzer(96_000, 9_600);
    let samples_48 = stereo(4_800, |index| (tone(index, 1_000.0, 48_000) * 0.4, 0.0));
    let samples_96 = stereo(9_600, |index| (tone(index, 1_000.0, 96_000) * 0.4, 0.0));
    let bands_48 = at_48.analyze(&samples_48).expect("48 kHz");
    let bands_96 = at_96.analyze(&samples_96).expect("96 kHz");
    let mut compared = 0;
    for (left, right) in bands_48.iter().zip(bands_96.iter()) {
        if let (Some(left), Some(right)) = (left, right) {
            assert!(
                (left - right).abs() < 0.5,
                "48 kHz {left} vs 96 kHz {right}"
            );
            compared += 1;
        }
    }
    assert!(compared > 0, "no band was comparable across rates");
}

#[test]
fn an_observation_of_the_wrong_length_is_refused() {
    // Zero padding a short observation would report a value for time that was never observed.
    let mut analyzer = analyzer(SR, FRAMES);
    assert!(analyzer
        .analyze(&stereo(FRAMES - 1, |_| (0.5, 0.5)))
        .is_none());
    assert!(analyzer
        .analyze(&stereo(FRAMES + 1, |_| (0.5, 0.5)))
        .is_none());
    assert!(analyzer.analyze(&[]).is_none());
}

#[test]
fn an_unusable_layout_is_refused_instead_of_guessed() {
    assert!(MonoSumAnalyzer::new(0, FRAMES).is_none());
    assert!(MonoSumAnalyzer::new(SR, 0).is_none());
    assert!(MonoSumAnalyzer::new(SR, 8).is_none());
    assert!(MonoSumAnalyzer::new(7_999, FRAMES).is_none());
}

#[test]
fn the_approximate_boundary_follows_the_observation_length() {
    // Three cycles in 100 ms is 30 Hz, the same three-cycle rule the Spectrum readout uses.
    let analyzer = analyzer(SR, FRAMES);
    assert!((analyzer.approximate_below_hz() - 30.0).abs() < 0.001);
    let longer = MonoSumAnalyzer::new(SR, 9_600).expect("layout");
    assert!((longer.approximate_below_hz() - 15.0).abs() < 0.001);
}

#[test]
fn every_band_covers_a_distinct_rising_frequency_range() {
    let mut previous_high = 0.0_f32;
    for index in 0..MONO_SUM_BAND_COUNT {
        let (low, high) = crate::log_bands::log_band_edges(
            index,
            MONO_SUM_BAND_COUNT,
            MONO_SUM_MIN_HZ,
            MONO_SUM_MAX_HZ,
        );
        assert!(high > low, "band {index} is empty");
        if index > 0 {
            assert!(
                (low - previous_high).abs() < 0.01,
                "band {index} leaves a gap"
            );
        }
        previous_high = high;
    }
    assert!((previous_high - MONO_SUM_MAX_HZ).abs() < 1.0);
}

/// Cost of one observation, against the work the Meter Session already does for the same 100 ms.
/// Run with `cargo test -p kirin_measure --release --lib mono_sum::tests::cost -- --ignored
/// --nocapture`. Release only; a Debug figure measures the optimiser being off.
#[test]
#[ignore]
fn cost_of_one_observation() {
    use std::time::Instant;

    let samples = stereo(FRAMES, |index| {
        let value = band_tones(index, SR);
        (value, value * 0.6)
    });

    let mut analyzer = analyzer(SR, FRAMES);
    for _ in 0..50 {
        analyzer.analyze(&samples).expect("warm");
    }
    const RUNS: u32 = 500;
    let started = Instant::now();
    for _ in 0..RUNS {
        analyzer.analyze(&samples).expect("analyzed");
    }
    let mono_us = started.elapsed().as_secs_f64() * 1e6 / f64::from(RUNS);

    let mut meter =
        crate::stereo_meter::StereoMeter::new(SR, ChannelLayout::stereo()).expect("meter");
    for _ in 0..50 {
        meter.push_observation(&samples);
    }
    let started = Instant::now();
    for _ in 0..RUNS {
        meter.push_observation(&samples);
    }
    let meter_us = started.elapsed().as_secs_f64() * 1e6 / f64::from(RUNS);

    println!(
        "MONO {mono_us:.1} us/observation, existing StereoMeter {meter_us:.1} us, \
         share of one 100 ms observation = {:.3}%",
        mono_us / 100_000.0 * 100.0
    );
}
