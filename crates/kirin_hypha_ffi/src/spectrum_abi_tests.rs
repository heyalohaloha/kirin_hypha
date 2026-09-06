use super::*;
use kirin_measure::spectrum::SpectrumDifference;

#[test]
fn spectrum_status_and_signed_display_values_have_stable_c_mapping() {
    assert_eq!(std::mem::size_of::<KirinSpectrumView>(), 3_112);
    assert_eq!(
        std::mem::offset_of!(KirinSpectrumView, presentation_end_samples),
        3_088
    );
    assert_eq!(
        std::mem::offset_of!(KirinSpectrumView, aperture_samples),
        3_096
    );
    assert_eq!(std::mem::offset_of!(KirinSpectrumView, fft_size), 3_100);
    assert_eq!(
        std::mem::offset_of!(KirinSpectrumView, approximate_below_hz),
        3_104
    );
    assert_eq!(spectrum_status_to_abi(SpectrumViewStatus::Hidden), 0);
    assert_eq!(spectrum_status_to_abi(SpectrumViewStatus::NoPair), 1);
    assert_eq!(spectrum_status_to_abi(SpectrumViewStatus::WarmingUp), 2);
    assert_eq!(spectrum_status_to_abi(SpectrumViewStatus::Active), 3);
    assert_eq!(spectrum_status_to_abi(SpectrumViewStatus::Unavailable), 4);
    assert_eq!(spectrum_status_to_abi(SpectrumViewStatus::InUse), 5);

    let difference = SpectrumDifference {
        presentation_end_samples: 48_000,
        sample_rate: 48_000,
        aperture_samples: 4_096,
        fft_size: 8_192,
        approximate_below_hz: 35.15625,
        min_hz: 10.0,
        max_hz: 22_000.0,
        channel_mode: SpectrumChannelMode::Side,
        channels: 2,
        pre_dbfs: [-42.0; SPECTRUM_BAND_COUNT],
        post_dbfs: [-45.5; SPECTRUM_BAND_COUNT],
        raw_db: [15.0; SPECTRUM_BAND_COUNT],
        display_db: [-3.5; SPECTRUM_BAND_COUNT],
    };
    let mut spectrum_timeline = kirin_measure::SpectrumDifferenceTimeline::default();
    spectrum_timeline.push(&difference);
    let snapshot = SpectrumViewSnapshot {
        status: SpectrumViewStatus::Active,
        analysis_mode: AnalysisViewMode::Spectrum,
        channel_mode: SpectrumChannelMode::Side,
        channels: 2,
        difference: Some(difference),
        spectrum_timeline,
        post_spectrum: None,
        post_spectrum_history: Default::default(),
        perceptual_difference: None,
        perceptual_timeline: Default::default(),
        absolute_timeline: Default::default(),
        analysis_owner_names: Default::default(),
    };
    let out = to_c_spectrum(snapshot.clone());
    assert_eq!(out.status, KIRIN_SPECTRUM_ACTIVE);
    assert_eq!(out.has_data, 1);
    assert_eq!(out.post_has_data, 1);
    assert_eq!(out.sample_rate, 48_000);
    assert_eq!(out.channel_mode, KIRIN_SPECTRUM_CHANNEL_SIDE);
    assert_eq!(out.channels, 2);
    assert_eq!(out.pre_dbfs[0], -42.0);
    assert_eq!(out.post_dbfs[SPECTRUM_BAND_COUNT - 1], -45.5);
    assert_eq!(out.display_db[0], -3.5);
    assert_eq!(out.display_db[SPECTRUM_BAND_COUNT - 1], -3.5);
    assert_eq!(out.presentation_end_samples, 48_000);
    assert_eq!(out.aperture_samples, 4_096);
    assert_eq!(out.fft_size, 8_192);
    assert_eq!(out.approximate_below_hz, 35.15625);
    let batch = to_c_spectrum_batch(snapshot);
    // The four-byte POST-presence tail occupies the struct's former alignment padding.
    assert_eq!(std::mem::size_of::<KirinSpectrumView>(), 3_112);
    assert_eq!(std::mem::size_of::<KirinSpectrumBatch>(), 28_016);
    assert_eq!(batch.count, 1);
    assert_eq!(batch.latest.presentation_end_samples, 48_000);
    assert_eq!(batch.frames[0].presentation_end_samples, 48_000);
    assert_eq!(batch.frames[0].display_db[0], -3.5);
}

