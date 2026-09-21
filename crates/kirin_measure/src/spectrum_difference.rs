//! POST − PRE の帯域差。**比較が成立する条件は `SpectrumFrame::compatible_with` が持つ。**
//!
//! `spectrum.rs` から分けている（B-971 / 行数規律）。値の定義はここ、成立条件はあちら。

use super::{
    SpectrumChannelMode, SpectrumFrame, SPECTRUM_APPROXIMATE_CYCLES, SPECTRUM_BAND_COUNT,
    SPECTRUM_DISPLAY_FLOOR_END_DBFS, SPECTRUM_DISPLAY_FLOOR_START_DBFS,
};

#[derive(Clone, Debug, PartialEq)]
pub struct SpectrumDifference {
    pub presentation_end_samples: i64,
    pub sample_rate: u32,
    pub aperture_samples: u32,
    pub fft_size: u32,
    pub approximate_below_hz: f32,
    pub min_hz: f32,
    pub max_hz: f32,
    pub channel_mode: SpectrumChannelMode,
    /// どの観測対象から作られたか（`SpectrumView::to_abi()`）。`SPECTRUM_VIEW_NONE` は
    /// 「名乗っていない」であって既定値ではない。
    ///
    /// **`channel_mode` を view の名札として読まない。** 単一チャンネル view でも
    /// `channel_mode` は `Lr` のままである（導出 view 専用の処理選択なので）。
    /// 役割を選んだフレームを「LR」と表示すると、値は正しいのに意味が違う（D-13 の G）。
    pub view: u8,
    pub channels: u8,
    /// Exact PRE magnitude used for this difference. Presentation only; never fed back to DSP.
    pub pre_dbfs: [f32; SPECTRUM_BAND_COUNT],
    /// Exact POST magnitude used for this difference. Presentation only; never fed back to DSP.
    pub post_dbfs: [f32; SPECTRUM_BAND_COUNT],
    /// Signed POST - PRE difference. This raw fact is never clipped.
    pub raw_db: [f32; SPECTRUM_BAND_COUNT],
    /// Display-only floor confidence. The raw difference above remains untouched.
    pub display_db: [f32; SPECTRUM_BAND_COUNT],
}

pub fn difference_post_minus_pre(
    post: &SpectrumFrame,
    pre: &SpectrumFrame,
) -> Option<SpectrumDifference> {
    if !post.compatible_with(pre) {
        return None;
    }
    let mut raw_db = [0.0; SPECTRUM_BAND_COUNT];
    let mut display_db = [0.0; SPECTRUM_BAND_COUNT];
    for index in 0..SPECTRUM_BAND_COUNT {
        raw_db[index] = post.dbfs[index] - pre.dbfs[index];
        let audible = post.dbfs[index].max(pre.dbfs[index]);
        let confidence = ((audible - SPECTRUM_DISPLAY_FLOOR_START_DBFS)
            / (SPECTRUM_DISPLAY_FLOOR_END_DBFS - SPECTRUM_DISPLAY_FLOOR_START_DBFS))
            .clamp(0.0, 1.0);
        display_db[index] = raw_db[index] * confidence;
    }
    Some(SpectrumDifference {
        presentation_end_samples: post.presentation_end_samples,
        sample_rate: post.sample_rate,
        aperture_samples: post.aperture_samples,
        fft_size: post.fft_size,
        approximate_below_hz: SPECTRUM_APPROXIMATE_CYCLES * post.sample_rate as f32
            / post.aperture_samples as f32,
        min_hz: post.min_hz,
        max_hz: post.max_hz,
        channel_mode: post.channel_mode,
        view: post.view,
        channels: post.channels,
        pre_dbfs: pre.dbfs,
        post_dbfs: post.dbfs,
        raw_db,
        display_db,
    })
}
