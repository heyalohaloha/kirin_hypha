use super::*;

pub const KIRIN_SPECTRUM_SELECTION_MID_SIDE: u8 = 3;

/// POST-local coherent Mid/Side Spectrum. Both arrays come from one analysis aperture.
#[derive(Clone, Copy)]
#[repr(C)]
pub struct KirinMidSideSpectrumView {
    pub status: u8,
    pub has_data: u8,
    pub channels: u8,
    pub reserved: u8,
    pub sample_rate: u32,
    pub min_hz: f32,
    pub max_hz: f32,
    pub mid_dbfs: [f32; SPECTRUM_BAND_COUNT],
    pub side_dbfs: [f32; SPECTRUM_BAND_COUNT],
    pub presentation_end_samples: i64,
    pub aperture_samples: u32,
    pub fft_size: u32,
    pub approximate_below_hz: f32,
}

impl KirinHyphaEngine {
    pub fn set_mid_side_spectrum_visible(&self, visible: bool) -> bool {
        let is_post =
            self.write_role.lock().ok().and_then(|role| *role) == Some(PluginDataRole::Post);
        if !is_post || (visible && self.spectrum_runtime.num_channels() != 2) {
            return false;
        }
        if visible {
            if let Some(runtime) = self.attack_runtime.as_ref() {
                runtime.set_enabled(false);
            }
            if !self.spectrum.set_post_mid_side_enabled(true) {
                return false;
            }
        }
        self.spectrum.set_post_visible(visible);
        true
    }

    pub fn poll_mid_side_spectrum(&self) -> Option<kirin_measure::MidSideSpectrumViewSnapshot> {
        self.spectrum.try_mid_side_view()
    }
}

fn empty_view() -> KirinMidSideSpectrumView {
    KirinMidSideSpectrumView {
        status: KIRIN_SPECTRUM_HIDDEN,
        has_data: 0,
        channels: 0,
        reserved: 0,
        sample_rate: 0,
        min_hz: 0.0,
        max_hz: 0.0,
        mid_dbfs: [0.0; SPECTRUM_BAND_COUNT],
        side_dbfs: [0.0; SPECTRUM_BAND_COUNT],
        presentation_end_samples: 0,
        aperture_samples: 0,
        fft_size: 0,
        approximate_below_hz: 0.0,
    }
}

fn to_c_view(snapshot: kirin_measure::MidSideSpectrumViewSnapshot) -> KirinMidSideSpectrumView {
    let mut out = empty_view();
    out.status = spectrum_status_to_abi(snapshot.status);
    out.channels = snapshot.channels;
    let Some(frame) = snapshot.frame.filter(|frame| frame.has_valid_layout()) else {
        return out;
    };
    out.has_data = 1;
    out.channels = frame.mid.channels;
    out.sample_rate = frame.mid.sample_rate;
    out.min_hz = frame.mid.min_hz;
    out.max_hz = frame.mid.max_hz;
    out.mid_dbfs = frame.mid.dbfs;
    out.side_dbfs = frame.side.dbfs;
    out.presentation_end_samples = frame.presentation_end_samples();
    out.aperture_samples = frame.mid.aperture_samples;
    out.fft_size = frame.mid.fft_size;
    out.approximate_below_hz =
        3.0 * frame.mid.sample_rate as f32 / frame.mid.aperture_samples as f32;
    out
}

/// Enable or disable coherent POST-local Mid/Side Spectrum analysis.
///
/// # Safety
/// `handle` must be null or a live pointer returned by `kirin_hypha_create`.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_set_mid_side_spectrum_visible(
    handle: *mut KirinHyphaEngine,
    visible: bool,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        !handle.is_null() && unsafe { (*handle).set_mid_side_spectrum_visible(visible) }
    }))
    .unwrap_or(false)
}