#[test]
fn unpaired_post_spectrum_is_distinct_from_exact_delta_at_the_abi() {
    let snapshot = SpectrumViewSnapshot {
        status: SpectrumViewStatus::NoPair,
        analysis_mode: AnalysisViewMode::Spectrum,
        channel_mode: SpectrumChannelMode::Lr,
        channels: 2,
        difference: None,
        spectrum_timeline: Default::default(),
        post_spectrum: Some(SpectrumFrame {
            schema_version: kirin_measure::SPECTRUM_SCHEMA_VERSION,
            sample_rate: 48_000,
            aperture_samples: 4_096,
            fft_size: 8_192,
            band_count: SPECTRUM_BAND_COUNT as u16,
            presentation_end_samples: 9_600,
            generation: 4,
            channel_mode: SpectrumChannelMode::Lr,
            channels: 2,
            min_hz: 10.0,
            max_hz: 22_000.0,
            dbfs: [-27.5; SPECTRUM_BAND_COUNT],
        }),
        post_spectrum_history: Default::default(),
        perceptual_difference: None,
        perceptual_timeline: Default::default(),
        absolute_timeline: Default::default(),
        analysis_owner_names: Default::default(),
    };
    let out = to_c_spectrum(snapshot);
    assert_eq!(out.status, KIRIN_SPECTRUM_NO_PAIR);
    assert_eq!(out.has_data, 0);
    assert_eq!(out.post_has_data, 1);
    assert_eq!(out.post_dbfs[0], -27.5);
    assert_eq!(out.presentation_end_samples, 9_600);
    assert_eq!(out.pre_dbfs, [0.0; SPECTRUM_BAND_COUNT]);
    assert_eq!(out.display_db, [0.0; SPECTRUM_BAND_COUNT]);
}

#[test]
fn perceptual_view_preserves_signed_raw_sharpness_and_exact_endpoint() {
    assert_eq!(std::mem::size_of::<KirinPerceptualView>(), 56);
    assert_eq!(
        std::mem::offset_of!(KirinPerceptualView, presentation_end_samples),
        40
    );
    assert_eq!(
        std::mem::offset_of!(KirinPerceptualView, state_epoch_samples),
        48
    );
    let difference = kirin_measure::PerceptualDifference {
        presentation_end_samples: 96_000,
        state_epoch_samples: 0,
        sample_rate: 48_000,
        aperture_samples: 4_800,
        channel_mode: SpectrumChannelMode::Mid,
        channels: 2,
        pre_sharpness: 1.25,
        post_sharpness: 0.85,
        delta_sharpness: -0.40,
    };
    let mut perceptual_timeline = kirin_measure::PerceptualDifferenceTimeline::default();
    perceptual_timeline.push(difference);
    let snapshot = SpectrumViewSnapshot {
        status: SpectrumViewStatus::Active,
        analysis_mode: AnalysisViewMode::Perceptual,
        channel_mode: SpectrumChannelMode::Mid,
        channels: 2,
        difference: None,
        spectrum_timeline: Default::default(),
        post_spectrum: None,
        post_spectrum_history: Default::default(),
        perceptual_difference: Some(difference),
        perceptual_timeline,
        absolute_timeline: Default::default(),
        analysis_owner_names: Default::default(),
    };
    let out = to_c_perceptual(snapshot.clone());
    assert_eq!(out.status, KIRIN_SPECTRUM_ACTIVE);
    assert_eq!(out.has_data, 1);
    assert_eq!(out.channel_mode, KIRIN_SPECTRUM_CHANNEL_MID);
    assert_eq!(out.aperture_samples, 4_800);
    assert_eq!(out.pre_sharpness, 1.25);
    assert_eq!(out.post_sharpness, 0.85);
    assert_eq!(out.delta_sharpness, -0.40);
    assert_eq!(out.presentation_end_samples, 96_000);
    assert_eq!(out.state_epoch_samples, 0);

    let batch = to_c_perceptual_batch(snapshot.clone());
    assert_eq!(std::mem::size_of::<KirinPerceptualBatch>(), 3_648);
    assert_eq!(batch.count, 1);
    assert_eq!(batch.latest.presentation_end_samples, 96_000);
    assert_eq!(batch.frames[0].presentation_end_samples, 96_000);
    assert_eq!(batch.frames[0].delta_sharpness, -0.40);

    let mut wrong_mode = snapshot;
    wrong_mode.analysis_mode = AnalysisViewMode::Spectrum;
    let hidden = to_c_perceptual(wrong_mode);
    assert_eq!(hidden.has_data, 0);
    assert!(hidden.delta_sharpness.is_nan());
}

