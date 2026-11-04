use super::*;
use crate::spectrum::{SpectrumAnalyzer, SpectrumLayout, SPECTRUM_FFT_SIZE};

#[test]
fn full_keeps_the_layout_it_always_had() {
    for rate in [8_000, 44_100, 48_000, 96_000, 192_000, 384_000] {
        assert_eq!(
            SpectrumLayout::new(rate).unwrap(),
            SpectrumLayout::for_scope(rate, SpectrumScope::Full).unwrap()
        );
    }
    let reference = SpectrumLayout::new(48_000).unwrap();
    assert_eq!(reference.aperture_samples, SPECTRUM_WINDOW_SIZE);
    assert_eq!(reference.fft_size, SPECTRUM_FFT_SIZE);
    assert_eq!(reference.min_hz, SPECTRUM_MIN_HZ);
}

#[test]
fn low_keeps_one_observation_time_at_every_host_rate() {
    let expected = [
        (8_000, 2_731, 8_192),
        (44_100, 15_053, 32_768),
        (48_000, 16_384, 32_768),
        (96_000, 32_768, 65_536),
        (192_000, 65_536, 131_072),
        (384_000, 131_072, 262_144),
    ];
    for (rate, aperture_samples, fft_size) in expected {
        let layout = SpectrumLayout::for_scope(rate, SpectrumScope::Low).unwrap();
        assert_eq!(layout.aperture_samples, aperture_samples);
        assert_eq!(layout.fft_size, fft_size);
        assert_eq!(layout.min_hz, SPECTRUM_LOW_MIN_HZ);
        assert_eq!(layout.max_hz, SPECTRUM_LOW_MAX_HZ);
        let aperture_ms = aperture_samples as f64 * 1_000.0 / f64::from(rate);
        assert!(
            (aperture_ms - 341.333).abs() < 0.13,
            "{rate}: {aperture_ms}"
        );
        assert!((8.78..8.80).contains(&layout.approximate_below_hz));
    }
}

fn two_tones(rate: u32, length: usize) -> Vec<f32> {
    (0..length)
        .map(|index| {
            let time = index as f32 / rate as f32;
            let tone = |hz: f32| (std::f32::consts::TAU * hz * time).sin();
            0.25 * tone(50.0) + 0.25 * tone(60.0)
        })
        .collect()
}

/// The level of the band nearest `hz`.
fn level_at(frame: &crate::spectrum::SpectrumFrame, hz: f32) -> f32 {
    let position = (hz / frame.min_hz).ln() / (frame.max_hz / frame.min_hz).ln();
    let band = (position * frame.dbfs.len() as f32 - 0.5).round() as usize;
    frame.dbfs[band]
}

#[test]
fn low_tells_two_close_bass_notes_apart_where_full_cannot() {
    let frame_for = |scope| {
        let mut analyzer = SpectrumAnalyzer::new_for_scope(48_000, scope).unwrap();
        let samples = two_tones(48_000, analyzer.aperture_samples());
        analyzer.analyze(&samples, None, 16_384, 1).unwrap()
    };
    let dip = |frame: &crate::spectrum::SpectrumFrame| {
        level_at(frame, 50.0).min(level_at(frame, 60.0)) - level_at(frame, 54.8)
    };
    let full = frame_for(SpectrumScope::Full);
    let low = frame_for(SpectrumScope::Low);
    assert!(dip(&full) < 3.0, "FULL dip {}", dip(&full));
    assert!(dip(&low) > 6.0, "LOW dip {}", dip(&low));
}

#[test]
fn low_frames_are_valid_and_never_comparable_with_full() {
    let mut full = SpectrumAnalyzer::new(48_000).unwrap();
    let mut low = SpectrumAnalyzer::new_for_scope(48_000, SpectrumScope::Low).unwrap();
    let full_frame = full
        .analyze(&vec![0.1; full.aperture_samples()], None, 16_384, 1)
        .unwrap();
    let low_frame = low
        .analyze(&vec![0.1; low.aperture_samples()], None, 16_384, 1)
        .unwrap();
    assert!(full_frame.has_valid_layout() && low_frame.has_valid_layout());
    assert_eq!(low_frame.scope, SpectrumScope::Low);
    assert!(low_frame.same_analysis_layout(&low_frame));
    assert!(!low_frame.same_analysis_layout(&full_frame));

    let mut mislabelled = low_frame;
    mislabelled.scope = SpectrumScope::Full;
    assert!(!mislabelled.has_valid_layout());
}

#[test]
fn an_unknown_scope_code_is_refused() {
    assert_eq!(SpectrumScope::try_from(0), Ok(SpectrumScope::Full));
    assert_eq!(SpectrumScope::try_from(1), Ok(SpectrumScope::Low));
    assert_eq!(SpectrumScope::try_from(2), Err(()));
}
