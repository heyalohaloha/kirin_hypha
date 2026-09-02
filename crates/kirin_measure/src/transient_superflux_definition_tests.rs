use std::collections::HashSet;

use super::*;

fn config(
    window: u32,
    bands_per_octave: u32,
    radius: usize,
    reference_dbfs: i32,
    mode: SuperFluxChannelMode,
    channel_count: usize,
) -> SuperFluxConfig {
    SuperFluxConfig::new(
        window,
        bands_per_octave,
        radius,
        reference_dbfs,
        mode,
        channel_count,
    )
}

fn hash_for(layout: &SuperFluxLayout, bands: &[SuperFluxBand]) -> [u8; 32] {
    bank::definition_hash(
        layout.sample_rate,
        layout.window_samples,
        layout.hop_samples,
        layout.fft_size,
        layout.spectral_lag_frames,
        layout.config,
        bands,
    )
}

#[test]
fn semantic_definition_hash_is_repeatable_and_covers_every_parameter() {
    let baseline_config = config(1_024, 24, 1, -70, SuperFluxChannelMode::Lr, 1);
    let baseline = SuperFluxLayout::for_rate(48_000, baseline_config).unwrap();
    let repeated = SuperFluxLayout::for_rate(48_000, baseline_config).unwrap();
    assert_eq!(baseline.definition_hash, repeated.definition_hash);
    assert_eq!(
        baseline.definition_hex(),
        "0733522696b3dbdab5a37da86d636f4b28b5ca08d992aba8d7d42886e9a6d364"
    );

    let variants = [
        SuperFluxLayout::for_rate(96_000, baseline_config).unwrap(),
        SuperFluxLayout::for_rate(
            48_000,
            config(2_048, 24, 1, -70, SuperFluxChannelMode::Lr, 1),
        )
        .unwrap(),
        SuperFluxLayout::for_rate(
            48_000,
            config(1_024, 12, 1, -70, SuperFluxChannelMode::Lr, 1),
        )
        .unwrap(),
        SuperFluxLayout::for_rate(
            48_000,
            config(1_024, 24, 0, -70, SuperFluxChannelMode::Lr, 1),
        )
        .unwrap(),
        SuperFluxLayout::for_rate(
            48_000,
            config(1_024, 24, 1, -60, SuperFluxChannelMode::Lr, 1),
        )
        .unwrap(),
        SuperFluxLayout::for_rate(
            48_000,
            config(1_024, 24, 1, -70, SuperFluxChannelMode::Mid, 1),
        )
        .unwrap(),
        SuperFluxLayout::for_rate(
            48_000,
            config(1_024, 24, 1, -70, SuperFluxChannelMode::Side, 2),
        )
        .unwrap(),
        SuperFluxLayout::for_rate(
            48_000,
            config(1_024, 24, 1, -70, SuperFluxChannelMode::Lr, 2),
        )
        .unwrap(),
    ];
    let hashes = variants
        .iter()
        .map(|layout| layout.definition_hash)
        .chain([baseline.definition_hash])
        .collect::<HashSet<_>>();
    assert_eq!(hashes.len(), variants.len() + 1);
}

#[test]
fn semantic_hash_excludes_float_coefficients_but_keeps_integer_topology() {
    let layout = SuperFluxLayout::for_rate(
        48_000,
        config(1_024, 24, 1, -70, SuperFluxChannelMode::Lr, 1),
    )
    .unwrap();

    let mut changed_weights = layout.bands.clone();
    changed_weights[0].weights[1] = f32::from_bits(0x3eaa_aaaa);
    assert_eq!(hash_for(&layout, &changed_weights), layout.definition_hash);

    let mut changed_topology = layout.bands.clone();
    changed_topology[0].bins[1] += 1;
    assert_ne!(hash_for(&layout, &changed_topology), layout.definition_hash);
}

#[test]
fn runtime_coefficients_verify_separately_for_every_supported_layout() {
    for sample_rate in SUPERFLUX_SUPPORTED_RATES {
        for window in [1_024, 2_048] {
            for bands_per_octave in [12, 24] {
                let layout = SuperFluxLayout::for_rate(
                    sample_rate,
                    config(
                        window,
                        bands_per_octave,
                        1,
                        -70,
                        SuperFluxChannelMode::Lr,
                        1,
                    ),
                )
                .unwrap();
                let receipt = layout.verify_runtime_coefficients().unwrap();
                assert!(receipt.window_sum.is_finite() && receipt.window_sum > 0.0);
                assert_eq!(receipt.window_energy, layout.window_energy);
                assert_eq!(receipt.full_scale_gain, layout.full_scale_gain);
                assert!(receipt.band_weight_count > layout.band_count);
            }
        }
    }
}

#[test]
fn runtime_verification_rejects_finite_hann_and_interior_triangle_mutations() {
    let layout = SuperFluxLayout::for_rate(
        48_000,
        config(1_024, 24, 1, -70, SuperFluxChannelMode::Lr, 1),
    )
    .unwrap();

    let mut invalid_hann = layout.clone();
    invalid_hann.window[1] = f32::from_bits(invalid_hann.window[1].to_bits() ^ 1);
    let window_sum = invalid_hann
        .window
        .iter()
        .map(|&value| value as f64)
        .sum::<f64>() as f32;
    invalid_hann.window_energy = invalid_hann
        .window
        .iter()
        .map(|&value| f64::from(value) * f64::from(value))
        .sum::<f64>() as f32;
    invalid_hann.full_scale_gain = window_sum / (2.0 * invalid_hann.window_energy).sqrt();
    assert!(invalid_hann.verify_runtime_coefficients().is_err());

    let mut invalid_triangle = layout;
    let (band, offset) = invalid_triangle
        .bands
        .iter_mut()
        .find_map(|band| {
            let center = band.bins[1] - band.bins[0];
            (1..band.weights.len() - 1)
                .find(|&offset| offset != center)
                .map(|offset| (band, offset))
        })
        .unwrap();
    band.weights[offset] = f32::from_bits(band.weights[offset].to_bits() ^ 1);
    assert!(invalid_triangle.verify_runtime_coefficients().is_err());
}
