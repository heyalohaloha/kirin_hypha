//! Same-candidate POST Spectrum ABI. The recovery batch carries already-computed facts only.

use kirin_measure::{
    SpectrumTimelineFrame, SPECTRUM_BAND_COUNT, SPECTRUM_DIFFERENCE_TIMELINE_CAPACITY,
};

#[derive(Clone, Copy)]
#[repr(C)]
pub struct KirinSpectrumView {
    pub status: u8,
    pub has_data: u8,
    pub channel_mode: u8,
    pub channels: u8,
    pub sample_rate: u32,
    pub min_hz: f32,
    pub max_hz: f32,
    pub pre_dbfs: [f32; SPECTRUM_BAND_COUNT],
    pub post_dbfs: [f32; SPECTRUM_BAND_COUNT],
    pub display_db: [f32; SPECTRUM_BAND_COUNT],
    pub presentation_end_samples: i64,
    pub aperture_samples: u32,
    pub fft_size: u32,
    pub approximate_below_hz: f32,
    pub post_has_data: u8,
    pub post_reserved: [u8; 3],
    pub shape_has_energy: u8,
    pub analysis_view: u8,
    pub shape_reserved: [u8; 2],
    pub shape_energy_delta_db: f32,
    pub shape_db: [f32; SPECTRUM_BAND_COUNT],
    pub shape_valid: [u8; SPECTRUM_BAND_COUNT],
}

#[repr(C)]
pub struct KirinSpectrumBatch {
    pub latest: KirinSpectrumView,
    pub count: u32,
    pub reserved: u32,
    pub frames: [KirinSpectrumView; SPECTRUM_DIFFERENCE_TIMELINE_CAPACITY],
}

pub(super) fn c_spectrum_from_timeline(
    status: u8,
    difference: &SpectrumTimelineFrame,
) -> KirinSpectrumView {
    let shape =
        std::array::from_fn::<_, SPECTRUM_BAND_COUNT, _>(|index| difference.shape_db_at(index));
    KirinSpectrumView {
        status,
        has_data: 1,
        channel_mode: difference.channel_mode as u8,
        channels: difference.channels,
        sample_rate: difference.sample_rate,
        min_hz: difference.min_hz,
        max_hz: difference.max_hz,
        pre_dbfs: difference.pre_dbfs,
        post_dbfs: difference.post_dbfs,
        display_db: difference.display_db,
        presentation_end_samples: difference.presentation_end_samples,
        aperture_samples: difference.aperture_samples,
        fft_size: difference.fft_size,
        approximate_below_hz: difference.approximate_below_hz,
        post_has_data: 1,
        post_reserved: [0; 3],
        shape_has_energy: u8::from(difference.energy_delta_db.is_some()),
        analysis_view: difference.view,
        shape_reserved: [0; 2],
        shape_energy_delta_db: difference.energy_delta_db.unwrap_or(0.0),
        shape_db: shape.map(|value| value.unwrap_or(0.0)),
        shape_valid: shape.map(|value| u8::from(value.is_some())),
    }
}

/// Poll presentation continuity together with the unchanged legacy recovery batch.
///
/// # Safety
/// All pointers must be live and writable as applicable. UI thread only.
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_poll_spectrum_batch_continuous(
    handle: *mut super::KirinHyphaEngine,
    out: *mut KirinSpectrumBatch,
    post_continuity: *mut u64,
    delta_continuity: *mut u64,
) -> bool {
    std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if handle.is_null()
            || out.is_null()
            || post_continuity.is_null()
            || delta_continuity.is_null()
        {
            return false;
        }
        let Some(snapshot) = (unsafe { &*handle }).poll_spectrum_batch() else {
            return false;
        };
        let span = snapshot.post_spectrum_history.continuity_span();
        let delta = snapshot.spectrum_timeline.continuity();
        let batch = super::to_c_spectrum_batch(snapshot);
        let post = post_identity_for(&batch.latest, span);
        unsafe {
            *out = batch;
            *post_continuity = post;
            *delta_continuity = delta;
        }
        true
    }))
    .unwrap_or(false)
}

fn post_identity_for(view: &KirinSpectrumView, span: Option<(i64, i64, u64)>) -> u64 {
    span.filter(|(start, end, _)| {
        view.post_has_data != 0 && (*start..=*end).contains(&view.presentation_end_samples)
    })
    .map_or(0, |(_, _, identity)| identity)
}

#[cfg(test)]
mod continuity_tests {
    use super::*;
    #[test]
    fn failed_poll_preserves_all_outputs_and_legacy_batch_layout() {
        let mut batch = super::super::to_c_spectrum_batch(Default::default());
        batch.count = 5;
        let (mut post, mut delta) = (123, 456);
        assert!(!unsafe {
            kirin_hypha_poll_spectrum_batch_continuous(
                std::ptr::null_mut(),
                &mut batch,
                &mut post,
                &mut delta,
            )
        });
        assert_eq!((batch.count, post, delta), (5, 123, 456));
        assert_eq!(std::mem::size_of::<KirinSpectrumBatch>(), 39_608);
        assert_eq!(std::mem::size_of::<KirinSpectrumView>(), 4_400);
    }
    #[test]
    fn a_join_cannot_borrow_a_newer_post_segments_identity() {
        let mut view = super::super::empty_c_spectrum();
        view.post_has_data = 1;
        for endpoint in [3000, 4800, 9600] {
            view.presentation_end_samples = endpoint;
            assert_eq!(post_identity_for(&view, Some((1600, 9600, 17))), 17);
        }
        view.presentation_end_samples = 4800;
        assert_eq!(post_identity_for(&view, Some((6400, 12800, 23))), 0);
        assert_eq!(post_identity_for(&view, None), 0);
        view.presentation_end_samples = 12800;
        assert_eq!(post_identity_for(&view, Some((6400, 12800, 23))), 23);
        view.post_has_data = 0;
        assert_eq!(post_identity_for(&view, Some((6400, 12800, 23))), 0);
    }
}