/// Poll the latest coherent Mid/Side observation. A status-only view is a successful read.
///
/// # Safety
/// `handle` and `out` must be live writable pointers. UI thread only.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_mid_side_spectrum(
    handle: *mut KirinHyphaEngine,
    out: *mut KirinMidSideSpectrumView,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if handle.is_null() || out.is_null() {
            return false;
        }
        let Some(snapshot) = (unsafe { &*handle }).poll_mid_side_spectrum() else {
            return false;
        };
        unsafe { *out = to_c_view(snapshot) };
        true
    }))
    .unwrap_or(false)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn abi_layout_is_fixed_and_status_only_is_empty() {
        assert_eq!(std::mem::size_of::<KirinMidSideSpectrumView>(), 2_088);
        assert_eq!(std::mem::align_of::<KirinMidSideSpectrumView>(), 8);
        assert_eq!(
            std::mem::offset_of!(KirinMidSideSpectrumView, sample_rate),
            4
        );
        assert_eq!(std::mem::offset_of!(KirinMidSideSpectrumView, mid_dbfs), 16);
        assert_eq!(
            std::mem::offset_of!(KirinMidSideSpectrumView, side_dbfs),
            1_040
        );
        assert_eq!(
            std::mem::offset_of!(KirinMidSideSpectrumView, presentation_end_samples),
            2_064
        );
        let view = to_c_view(kirin_measure::MidSideSpectrumViewSnapshot {
            status: SpectrumViewStatus::WarmingUp,
            channels: 2,
            ..Default::default()
        });
        assert_eq!(view.status, KIRIN_SPECTRUM_WARMING_UP);
        assert_eq!(view.has_data, 0);
        assert_eq!(view.channels, 2);
    }

    #[test]
    fn null_poll_rejects_without_mutating_the_output() {
        let mut output = empty_view();
        output.status = 123;
        assert!(!unsafe { kirin_hypha_poll_mid_side_spectrum(std::ptr::null_mut(), &mut output) });
        assert_eq!(output.status, 123);
        assert!(!unsafe {
            kirin_hypha_poll_mid_side_spectrum(std::ptr::null_mut(), std::ptr::null_mut())
        });
    }

    #[test]
    fn complete_frame_maps_both_series_without_relabeling() {
        let mut analyzer = kirin_measure::SpectrumAnalyzer::new(48_000).unwrap();
        let left = vec![0.5; kirin_measure::SPECTRUM_WINDOW_SIZE];
        let right = vec![-0.25; kirin_measure::SPECTRUM_WINDOW_SIZE];
        let mid = analyzer
            .analyze_mode(&left, Some(&right), SpectrumChannelMode::Mid, 9_600, 7)
            .unwrap();
        let side = analyzer
            .analyze_mode(&left, Some(&right), SpectrumChannelMode::Side, 9_600, 7)
            .unwrap();
        let frame =
            kirin_measure::MidSideSpectrumFrame::from_frames(mid.clone(), side.clone()).unwrap();
        let view = to_c_view(kirin_measure::MidSideSpectrumViewSnapshot {
            status: SpectrumViewStatus::Active,
            channels: 2,
            frame: Some(frame),
            ..Default::default()
        });
        assert_eq!(view.has_data, 1);
        assert_eq!(view.mid_dbfs, mid.dbfs);
        assert_eq!(view.side_dbfs, side.dbfs);
        assert_eq!(view.presentation_end_samples, 9_600);
    }

    #[test]
    fn mid_side_visibility_is_post_and_stereo_only() {
        let pre = KirinHyphaEngine::new(48_000, 2);
        assert!(!pre.set_mid_side_spectrum_visible(true));

        let mono = KirinHyphaEngine::new(48_000, 1);
        *mono.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        assert!(!mono.set_mid_side_spectrum_visible(true));

        let stereo = KirinHyphaEngine::new(48_000, 2);
        *stereo.write_role.lock().unwrap() = Some(PluginDataRole::Post);
        assert!(stereo.set_mid_side_spectrum_visible(true));
        assert!(stereo.spectrum_runtime.mid_side_enabled());
        assert!(stereo.set_spectrum_visible(true));
        assert!(!stereo.spectrum_runtime.mid_side_enabled());
    }
}
