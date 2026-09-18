//! The channel map, proved through the shipping constructor.
//!
//! `MeasureEngine::new` is the only way the product builds a loudness meter, so these tests go
//! through it rather than through a hand-built `EbuR128`. Removing the `set_channel_map` call
//! inside it must make one of them fail (試験規律 §9.2 ⑤).
//!
//! Expected values come from ITU-R BS.1770-4 §2 Table 3 (Gi = 1.0 for the front channels, 1.41 for
//! left/right surround), not from this crate's own tables.

use super::*;
use crate::channel_layout::{ChannelLayout, LayoutId};

const SR: u32 = 48_000;
/// Long enough for Integrated to gate open; also several momentary windows.
const SECONDS: usize = 4;

/// A −20 dBFS 997 Hz sine placed in exactly one channel of an otherwise silent buffer.
fn tone_in_one_channel(channels: usize, channel: usize) -> Vec<f64> {
    let frames = SR as usize * SECONDS;
    let mut out = vec![0.0; frames * channels];
    for frame in 0..frames {
        let phase = std::f64::consts::TAU * 997.0 * frame as f64 / SR as f64;
        out[frame * channels + channel] = 0.1 * phase.sin();
    }
    out
}

/// Integrated loudness of that signal, through the shipping engine.
fn integrated_for_channel(layout: ChannelLayout, channel: usize) -> Option<f64> {
    let channels = layout.channel_count();
    let mut engine = MeasureEngine::new(SR, layout).expect("engine");
    let samples = tone_in_one_channel(channels, channel);
    for chunk in samples.chunks(SR as usize / 10 * channels) {
        let _ = engine.push(chunk);
    }
    engine.finalize().lufs_i
}

#[test]
fn a_ceiling_channel_is_measured_and_not_left_unused() {
    // ebur128's default map fixes every channel past index 5 to `Unused`, so at 7.1.4 the four
    // ceiling channels and the two rear surrounds would contribute nothing. Top Front Left sits at
    // index 6. If the constructor stops applying the map, this is silence.
    let layout = ChannelLayout::by_id(LayoutId::Surround7_1_4);
    let tfl = layout
        .index_of(crate::channel_layout::ChannelRole::TopFrontLeft)
        .expect("7.1.4 has Top Front Left");
    assert_eq!(tfl, 6, "the buffer order this test depends on");

    let left = integrated_for_channel(layout, 0).expect("Left must produce loudness");
    let ceiling = integrated_for_channel(layout, tfl)
        .expect("a ceiling channel must produce loudness, not be dropped as Unused");

    // Up045 carries no BS.1770-4 surround weighting, so it is the same loudness as Left.
    assert!(
        (ceiling - left).abs() < 0.01,
        "Top Front Left {ceiling} LUFS should equal Left {left} LUFS"
    );
}

#[test]
fn the_surround_pair_carries_the_standard_weighting_and_the_front_does_not() {
    // BS.1770-4 Table 3: Gi = 1.41 for left/right surround, 1.0 for L/R/C. 10·log10(1.41) LU.
    let layout = ChannelLayout::by_id(LayoutId::Surround7_1_4);
    let lss = layout
        .index_of(crate::channel_layout::ChannelRole::LeftSurroundSide)
        .expect("7.1.4 has Left Surround Side");

    let left = integrated_for_channel(layout, 0).expect("Left");
    let side = integrated_for_channel(layout, lss).expect("Left Surround Side");

    let expected = 10.0 * 1.41_f64.log10();
    assert!(
        (side - left - expected).abs() < 0.02,
        "surround weighting should be {expected:.4} LU, measured {:.4}",
        side - left
    );
}

#[test]
fn lfe_is_not_counted_as_loudness() {
    // BS.1770-4 §2: the LFE channel is excluded. An LFE-only programme has no loudness at all.
    let layout = ChannelLayout::by_id(LayoutId::Surround5_1);
    let lfe = layout
        .index_of(crate::channel_layout::ChannelRole::Lfe)
        .expect("5.1 has LFE");
    assert_eq!(
        integrated_for_channel(layout, lfe),
        None,
        "an LFE-only signal must not register as loudness"
    );
}

#[test]
fn mono_and_stereo_keep_the_loudness_the_default_map_gave_them() {
    // The explicit map must not move the values the shipping mono/stereo path already produces.
    // Both Centre (mono) and Left/Right weigh 1.0, so a tone at the same level reads the same.
    let mono = integrated_for_channel(ChannelLayout::mono(), 0).expect("mono");
    let stereo_left = integrated_for_channel(ChannelLayout::stereo(), 0).expect("stereo left");
    assert!(
        (mono - stereo_left).abs() < 0.01,
        "mono {mono} LUFS and one stereo channel {stereo_left} LUFS must agree"
    );
}
