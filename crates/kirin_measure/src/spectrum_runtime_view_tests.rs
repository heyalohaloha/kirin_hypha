//! runtime 側の selector 検証（承認事項 3A / D-5）。
//!
//! 値空間そのものは `channel_layout::spectrum_view` が持つ。ここで見るのは
//! 「runtime が layout を実際に持っていて、**測れない view を受理しないか**」である。

use super::*;
use crate::channel_layout::{ChannelLayout, ChannelRole, LayoutId};

#[test]
fn a_view_the_layout_does_not_offer_is_refused() {
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    assert_eq!(runtime.view(), Some(SpectrumView::Lr));

    // stereo が持ち、解析経路が測れるもの。
    assert!(runtime.set_view(SpectrumView::Mid));
    assert_eq!(runtime.view(), Some(SpectrumView::Mid));

    // stereo が持たないもの。**拒否して、選択はそのまま。** 近い view へ丸めない。
    assert!(!runtime.set_view(SpectrumView::Channel(ChannelRole::Centre)));
    assert!(!runtime.set_view(SpectrumView::Channel(ChannelRole::Lfe)));
    assert_eq!(runtime.view(), Some(SpectrumView::Mid));
}

#[test]
fn a_single_channel_view_is_refused_until_the_analysis_can_honour_it() {
    // **これが無いと `view()` は `R` と答え、frame は LR を運ぶ。** 値空間が先に開いて
    // 解析経路が追いついていない状態を、受理して隠さない（D-13）。P-4 で受理に変わる。
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::stereo());
    assert!(SpectrumView::Channel(ChannelRole::Right).is_available_in(ChannelLayout::stereo()));
    assert!(!runtime.set_view(SpectrumView::Channel(ChannelRole::Right)));
    assert_eq!(runtime.view(), Some(SpectrumView::Lr));
    assert_eq!(runtime.channel_mode(), SpectrumChannelMode::Lr);
}

#[test]
fn a_wider_layout_is_carried_as_it_is_instead_of_being_clamped_to_two() {
    // B-963 以前は clamp(1, 2) で 2ch として記録され、以後の block がすべて拒否されて
    // Spectrum が無言で何も出さなかった。実チャンネル数を持てば、測れないことが現れる。
    let layout = ChannelLayout::by_id(LayoutId::Surround5_1);
    let runtime = SpectrumRuntime::new(48_000, layout);
    assert_eq!(runtime.num_channels(), 6);
    assert_eq!(runtime.layout().id(), LayoutId::Surround5_1);

    // 5.1 は導出 view を提供せず、単一チャンネル view はまだ測れない。
    // **したがって観測対象が無い。「既定の view」を名乗らない。**
    assert_eq!(runtime.view(), None);
    for view in SpectrumView::available_in(layout) {
        assert!(!runtime.set_view(view), "{:?} must be refused", view);
    }
}

#[test]
fn a_layout_the_worker_cannot_deinterleave_is_refused_instead_of_silently_jamming() {
    // worker の de-interleave は `num_channels == 2` のときだけ 2 本を pop し、それ以外は 1 本。
    // 6ch を受理すると interleave が 1 本の流れとして扱われ、ring が溜まって drop が積み上がる。
    // B-963 が clamp を外したことで **クリーンな count 不一致拒否が、この無言の詰まりに変わった。**
    let runtime = SpectrumRuntime::new(48_000, ChannelLayout::by_id(LayoutId::Surround5_1));
    assert!(!runtime.set_enabled(true), "5.1 must not be enabled");

    let frames = 48_000usize / 10;
    let block: Vec<f32> = (0..frames * 6)
        .map(|i| ((i % 6) as f32 + 1.0) * 0.1)
        .collect();
    let mut position = 0_i64;
    for chunk in block.chunks(256 * 6) {
        runtime.push_block_from_audio(chunk, 6, Some(position));
        position += (chunk.len() / 6) as i64;
    }
    let stats = runtime.stats();
    // 受理していないので、詰まりも「欠落」も無い。**未対応は欠落ではない。**
    assert_eq!((stats.pushed_blocks, stats.dropped_blocks), (0, 0));
    assert_eq!(stats.analyzed_frames, 0);
    runtime.shutdown_and_join();
}

#[test]
fn mono_and_stereo_still_enable() {
    // 上の門が現行の対応範囲を巻き込んでいないこと。mono は 1ch の pop が正しい。
    for layout in [ChannelLayout::mono(), ChannelLayout::stereo()] {
        let runtime = SpectrumRuntime::new(48_000, layout);
        assert!(runtime.set_enabled(true), "{:?}", layout.id().as_str());
        runtime.shutdown_and_join();
    }
}