#[test]
fn absolute_batch_preserves_exact_post_facts_without_delta() {
    assert_eq!(std::mem::size_of::<KirinAbsoluteView>(), 64);
    assert_eq!(std::mem::size_of::<KirinAbsoluteBatch>(), 4_168);
    let mut absolute_timeline = kirin_measure::AbsoluteTimeline::default();
    assert!(absolute_timeline.push(kirin_measure::AbsoluteFrame {
        schema_version: kirin_measure::ABSOLUTE_SCHEMA_VERSION,
        sample_rate: 48_000,
        aperture_samples: 4_800,
        presentation_end_samples: 9_600,
        state_epoch_samples: 0,
        generation: 7,
        channels: 2,
        lufs_m: Some(-18.5),
        true_peak: Some(-2.0),
        sharpness: Some(1.25),
    }));
    let batch = to_c_absolute_batch(SpectrumViewSnapshot {
        status: SpectrumViewStatus::Active,
        analysis_mode: AnalysisViewMode::Absolute,
        channel_mode: SpectrumChannelMode::Lr,
        channels: 2,
        difference: None,
        spectrum_timeline: Default::default(),
        post_spectrum: None,
        post_spectrum_history: Default::default(),
        perceptual_difference: None,
        perceptual_timeline: Default::default(),
        absolute_timeline,
        analysis_owner_names: Default::default(),
    });
    assert_eq!(batch.count, 1);
    assert_eq!(batch.latest.lufs_m, -18.5);
    assert_eq!(batch.latest.true_peak, -2.0);
    assert_eq!(batch.latest.sharpness, 1.25);
    assert_eq!(batch.latest.presentation_end_samples, 9_600);
    assert_eq!(batch.latest.generation, 7);
}

#[test]
fn analysis_owner_names_are_bounded_utf8_and_null_terminated() {
    assert_eq!(std::mem::size_of::<KirinAnalysisOwners>(), 138);
    let owners = to_c_analysis_owners(["Mix".to_string(), "Vocal".to_string()]);
    assert_eq!(owners.count, 2);
    assert_eq!(&owners.names[0][..4], b"Mix\0");
    assert_eq!(&owners.names[1][..6], b"Vocal\0");
    assert!(owners.names[0][4..].iter().all(|byte| *byte == 0));
    assert!(owners.names[1][6..].iter().all(|byte| *byte == 0));

    let unicode = to_c_analysis_owners(["ボーカル".to_string(), String::new()]);
    assert_eq!(unicode.count, 1);
    let nul = unicode.names[0].iter().position(|byte| *byte == 0).unwrap();
    assert_eq!(
        std::str::from_utf8(&unicode.names[0][..nul]).unwrap(),
        "ボーカル"
    );
}

