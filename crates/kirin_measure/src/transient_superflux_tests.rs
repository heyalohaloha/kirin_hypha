use std::f32::consts::PI;

use super::*;

fn config(window: u32, radius: usize, mode: SuperFluxChannelMode) -> SuperFluxConfig {
    let channel_count = usize::from(mode == SuperFluxChannelMode::Side) + 1;
    SuperFluxConfig::new(window, 24, radius, -70, mode, channel_count)
}

fn stereo_config(window: u32, radius: usize, mode: SuperFluxChannelMode) -> SuperFluxConfig {
    SuperFluxConfig::new(window, 24, radius, -70, mode, 2)
}

fn sine(len: usize, bin: usize, amplitude: f32) -> Vec<f32> {
    (0..len)
        .map(|index| amplitude * (2.0 * PI * bin as f32 * index as f32 / len as f32).sin())
        .collect()
}

fn impulse(len: usize, amplitude: f32) -> Vec<f32> {
    let mut values = vec![0.0; len];
    values[len / 2] = amplitude;
    values
}

fn deterministic_noise(len: usize) -> Vec<f32> {
    let mut state = 0x1234_5678_u32;
    (0..len)
        .map(|_| {
            state = state.wrapping_mul(1_664_525).wrapping_add(1_013_904_223);
            (state >> 8) as f32 / 0x00ff_ffff_u32 as f32 - 0.5
        })
        .collect()
}

fn onset_after_silence(sample_rate: u32, config: SuperFluxConfig, amplitude: f32) -> f32 {
    let mut analyzer = SuperFluxAnalyzer::new(sample_rate, config).unwrap();
    let silence = vec![0.0; analyzer.layout().window_samples];
    let onset = impulse(silence.len(), amplitude);
    for index in 0..analyzer.layout().spectral_lag_frames {
        assert!(analyzer
            .analyze_window(
                &silence,
                None,
                index as i64 * analyzer.layout().hop_samples as i64
            )
            .unwrap()
            .is_none());
    }
    analyzer
        .analyze_window(&onset, None, 8_192)
        .unwrap()
        .unwrap()
        .value
}

#[test]
fn two_windows_map_to_the_six_supported_host_rates() {
    let expected_1024 = [
        (44_100, 941, 235, 1_024),
        (48_000, 1_024, 256, 1_024),
        (88_200, 1_882, 470, 2_048),
        (96_000, 2_048, 512, 2_048),
        (176_400, 3_763, 941, 4_096),
        (192_000, 4_096, 1_024, 4_096),
    ];
    let expected_2048 = [
        (44_100, 1_882, 235, 2_048),
        (48_000, 2_048, 256, 2_048),
        (88_200, 3_763, 470, 4_096),
        (96_000, 4_096, 512, 4_096),
        (176_400, 7_526, 941, 8_192),
        (192_000, 8_192, 1_024, 8_192),
    ];
    for (window, lag, expected) in [(1_024, 1, expected_1024), (2_048, 2, expected_2048)] {
        for (rate, samples, hop, fft) in expected {
            let layout =
                SuperFluxLayout::for_rate(rate, config(window, 1, SuperFluxChannelMode::Lr))
                    .unwrap();
            assert_eq!(
                (
                    layout.window_samples,
                    layout.hop_samples,
                    layout.fft_size,
                    layout.spectral_lag_frames,
                ),
                (samples, hop, fft, lag)
            );
        }
    }
}

#[test]
fn invalid_grid_values_and_nonstandard_rates_fail_closed() {
    let valid = config(1_024, 1, SuperFluxChannelMode::Lr);
    assert!(SuperFluxLayout::for_rate(
        48_000,
        SuperFluxConfig::new(512, 24, 1, -70, valid.channel_mode, 1)
    )
    .is_err());
    assert!(SuperFluxLayout::for_rate(
        48_000,
        SuperFluxConfig::new(1_024, 18, 1, -70, valid.channel_mode, 1)
    )
    .is_err());
    assert!(SuperFluxLayout::for_rate(
        48_000,
        SuperFluxConfig::new(1_024, 24, 2, -70, valid.channel_mode, 1)
    )
    .is_err());
    assert!(SuperFluxLayout::for_rate(
        48_000,
        SuperFluxConfig::new(1_024, 24, 1, -65, valid.channel_mode, 1)
    )
    .is_err());
    assert!(SuperFluxLayout::for_rate(
        48_000,
        SuperFluxConfig::new(1_024, 24, 1, -70, valid.channel_mode, 0)
    )
    .is_err());
    assert!(SuperFluxLayout::for_rate(
        48_000,
        SuperFluxConfig::new(1_024, 24, 1, -70, SuperFluxChannelMode::Side, 1)
    )
    .is_err());
    assert!(SuperFluxLayout::for_rate(47_999, valid).is_err());
}

