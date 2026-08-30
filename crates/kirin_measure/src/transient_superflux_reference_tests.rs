use super::*;

#[test]
fn source_provenance_is_revision_and_digest_pinned() {
    assert!(PAPER_2013_SOURCE_URL.ends_with("Boeck_DAFx-13.pdf"));
    assert!(CPJKU_1_03_SOURCE_URL.contains(CPJKU_1_03_SOURCE_REVISION));
    for digest in [
        PAPER_2013_SOURCE_SHA256,
        CPJKU_1_03_SOURCE_SHA256,
        CPJKU_1_03_BIN_GOLDEN_SHA256,
        PAPER_2013_ONLINE_DEFINITION_SHA256,
        CPJKU_1_03_ONLINE_DEFINITION_SHA256,
    ] {
        assert_eq!(digest.len(), 64);
        assert!(digest.bytes().all(|byte| byte.is_ascii_hexdigit()));
    }
    assert_eq!(
        PAPER_2013_ONLINE.parameter_source_url,
        PAPER_2013_SOURCE_URL
    );
    assert_eq!(
        PAPER_2013_ONLINE.numerical_source_revision,
        CPJKU_1_03_SOURCE_REVISION
    );
    assert_eq!(
        CPJKU_1_03_ONLINE.parameter_source_sha256,
        CPJKU_1_03_SOURCE_SHA256
    );
}

#[test]
fn named_contracts_hold_the_full_shared_algorithm() {
    for contract in [PAPER_2013_ONLINE, CPJKU_1_03_ONLINE] {
        assert_eq!(contract.sample_rate_hz, 44_100);
        assert_eq!(contract.frame_size_samples, 2_048);
        assert_eq!(contract.fft_bins, 1_024);
        assert_eq!(contract.frame_rate_fps, 200);
        assert_eq!(contract.hop_numerator_samples, 44_100);
        assert_eq!(contract.hop_denominator_frames, 200);
        assert_eq!(
            contract.window,
            SuperFluxReferenceWindow::NumpySymmetricHann
        );
        assert_eq!(contract.bands_per_octave, 24);
        assert_eq!(
            contract.bin_rounding,
            SuperFluxReferenceBinRounding::NumpyTiesToEven
        );
        assert_eq!(
            contract.magnitude_compression,
            SuperFluxReferenceMagnitudeCompression::Log10OnePlus
        );
        assert_eq!(contract.spectral_lag_frames, 2);
        assert_eq!(contract.maximum_filter_radius, 1);
        assert_eq!(contract.maximum_filter_width, 3);
        assert_eq!(
            contract.band_reduction,
            SuperFluxReferenceBandReduction::Sum
        );
        assert_eq!(contract.effective_post_max_frames(), 0);
        assert_eq!(contract.effective_post_avg_frames(), 0);
        assert_eq!(contract.combine_declared_input_micros, 30_000);
    }
}

#[test]
fn named_contracts_preserve_their_documented_differences() {
    assert_eq!(PAPER_2013_ONLINE.frequency_min_millihz, 27_500);
    assert_eq!(PAPER_2013_ONLINE.frequency_max_millihz, 16_000_000);
    assert_eq!(PAPER_2013_ONLINE.reported_band_count, Some(138));
    assert_eq!(PAPER_2013_ONLINE.pre_max_ms, 30);
    assert_eq!(PAPER_2013_ONLINE.pre_avg_ms, 100);
    assert_eq!(PAPER_2013_ONLINE.pre_max_frames(), 6);
    assert_eq!(PAPER_2013_ONLINE.pre_avg_frames(), 20);
    assert_eq!(PAPER_2013_ONLINE.declared_post_max_ms, 30);
    assert_eq!(PAPER_2013_ONLINE.declared_post_avg_ms, 70);
    assert_eq!(PAPER_2013_ONLINE.combine_implementation_divisor, 1);
    assert_eq!(PAPER_2013_ONLINE.effective_combine_micros(), Some(30_000));
    assert_eq!(
        PAPER_2013_ONLINE.delta,
        SuperFluxReferenceDelta::DatasetSweptNoSingleValue
    );

    assert_eq!(CPJKU_1_03_ONLINE.frequency_min_millihz, 30_000);
    assert_eq!(CPJKU_1_03_ONLINE.frequency_max_millihz, 17_000_000);
    assert_eq!(CPJKU_1_03_ONLINE.reported_band_count, None);
    assert_eq!(CPJKU_1_03_ONLINE.pre_max_ms, 10);
    assert_eq!(CPJKU_1_03_ONLINE.pre_avg_ms, 150);
    assert_eq!(CPJKU_1_03_ONLINE.pre_max_frames(), 2);
    assert_eq!(CPJKU_1_03_ONLINE.pre_avg_frames(), 30);
    assert_eq!(CPJKU_1_03_ONLINE.declared_post_max_ms, 50);
    assert_eq!(CPJKU_1_03_ONLINE.declared_post_avg_ms, 0);
    assert_eq!(CPJKU_1_03_ONLINE.combine_implementation_divisor, 1_000);
    assert_eq!(CPJKU_1_03_ONLINE.effective_combine_micros(), Some(30));
    assert_eq!(
        CPJKU_1_03_ONLINE.delta,
        SuperFluxReferenceDelta::FixedMilliLogMagnitude(1_100)
    );
}