#[test]
fn presentation_alignment_requires_known_wrapper_output_latency() {
    let exact = PendingCaptureWindow {
        position_valid: true,
        position_samples: 9_600,
        num_frames: 480,
        clock_source: CaptureClockSource::ProjectTimeline,
        presentation_latency: PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(2_048),
        },
        force_new_epoch: false,
    };
    assert_eq!(spectrum_presentation_start(exact), Some(11_648));
    assert_eq!(
        spectrum_presentation_start(PendingCaptureWindow {
            presentation_latency: PresentationLatencySamples::default(),
            ..exact
        }),
        None
    );
    assert_eq!(
        spectrum_presentation_start(PendingCaptureWindow {
            position_valid: false,
            ..exact
        }),
        None
    );
}

#[test]
fn attack_uses_exact_project_clock_when_presentation_callback_is_absent() {
    let exact = PendingCaptureWindow {
        position_valid: true,
        position_samples: 9_600,
        num_frames: 480,
        clock_source: CaptureClockSource::ProjectTimeline,
        presentation_latency: PresentationLatencySamples {
            source: PresentationLatencySource::Vst3,
            input: Some(0),
            output: Some(2_048),
        },
        force_new_epoch: false,
    };
    assert_eq!(attack_timeline_start(exact), Some(11_648));
    assert_eq!(
        attack_timeline_start(PendingCaptureWindow {
            presentation_latency: PresentationLatencySamples::default(),
            ..exact
        }),
        Some(9_600)
    );
    assert_eq!(
        attack_timeline_start(PendingCaptureWindow {
            clock_source: CaptureClockSource::AudioRenderTimeline,
            presentation_latency: PresentationLatencySamples::default(),
            ..exact
        }),
        Some(9_600)
    );
    assert_eq!(
        attack_timeline_start(PendingCaptureWindow {
            position_valid: false,
            presentation_latency: PresentationLatencySamples::default(),
            ..exact
        }),
        None
    );
    assert_eq!(
        attack_timeline_start(PendingCaptureWindow {
            clock_source: CaptureClockSource::Unknown,
            presentation_latency: PresentationLatencySamples::default(),
            ..exact
        }),
        None
    );
}

#[test]
fn pre_role_cannot_expose_the_post_spectrum_page() {
    let engine = KirinHyphaEngine::new(48_000, 2);
    assert!(!engine.set_spectrum_visible(true));
    assert!(!engine.set_spectrum_channel_mode(KIRIN_SPECTRUM_CHANNEL_MID));
    *engine.write_role.lock().unwrap() = Some(PluginDataRole::Pre);
    assert!(!engine.set_spectrum_visible(true));
    assert!(!engine.set_spectrum_channel_mode(KIRIN_SPECTRUM_CHANNEL_MID));
    assert!(!engine.spectrum_stats().enabled);
}

#[test]
fn post_channel_mode_is_single_select_and_side_requires_stereo() {
    let stereo = KirinHyphaEngine::new(48_000, 2);
    *stereo.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(stereo.set_spectrum_channel_mode(KIRIN_SPECTRUM_CHANNEL_MID));
    assert_eq!(
        stereo.spectrum_stats().channel_mode,
        SpectrumChannelMode::Mid
    );
    assert!(stereo.set_spectrum_channel_mode(KIRIN_SPECTRUM_CHANNEL_SIDE));
    assert_eq!(
        stereo.spectrum_stats().channel_mode,
        SpectrumChannelMode::Side
    );
    assert!(!stereo.set_spectrum_channel_mode(3));

    let mono = KirinHyphaEngine::new(48_000, 1);
    *mono.write_role.lock().unwrap() = Some(PluginDataRole::Post);
    assert!(mono.set_spectrum_channel_mode(KIRIN_SPECTRUM_CHANNEL_MID));
    assert!(!mono.set_spectrum_channel_mode(KIRIN_SPECTRUM_CHANNEL_SIDE));
    assert_eq!(mono.spectrum_stats().channel_mode, SpectrumChannelMode::Mid);
}
