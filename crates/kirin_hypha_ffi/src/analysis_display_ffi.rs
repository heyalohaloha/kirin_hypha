use super::*;

pub(super) fn to_c_perceptual(snapshot: SpectrumViewSnapshot) -> KirinPerceptualView {
    let difference = (snapshot.analysis_mode == AnalysisViewMode::Perceptual)
        .then_some(snapshot.perceptual_difference)
        .flatten();
    let has_data = difference.is_some() as u8;
    let (sample_rate, aperture_samples, pre, post, delta, presentation_end_samples, state_epoch) =
        difference.map_or((0, 0, f64::NAN, f64::NAN, f64::NAN, 0, 0), |difference| {
            (
                difference.sample_rate,
                difference.aperture_samples,
                difference.pre_sharpness,
                difference.post_sharpness,
                difference.delta_sharpness,
                difference.presentation_end_samples,
                difference.state_epoch_samples,
            )
        });
    KirinPerceptualView {
        status: spectrum_status_to_abi(snapshot.status),
        has_data,
        channel_mode: snapshot.channel_mode as u8,
        channels: snapshot.channels,
        sample_rate,
        aperture_samples,
        pre_sharpness: pre,
        post_sharpness: post,
        delta_sharpness: delta,
        presentation_end_samples,
        state_epoch_samples: state_epoch,
    }
}

pub(super) fn empty_c_perceptual() -> KirinPerceptualView {
    KirinPerceptualView {
        status: KIRIN_SPECTRUM_HIDDEN,
        has_data: 0,
        channel_mode: KIRIN_SPECTRUM_CHANNEL_LR,
        channels: 0,
        sample_rate: 0,
        aperture_samples: 0,
        pre_sharpness: f64::NAN,
        post_sharpness: f64::NAN,
        delta_sharpness: f64::NAN,
        presentation_end_samples: 0,
        state_epoch_samples: 0,
    }
}

pub(super) fn to_c_perceptual_batch(snapshot: SpectrumViewSnapshot) -> KirinPerceptualBatch {
    let latest = to_c_perceptual(snapshot.clone());
    let mut frames = [empty_c_perceptual(); PERCEPTUAL_DIFFERENCE_TIMELINE_CAPACITY];
    let mut count = 0usize;
    if snapshot.status == SpectrumViewStatus::Active
        && snapshot.analysis_mode == AnalysisViewMode::Perceptual
    {
        for difference in snapshot.perceptual_timeline.frames() {
            frames[count] = KirinPerceptualView {
                status: KIRIN_SPECTRUM_ACTIVE,
                has_data: 1,
                channel_mode: difference.channel_mode as u8,
                channels: difference.channels,
                sample_rate: difference.sample_rate,
                aperture_samples: difference.aperture_samples,
                pre_sharpness: difference.pre_sharpness,
                post_sharpness: difference.post_sharpness,
                delta_sharpness: difference.delta_sharpness,
                presentation_end_samples: difference.presentation_end_samples,
                state_epoch_samples: difference.state_epoch_samples,
            };
            count += 1;
        }
    }
    KirinPerceptualBatch {
        latest,
        count: count as u32,
        reserved: 0,
        frames,
    }
}

pub(super) fn empty_c_absolute() -> KirinAbsoluteView {
    KirinAbsoluteView {
        status: KIRIN_SPECTRUM_HIDDEN,
        has_data: 0,
        channels: 0,
        reserved: 0,
        sample_rate: 0,
        aperture_samples: 0,
        lufs_m: f64::NAN,
        true_peak: f64::NAN,
        sharpness: f64::NAN,
        presentation_end_samples: 0,
        state_epoch_samples: 0,
        generation: 0,
    }
}

pub(super) fn to_c_absolute_batch(snapshot: SpectrumViewSnapshot) -> KirinAbsoluteBatch {
    let mut latest = empty_c_absolute();
    latest.status = spectrum_status_to_abi(snapshot.status);
    latest.channels = snapshot.channels;
    let mut frames = [empty_c_absolute(); ABSOLUTE_TIMELINE_CAPACITY];
    let mut count = 0usize;
    if snapshot.status == SpectrumViewStatus::Active
        && snapshot.analysis_mode == AnalysisViewMode::Absolute
    {
        for frame in snapshot.absolute_timeline.frames() {
            frames[count] = KirinAbsoluteView {
                status: KIRIN_SPECTRUM_ACTIVE,
                has_data: 1,
                channels: frame.channels,
                reserved: 0,
                sample_rate: frame.sample_rate,
                aperture_samples: frame.aperture_samples,
                lufs_m: opt_f64(frame.lufs_m),
                true_peak: opt_f64(frame.true_peak),
                sharpness: opt_f64(frame.sharpness),
                presentation_end_samples: frame.presentation_end_samples,
                state_epoch_samples: frame.state_epoch_samples,
                generation: frame.generation,
            };
            count += 1;
        }
        if count > 0 {
            latest = frames[count - 1];
        }
    }
    KirinAbsoluteBatch {
        latest,
        count: count as u32,
        reserved: 0,
        frames,
    }
}
