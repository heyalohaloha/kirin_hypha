//! runtime 側の selector 検証（承認事項 3A / D-5）。
//!
//! 値空間そのものは `channel_layout::spectrum_view` が持つ。ここで見るのは
//! 「runtime が layout を実際に持っていて、提供しない view を拒否するか」である。

use super::*;
use crate::channel_layout::{ChannelLayout, ChannelRole, LayoutId};

#[test]
fn a_view_the_layout_does_not_offer_is_refused() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    assert_eq!(runtime.view(), SpectrumView::Lr);

    // stereo が持つもの。
    assert!(runtime.set_view(SpectrumView::Mid));
    assert_eq!(runtime.view(), SpectrumView::Mid);
    assert!(runtime.set_view(SpectrumView::Channel(ChannelRole::Right)));
    assert_eq!(runtime.view(), SpectrumView::Channel(ChannelRole::Right));

    // stereo が持たないもの。**拒否して、選択はそのまま。** 近い view へ丸めない。
    assert!(!runtime.set_view(SpectrumView::Channel(ChannelRole::Centre)));
    assert!(!runtime.set_view(SpectrumView::Channel(ChannelRole::Lfe)));
    assert_eq!(runtime.view(), SpectrumView::Channel(ChannelRole::Right));
}

#[test]
fn a_wider_layout_is_carried_as_it_is_instead_of_being_clamped_to_two() {
    // B-963 以前は clamp(1, 2) で 2ch として記録され、以後の block がすべて拒否されて
    // Spectrum が無言で何も出さなかった。実チャンネル数を持てば、測れないことが現れる。
    let layout = ChannelLayout::by_id(LayoutId::Surround5_1);
    let runtime = SpectrumRuntime::new(48_000, layout);
    assert_eq!(runtime.num_channels(), 6);
    assert_eq!(runtime.layout().id(), LayoutId::Surround5_1);

    // 5.1 は導出 view を提供しない（前方ペアの M/S を作品の M/S と呼ばない）。
    assert!(!runtime.set_view(SpectrumView::Mid));
    assert!(!runtime.set_view(SpectrumView::Side));
    assert!(!runtime.set_view(SpectrumView::Lr));
    // 既定はバッファ先頭の役割。
    assert_eq!(runtime.view(), SpectrumView::Channel(ChannelRole::Left));
}
