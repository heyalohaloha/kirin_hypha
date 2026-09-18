//! `KirinMeterSession` の C ABI 定義。
//!
//! 追加は必ず末尾へ行う。既存フィールドの offset を動かさないことが互換の条件であり、
//! `meter_session_abi_tests.rs` の offset 表と JUCE 側の `static_assert` が同時に守っている。

use crate::{KIRIN_MONO_SUM_BAND_COUNT, KIRIN_STEREO_FIELD_BINS};

/// Record/Keepから独立した常設メーター。current/session値は同じ`observed_frames`境界、値なしはNaN。
#[repr(C)]
pub struct KirinMeterSession {
    pub generation: u64,
    pub active_frames: u64,
    pub observed_frames: u64,
    pub sample_rate: u32,
    pub state: u8,
    pub reserved: [u8; 3],
    pub lufs_m: f64,
    pub lufs_s: f64,
    pub lufs_i: f64,
    pub lra: f64,
    pub true_peak: f64,
    pub max_true_peak: f64,
    pub plr: f64,
    pub channels: u8,
    pub balance_state: u8,
    pub channel_clip_latched: [u8; 2],
    pub stereo_reserved: [u8; 4],
    pub sample_peak_dbfs: [f64; 2],
    pub sample_peak_hold_dbfs: [f64; 2],
    pub channel_true_peak_dbtp: [f64; 2],
    pub channel_max_true_peak_dbtp: [f64; 2],
    pub clip_events: [u64; 2],
    pub balance_db: f64,
    pub correlation: f64,
    pub field_size: u8,
    pub field_observation_count: u8,
    pub field_reserved: [u8; 6],
    pub field_density: [u8; KIRIN_STEREO_FIELD_BINS],
    /// EBU Mode Maximum Momentary through `observed_frames`; append-only ABI field.
    pub max_lufs_m: f64,
    /// Full-wave average, sine-calibrated, over the latest exact 300 ms.
    pub channel_vu_dbfs: [f64; 2],
    /// Per-channel ITU-R BS.1770 True Peak of the latest exact 100 ms observation.
    pub channel_instant_true_peak_dbtp: [f64; 2],
    /// 0 when MONO is unavailable, `KIRIN_MONO_SUM_BAND_COUNT` when it is. A flag, not a variable
    /// band count; append-only ABI field.
    pub mono_sum_band_count: u8,
    pub mono_sum_reserved: [u8; 3],
    /// Bands under this frequency hold fewer than three cycles in one observation.
    ///
    /// This is the observation layout's boundary, not a property of what the latest observation
    /// held, so it stays valid while `mono_sum_band_count` is 0 and a held display keeps its
    /// marking. It is 0 only before the first stereo observation has established a layout.
    pub mono_sum_approximate_below_hz: f32,
    /// How much of each third-octave band survives the mono sum. NaN is a band with nothing to
    /// measure, never a band that reads 0 dB.
    pub mono_sum_db: [f32; KIRIN_MONO_SUM_BAND_COUNT],
}