#[test]
fn filterbank_triplets_are_strict_and_exclude_dc_and_nyquist() {
    for rate in SUPERFLUX_SUPPORTED_RATES {
        for bands_per_octave in [12, 24] {
            let layout = SuperFluxLayout::for_rate(
                rate,
                SuperFluxConfig::new(1_024, bands_per_octave, 1, -70, SuperFluxChannelMode::Lr, 1),
            )
            .unwrap();
            assert!(layout.band_count > 30);
            for band in &layout.bands {
                assert!(0 < band.bins[0]);
                assert!(band.bins[0] < band.bins[1]);
                assert!(band.bins[1] < band.bins[2]);
                assert!(band.bins[2] < layout.fft_size / 2);
                assert_eq!(band.weights.first(), Some(&0.0));
                assert_eq!(band.weights[band.bins[1] - band.bins[0]], 1.0);
                assert_eq!(band.weights.last(), Some(&0.0));
            }
            assert_eq!(layout.band_triplets().len(), layout.band_count);
        }
    }
}

#[test]
fn coherent_sine_magnitude_uses_the_fixed_full_scale_gain() {
    let mut analyzer =
        SuperFluxAnalyzer::new(48_000, config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    let bin = 20;
    let tone = sine(analyzer.layout().window_samples, bin, 1.0);
    analyzer.analyze_window(&tone, None, 0).unwrap();
    assert!((analyzer.magnitude[bin] - 1.0).abs() < 2.0e-5);
}

#[test]
fn lr_averages_power_while_mid_and_side_transform_waveforms() {
    let lr_config = config(1_024, 1, SuperFluxChannelMode::Lr);
    let mut mono = SuperFluxAnalyzer::new(48_000, lr_config).unwrap();
    let mut stereo =
        SuperFluxAnalyzer::new(48_000, stereo_config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    let tone = sine(mono.layout().window_samples, 20, 1.0);
    let silence = vec![0.0; tone.len()];
    mono.analyze_window(&tone, None, 0).unwrap();
    stereo.analyze_window(&tone, Some(&silence), 0).unwrap();
    assert!((stereo.magnitude[20] / mono.magnitude[20] - 2.0_f32.sqrt().recip()).abs() < 2.0e-5);

    let inverted = tone.iter().map(|value| -*value).collect::<Vec<_>>();
    let mut mid =
        SuperFluxAnalyzer::new(48_000, stereo_config(1_024, 1, SuperFluxChannelMode::Mid)).unwrap();
    let mut side =
        SuperFluxAnalyzer::new(48_000, config(1_024, 1, SuperFluxChannelMode::Side)).unwrap();
    mid.analyze_window(&tone, Some(&inverted), 0).unwrap();
    side.analyze_window(&tone, Some(&inverted), 0).unwrap();
    assert_eq!(mid.magnitude[20], 0.0);
    assert!((side.magnitude[20] - 1.0).abs() < 2.0e-5);
}

#[test]
fn mono_lr_matches_dual_mono_and_in_phase_mid_while_side_cancels() {
    let tone = sine(1_024, 20, 0.5);
    let mut mono =
        SuperFluxAnalyzer::new(48_000, config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    let mut dual_mono =
        SuperFluxAnalyzer::new(48_000, stereo_config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    let mut mid =
        SuperFluxAnalyzer::new(48_000, stereo_config(1_024, 1, SuperFluxChannelMode::Mid)).unwrap();
    let mut side =
        SuperFluxAnalyzer::new(48_000, stereo_config(1_024, 1, SuperFluxChannelMode::Side))
            .unwrap();
    mono.analyze_window(&tone, None, 0).unwrap();
    dual_mono.analyze_window(&tone, Some(&tone), 0).unwrap();
    mid.analyze_window(&tone, Some(&tone), 0).unwrap();
    side.analyze_window(&tone, Some(&tone), 0).unwrap();
    assert_eq!(mono.magnitude, dual_mono.magnitude);
    assert_eq!(mono.magnitude, mid.magnitude);
    assert!(side.magnitude.iter().all(|value| *value == 0.0));
}

#[test]
fn silence_dc_and_stationary_tone_have_zero_flux_after_warmup() {
    let config = config(1_024, 1, SuperFluxChannelMode::Lr);
    for signal in [vec![0.0; 1_024], vec![0.25; 1_024], sine(1_024, 20, 0.5)] {
        let mut analyzer = SuperFluxAnalyzer::new(48_000, config).unwrap();
        assert!(analyzer.analyze_window(&signal, None, 0).unwrap().is_none());
        assert_eq!(
            analyzer
                .analyze_window(&signal, None, 256)
                .unwrap()
                .unwrap()
                .value,
            0.0
        );
    }
}

#[test]
fn repeated_deterministic_noise_is_stationary_for_the_odf() {
    let mut analyzer =
        SuperFluxAnalyzer::new(48_000, config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    let noise = deterministic_noise(analyzer.layout().window_samples);
    assert!(analyzer.analyze_window(&noise, None, 0).unwrap().is_none());
    assert_eq!(
        analyzer
            .analyze_window(&noise, None, 256)
            .unwrap()
            .unwrap()
            .value,
        0.0
    );
}

#[test]
fn diagnostic_band_flux_mean_exactly_reconstructs_frame_value() {
    let mut analyzer =
        SuperFluxAnalyzer::new(48_000, config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    let silence = vec![0.0; analyzer.layout().window_samples];
    let onset = impulse(silence.len(), 1.0);
    let mut bands = vec![f32::NAN; analyzer.layout().band_count];
    assert!(analyzer
        .analyze_window_with_band_flux(&silence, None, 0, &mut bands)
        .unwrap()
        .is_none());
    assert!(bands.iter().all(|value| *value == 0.0));
    let frame = analyzer
        .analyze_window_with_band_flux(&onset, None, 256, &mut bands)
        .unwrap()
        .unwrap();
    assert!(bands.iter().all(|value| value.is_finite() && *value >= 0.0));
    assert_eq!(frame.value, bands.iter().sum::<f32>() / bands.len() as f32);
}

#[test]
fn invalid_diagnostic_band_buffer_does_not_advance_history() {
    let mut analyzer =
        SuperFluxAnalyzer::new(48_000, config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    let silence = vec![0.0; analyzer.layout().window_samples];
    let mut short = vec![0.0; analyzer.layout().band_count - 1];
    assert_eq!(
        analyzer
            .analyze_window_with_band_flux(&silence, None, 0, &mut short)
            .unwrap_err(),
        "invalid SuperFlux band-flux output length"
    );
    assert!(analyzer
        .analyze_window(&silence, None, 0)
        .unwrap()
        .is_none());
}

#[test]
fn impulse_is_positive_and_fixed_scale_preserves_gain_order() {
    let config = config(1_024, 1, SuperFluxChannelMode::Lr);
    let quiet = onset_after_silence(48_000, config, 0.1);
    let loud = onset_after_silence(48_000, config, 1.0);
    assert!(quiet > 0.0);
    assert!(loud > quiet, "{loud} <= {quiet}");
}

#[test]
fn frequency_neighbor_maximum_suppresses_an_adjacent_band_change() {
    let mut radius_zero =
        SuperFluxAnalyzer::new(48_000, config(1_024, 0, SuperFluxChannelMode::Lr)).unwrap();
    let mut radius_one =
        SuperFluxAnalyzer::new(48_000, config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    let band = radius_zero.layout().band_count / 2;
    for analyzer in [&mut radius_zero, &mut radius_one] {
        analyzer.frames_seen = 1;
        analyzer.current_log_bands[band] = 1.0;
        analyzer.log_history[band + 1] = 1.0;
    }
    assert!(radius_zero.odf_value(0) > 0.0);
    assert_eq!(radius_one.odf_value(0), 0.0);
}

#[test]
fn two_hop_lag_reads_n_minus_two_not_the_previous_frame() {
    let mut analyzer =
        SuperFluxAnalyzer::new(48_000, config(2_048, 1, SuperFluxChannelMode::Lr)).unwrap();
    let silence = vec![0.0; analyzer.layout().window_samples];
    let onset = impulse(silence.len(), 1.0);
    assert!(analyzer
        .analyze_window(&silence, None, 0)
        .unwrap()
        .is_none());
    assert!(analyzer
        .analyze_window(&onset, None, 256)
        .unwrap()
        .is_none());
    assert!(
        analyzer
            .analyze_window(&onset, None, 512)
            .unwrap()
            .unwrap()
            .value
            > 0.0
    );
    assert_eq!(
        analyzer
            .analyze_window(&onset, None, 768)
            .unwrap()
            .unwrap()
            .value,
        0.0
    );
}

#[test]
fn reset_restores_warmup_and_reproducible_history() {
    let mut analyzer =
        SuperFluxAnalyzer::new(48_000, config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    let silence = vec![0.0; analyzer.layout().window_samples];
    let onset = impulse(silence.len(), 1.0);
    analyzer.analyze_window(&silence, None, 0).unwrap();
    let first = analyzer
        .analyze_window(&onset, None, 256)
        .unwrap()
        .unwrap()
        .value;
    analyzer.reset();
    assert!(analyzer
        .analyze_window(&silence, None, 0)
        .unwrap()
        .is_none());
    let second = analyzer
        .analyze_window(&onset, None, 256)
        .unwrap()
        .unwrap()
        .value;
    assert_eq!(first, second);
}

#[test]
fn invalid_input_and_timestamp_overflow_do_not_advance_history() {
    let mut analyzer =
        SuperFluxAnalyzer::new(48_000, config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    let valid = vec![0.0; analyzer.layout().window_samples];
    assert!(analyzer.analyze_window(&[0.0; 8], None, 0).is_err());
    let mut invalid = valid.clone();
    invalid[0] = f32::NAN;
    assert!(analyzer.analyze_window(&invalid, None, 0).is_err());
    assert!(analyzer.analyze_window(&valid, Some(&valid), 0).is_err());
    assert!(analyzer.analyze_window(&valid, None, i64::MAX).is_err());
    assert!(analyzer.analyze_window(&valid, None, 0).unwrap().is_none());

    let mut stereo =
        SuperFluxAnalyzer::new(48_000, stereo_config(1_024, 1, SuperFluxChannelMode::Side))
            .unwrap();
    assert!(stereo.analyze_window(&valid, None, 0).is_err());
    assert!(stereo.analyze_window(&valid, Some(&[0.0; 8]), 0).is_err());
    let mut invalid_right = valid.clone();
    invalid_right[0] = f32::INFINITY;
    assert!(stereo
        .analyze_window(&valid, Some(&invalid_right), 0)
        .is_err());
    assert!(stereo
        .analyze_window(&valid, Some(&valid), 0)
        .unwrap()
        .is_none());
}

#[test]
fn odd_window_support_uses_floor_left_and_ceil_right() {
    let mut analyzer =
        SuperFluxAnalyzer::new(44_100, config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    assert_eq!(analyzer.layout().window_samples, 941);
    let silence = vec![0.0; 941];
    analyzer.analyze_window(&silence, None, -470).unwrap();
    let frame = analyzer
        .analyze_window(&silence, None, -470)
        .unwrap()
        .unwrap();
    assert_eq!(
        (
            frame.support_start_samples,
            frame.event_sample,
            frame.support_end_samples,
        ),
        (-470, 0, 471)
    );
}

#[test]
fn steady_state_reuses_all_analyzer_buffers() {
    let mut analyzer =
        SuperFluxAnalyzer::new(48_000, config(1_024, 1, SuperFluxChannelMode::Lr)).unwrap();
    let signal = sine(analyzer.layout().window_samples, 20, 0.5);
    let pointers = (
        analyzer.fft_buffer.as_ptr(),
        analyzer.fft_scratch.as_ptr(),
        analyzer.power.as_ptr(),
        analyzer.magnitude.as_ptr(),
        analyzer.current_log_bands.as_ptr(),
        analyzer.log_history.as_ptr(),
    );
    let capacities = (
        analyzer.fft_buffer.capacity(),
        analyzer.fft_scratch.capacity(),
        analyzer.power.capacity(),
        analyzer.magnitude.capacity(),
        analyzer.current_log_bands.capacity(),
        analyzer.log_history.capacity(),
    );
    assert_eq!(
        analyzer.fft_scratch.len(),
        analyzer.fft.get_inplace_scratch_len()
    );
    for frame in 0..64 {
        analyzer
            .analyze_window(&signal, None, frame * analyzer.layout().hop_samples as i64)
            .unwrap();
    }
    assert_eq!(
        pointers,
        (
            analyzer.fft_buffer.as_ptr(),
            analyzer.fft_scratch.as_ptr(),
            analyzer.power.as_ptr(),
            analyzer.magnitude.as_ptr(),
            analyzer.current_log_bands.as_ptr(),
            analyzer.log_history.as_ptr(),
        )
    );
    assert_eq!(
        capacities,
        (
            analyzer.fft_buffer.capacity(),
            analyzer.fft_scratch.capacity(),
            analyzer.power.capacity(),
            analyzer.magnitude.capacity(),
            analyzer.current_log_bands.capacity(),
            analyzer.log_history.capacity(),
        )
    );
}

#[test]
fn impulse_is_finite_and_positive_at_every_supported_layout() {
    for rate in SUPERFLUX_SUPPORTED_RATES {
        for window in [1_024, 2_048] {
            let value = onset_after_silence(rate, config(window, 1, SuperFluxChannelMode::Lr), 1.0);
            assert!(
                value.is_finite() && value > 0.0,
                "rate={rate}, window={window}"
            );
        }
    }
}
