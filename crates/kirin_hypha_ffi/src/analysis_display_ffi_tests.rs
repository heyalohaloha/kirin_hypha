use super::*;

#[test]
fn psb_abi_is_additive_and_null_safe() {
    assert_eq!(std::mem::size_of::<KirinPsbView>(), 192);
    assert_eq!(std::mem::offset_of!(KirinPsbView, shares), 32);
    assert!(!unsafe { kirin_hypha_poll_psb(std::ptr::null_mut(), std::ptr::null_mut()) });
    assert!(!unsafe {
        super::super::watch_display_ffi::kirin_hypha_poll_meter_display(
            std::ptr::null_mut(),
            std::ptr::null_mut(),
        )
    });
}

fn absolute_snapshot() -> SpectrumViewSnapshot {
    let mut snapshot = SpectrumViewSnapshot {
        status: SpectrumViewStatus::Active,
        analysis_mode: AnalysisViewMode::Absolute,
        channels: 2,
        ..Default::default()
    };
    assert!(snapshot
        .absolute_timeline
        .push(kirin_measure::AbsoluteFrame {
            schema_version: kirin_measure::ABSOLUTE_SCHEMA_VERSION,
            sample_rate: 48_000,
            aperture_samples: 4_800,
            presentation_end_samples: 4_800,
            state_epoch_samples: 0,
            generation: 7,
            channels: 2,
            lufs_m: Some(-18.0),
            true_peak: Some(-3.0),
            sharpness: Some(1.5),
            psb: Some([0.05; 20]),
        }));
    snapshot
}

#[test]
fn psb_absolute_export_rejects_stale_status_and_other_analysis_modes() {
    let snapshot = absolute_snapshot();
    let out = to_c_psb(snapshot.clone());
    assert_eq!(
        (out.has_data, out.is_delta, out.sample_rate),
        (1, 0, 48_000)
    );
    assert_eq!(out.shares, [0.05; 20]);
    for status in [
        SpectrumViewStatus::Hidden,
        SpectrumViewStatus::NoPair,
        SpectrumViewStatus::WarmingUp,
        SpectrumViewStatus::Unavailable,
        SpectrumViewStatus::InUse,
    ] {
        let mut unavailable = snapshot.clone();
        unavailable.status = status;
        let out = to_c_psb(unavailable);
        assert_eq!(out.has_data, 0);
        assert!(out.shares.iter().all(|v| v.is_nan()));
    }
    let mut other = snapshot;
    other.analysis_mode = AnalysisViewMode::Spectrum;
    assert_eq!(to_c_psb(other).has_data, 0);
}

#[test]
fn psb_delta_exports_signed_shares_only_when_both_are_valid() {
    let pre = kirin_measure::PerceptualFrame {
        schema_version: kirin_measure::PERCEPTUAL_SCHEMA_VERSION,
        sample_rate: 48_000,
        aperture_samples: 4_800,
        presentation_end_samples: 4_800,
        state_epoch_samples: 0,
        generation: 7,
        channel_mode: SpectrumChannelMode::Lr,
        channels: 2,
        sharpness: 1.5,
        psb: Some([0.05; 20]),
    };
    let mut post = pre.clone();
    post.psb.as_mut().unwrap()[0] += 0.02;
    post.psb.as_mut().unwrap()[1] -= 0.02;
    let difference = kirin_measure::perceptual::difference_post_minus_pre(&post, &pre).unwrap();
    let mut snapshot = SpectrumViewSnapshot {
        status: SpectrumViewStatus::Active,
        analysis_mode: AnalysisViewMode::Perceptual,
        channels: 2,
        perceptual_difference: Some(difference),
        ..Default::default()
    };
    let out = to_c_psb(snapshot.clone());
    assert_eq!((out.has_data, out.is_delta), (1, 1));
    assert!((out.shares[0] - 0.02).abs() < 1e-12);
    assert!((out.shares[1] + 0.02).abs() < 1e-12);
    let mut stale_mode = snapshot.clone();
    stale_mode
        .perceptual_difference
        .as_mut()
        .unwrap()
        .channel_mode = SpectrumChannelMode::Mid;
    assert_eq!(to_c_psb(stale_mode).has_data, 0);
    snapshot.perceptual_difference.as_mut().unwrap().pre_psb = None;
    assert_eq!(to_c_psb(snapshot).has_data, 0);
}