#[test]
fn cpjku_layout_matches_the_pinned_official_source_golden() {
    let bins = cpjku_filter_bins(CPJKU_1_03_ONLINE).expect("valid CPJKU layout");
    assert_eq!(bins.len(), 143);
    assert_eq!(&bins[..16], &(1_u32..=16).collect::<Vec<_>>());
    assert_eq!(
        &bins[bins.len() - 16..],
        &[519, 534, 550, 566, 583, 600, 617, 635, 654, 673, 693, 713, 734, 755, 778, 800]
    );
    assert_eq!(hex::encode(hash_bins(&bins)), CPJKU_1_03_BIN_GOLDEN_SHA256);

    let receipt = build_superflux_reference_receipt(CPJKU_1_03_ONLINE)
        .expect("official CPJKU control must build");
    assert_eq!(receipt.realized_unique_bin_points, 143);
    assert_eq!(receipt.realized_band_count, 141);
    assert_eq!(
        receipt.layout_status,
        SuperFluxReferenceLayoutStatus::VerifiedAgainstCpjku103BinGolden
    );
}

#[test]
fn paper_band_count_discrepancy_is_explicit_and_not_claimed_as_verified() {
    let receipt = build_superflux_reference_receipt(PAPER_2013_ONLINE)
        .expect("known paper/source discrepancy must remain representable");
    assert_eq!(receipt.realized_unique_bin_points, 141);
    assert_eq!(receipt.realized_band_count, 139);
    assert_eq!(PAPER_2013_ONLINE.reported_band_count, Some(138));
    assert_eq!(
        receipt.layout_status,
        SuperFluxReferenceLayoutStatus::PaperReported138ButCpjkuNumericsRealize139
    );
}

#[test]
fn every_reference_receipt_is_structurally_ranking_ineligible() {
    for contract in [PAPER_2013_ONLINE, CPJKU_1_03_ONLINE] {
        let receipt = build_superflux_reference_receipt(contract).expect("named contract");
        assert_eq!(
            receipt.use_contract,
            SuperFluxReferenceUse::ExternalValidationOnlyRankingIneligible
        );
        assert!(!receipt.ranking_eligible());
        assert_eq!(
            receipt.trace_status,
            SuperFluxReferenceTraceStatus::PendingNoOfficialAudioActivationGolden
        );
    }
}

#[test]
fn numpy_rounding_matches_ties_to_even_at_boundaries() {
    assert_eq!(numpy_round_ties_even(0.49), 0);
    assert_eq!(numpy_round_ties_even(0.5), 0);
    assert_eq!(numpy_round_ties_even(1.5), 2);
    assert_eq!(numpy_round_ties_even(2.5), 2);
    assert_eq!(numpy_round_ties_even(3.5), 4);
    assert_eq!(numpy_round_ties_even(4.51), 5);
}

#[test]
fn malformed_layout_contracts_fail_closed() {
    for malformed in [
        SuperFluxReferenceContract {
            fft_bins: 2,
            ..CPJKU_1_03_ONLINE
        },
        SuperFluxReferenceContract {
            bands_per_octave: 0,
            ..CPJKU_1_03_ONLINE
        },
        SuperFluxReferenceContract {
            frequency_min_millihz: 0,
            ..CPJKU_1_03_ONLINE
        },
        SuperFluxReferenceContract {
            frequency_max_millihz: CPJKU_1_03_ONLINE.frequency_min_millihz,
            ..CPJKU_1_03_ONLINE
        },
    ] {
        assert_eq!(
            cpjku_filter_bins(malformed),
            Err("invalid SuperFlux reference filter parameters")
        );
    }
}

