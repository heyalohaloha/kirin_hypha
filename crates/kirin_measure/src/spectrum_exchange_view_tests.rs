//! 交換 wire が観測対象を運ぶこと（B-971）。
//!
//! `spectrum_exchange_tests.rs` から分けている。同じ話の続きだが、あちらは 500 行に収まらない。

use super::*;
use crate::spectrum::{
    SpectrumFrame, SPECTRUM_BAND_COUNT, SPECTRUM_FFT_SIZE, SPECTRUM_SCHEMA_VERSION,
    SPECTRUM_WINDOW_SIZE,
};

pub(super) fn frame(end: i64, value: f32) -> SpectrumFrame {
    SpectrumFrame {
        schema_version: SPECTRUM_SCHEMA_VERSION,
        sample_rate: 48_000,
        aperture_samples: SPECTRUM_WINDOW_SIZE as u32,
        fft_size: SPECTRUM_FFT_SIZE as u32,
        band_count: SPECTRUM_BAND_COUNT as u16,
        presentation_end_samples: end,
        generation: 7,
        channel_mode: SpectrumChannelMode::Lr,
        view: crate::channel_layout::SpectrumView::Lr.to_abi(),
        channels: 2,
        min_hz: 10.0,
        max_hz: 22_000.0,
        dbfs: [value; SPECTRUM_BAND_COUNT],
    }
}

/// **どの観測対象で測ったかを wire が運び、違う対象どうしを引き算しない**（B-971）。
///
/// `channel_mode` は単一チャンネル view でも `Lr` のままなので、これだけでは L と C を
/// 区別できない。値は正しいのに意味が違う状態を作らない（D-13 の G）。
#[test]
fn the_wire_carries_the_view_and_two_views_are_not_differenced() {
    use crate::channel_layout::{ChannelRole, SpectrumView};

    // 予約バイトに載せたので、フレーム長は変わらず view は往復する。
    let mut role_frame = frame(4_800, -20.0);
    role_frame.view = SpectrumView::Channel(ChannelRole::Right).to_abi();
    let mut history = SpectrumHistory::with_capacity();
    history.push(role_frame.clone());
    let decoded = decode_snapshot(&encode_snapshot(Uuid::new_v4(), &history)).unwrap();
    assert_eq!(decoded.history.newest().unwrap().view, role_frame.view);

    // 同じ view どうしは従来どおり引ける。
    let pre = frame(4_800, -30.0);
    let post = frame(4_800, -20.0);
    assert_eq!(pre.view, post.view);
    assert!(crate::difference_post_minus_pre(&post, &pre).is_some());

    // 違う view どうしは引かない。`channel_mode` も `channels` も同じままである。
    assert_eq!(role_frame.channel_mode, pre.channel_mode);
    assert_eq!(role_frame.channels, pre.channels);
    assert!(crate::difference_post_minus_pre(&role_frame, &pre).is_none());
}
