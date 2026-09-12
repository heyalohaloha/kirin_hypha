use std::panic::{catch_unwind, AssertUnwindSafe};

#[repr(C)]
pub struct KirinReferenceGainFacts {
    pub paired_block_count: u64,
    pub paired_loudness_delta_median_millilu: i64,
    pub a_cue_true_peak_millidbtp: i64,
    pub b_cue_true_peak_millidbtp: i64,
}

#[repr(C)]
pub struct KirinTrackEventGainFacts {
    pub paired_window_count: u64,
    pub paired_energy_delta_millidb: i64,
    pub post_cue_true_peak_millidbtp: i64,
    pub pre_cue_true_peak_millidbtp: i64,
}

/// Sample-aligned Reference A/B PCMを既存BS.1770核で解析する（worker thread専用）。
///
/// # Safety
/// `a`と`b`は`num_frames * num_channels`個の有効なf32、`out`は書込可能であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_analyze_reference_gain(
    a: *const f32,
    b: *const f32,
    num_frames: usize,
    sample_rate: u32,
    num_channels: u32,
    out: *mut KirinReferenceGainFacts,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if a.is_null()
            || b.is_null()
            || out.is_null()
            || num_frames == 0
            || num_frames > 2_097_152
            || !matches!(num_channels, 1 | 2)
        {
            return false;
        }
        let Some(sample_count) = num_frames.checked_mul(num_channels as usize) else {
            return false;
        };
        let a_samples = unsafe { std::slice::from_raw_parts(a, sample_count) };
        let b_samples = unsafe { std::slice::from_raw_parts(b, sample_count) };
        let Ok(facts) = kirin_measure::analyze_reference_gain(
            a_samples,
            b_samples,
            sample_rate,
            num_channels as usize,
        ) else {
            return false;
        };
        unsafe {
            *out = KirinReferenceGainFacts {
                paired_block_count: facts.paired_block_count,
                paired_loudness_delta_median_millilu: facts.paired_loudness_delta_median_millilu,
                a_cue_true_peak_millidbtp: facts.a_cue_true_peak_millidbtp,
                b_cue_true_peak_millidbtp: facts.b_cue_true_peak_millidbtp,
            };
        }
        true
    }))
    .unwrap_or(false)
}

/// Exact four-second TRACK/STEM PCMの短音・疎音用固定gain facts（worker thread専用）。
///
/// # Safety
/// `post`と`pre`は`num_frames * num_channels`個の有効なf32、`out`は書込可能であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_analyze_track_event_gain(
    post: *const f32,
    pre: *const f32,
    num_frames: usize,
    sample_rate: u32,
    num_channels: u32,
    out: *mut KirinTrackEventGainFacts,
) -> bool {
    catch_unwind(AssertUnwindSafe(|| {
        if post.is_null()
            || pre.is_null()
            || out.is_null()
            || !matches!(num_channels, 1 | 2)
            || num_frames
                != usize::try_from(sample_rate)
                    .ok()
                    .and_then(|rate| rate.checked_mul(4))
                    .unwrap_or(0)
        {
            return false;
        }
        let Some(sample_count) = num_frames.checked_mul(num_channels as usize) else {
            return false;
        };
        let post_samples = unsafe { std::slice::from_raw_parts(post, sample_count) };
        let pre_samples = unsafe { std::slice::from_raw_parts(pre, sample_count) };
        let Ok(facts) = kirin_measure::reference_gain::analyze_track_event_gain(
            post_samples,
            pre_samples,
            sample_rate,
            num_channels as usize,
        ) else {
            return false;
        };
        unsafe {
            *out = KirinTrackEventGainFacts {
                paired_window_count: facts.paired_window_count,
                paired_energy_delta_millidb: facts.paired_energy_delta_millidb,
                post_cue_true_peak_millidbtp: facts.post_cue_true_peak_millidbtp,
                pre_cue_true_peak_millidbtp: facts.pre_cue_true_peak_millidbtp,
            };
        }
        true
    }))
    .unwrap_or(false)
}