#[test]
fn zero_combine_divisor_fails_closed() {
    let malformed = SuperFluxReferenceContract {
        combine_implementation_divisor: 0,
        ..CPJKU_1_03_ONLINE
    };
    assert_eq!(malformed.effective_combine_micros(), None);
    assert_eq!(
        build_superflux_reference_receipt(malformed),
        Err("SuperFlux named reference contract mismatch")
    );
}

#[test]
fn definition_hashes_are_deterministic_distinct_and_cover_parameters() {
    let paper_hash = PAPER_2013_ONLINE.definition_hash();
    let cpjku_hash = CPJKU_1_03_ONLINE.definition_hash();
    assert_eq!(hex::encode(paper_hash), PAPER_2013_ONLINE_DEFINITION_SHA256);
    assert_eq!(hex::encode(cpjku_hash), CPJKU_1_03_ONLINE_DEFINITION_SHA256);
    assert_eq!(paper_hash, PAPER_2013_ONLINE.definition_hash());
    assert_eq!(cpjku_hash, CPJKU_1_03_ONLINE.definition_hash());
    assert_ne!(paper_hash, cpjku_hash);

    macro_rules! assert_field_hashed {
        ($field:ident, $value:expr) => {{
            let changed = SuperFluxReferenceContract {
                $field: $value,
                ..CPJKU_1_03_ONLINE
            };
            assert_ne!(
                changed.definition_hash(),
                cpjku_hash,
                "{} must affect the definition hash",
                stringify!($field)
            );
        }};
    }

    assert_field_hashed!(id, SuperFluxReferenceId::Paper2013Online);
    assert_field_hashed!(parameter_source_url, "https://invalid.example/parameters");
    assert_field_hashed!(parameter_source_sha256, "00");
    assert_field_hashed!(numerical_source_url, "https://invalid.example/numerics");
    assert_field_hashed!(numerical_source_revision, "different-revision");
    assert_field_hashed!(numerical_source_sha256, "11");
    assert_field_hashed!(sample_rate_hz, 48_000);
    assert_field_hashed!(frame_size_samples, 4_096);
    assert_field_hashed!(fft_bins, 2_048);
    assert_field_hashed!(frame_rate_fps, 100);
    assert_field_hashed!(hop_numerator_samples, 48_000);
    assert_field_hashed!(hop_denominator_frames, 100);
    assert_field_hashed!(bands_per_octave, 12);
    assert_field_hashed!(frequency_min_millihz, 31_000);
    assert_field_hashed!(frequency_max_millihz, 16_000_000);
    assert_field_hashed!(reported_band_count, Some(141));
    assert_field_hashed!(spectral_lag_frames, 1);
    assert_field_hashed!(maximum_filter_radius, 2);
    assert_field_hashed!(maximum_filter_width, 5);
    assert_field_hashed!(pre_max_ms, 20);
    assert_field_hashed!(pre_avg_ms, 100);
    assert_field_hashed!(declared_post_max_ms, 20);
    assert_field_hashed!(declared_post_avg_ms, 10);
    assert_field_hashed!(effective_post_max_ms, 10);
    assert_field_hashed!(effective_post_avg_ms, 10);
    assert_field_hashed!(combine_declared_input_micros, 20_000);
    assert_field_hashed!(combine_implementation_divisor, 500);
    assert_field_hashed!(
        combine_provenance,
        SuperFluxReferenceCombineProvenance::PaperDeclaredThirtyMilliseconds
    );
    assert_field_hashed!(
        delta,
        SuperFluxReferenceDelta::FixedMilliLogMagnitude(1_200)
    );
}

#[test]
fn receipt_rejects_a_mutated_named_contract() {
    let changed = SuperFluxReferenceContract {
        pre_max_ms: CPJKU_1_03_ONLINE.pre_max_ms + 1,
        ..CPJKU_1_03_ONLINE
    };
    assert_eq!(
        build_superflux_reference_receipt(changed),
        Err("SuperFlux named reference contract mismatch")
    );
}
