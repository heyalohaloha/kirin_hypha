//! Record は「どの配置を、どの map で測ったか」を自分で語れる（P-2 / 棚卸し §8）。
//!
//! 期待値はリテラルで書く。`ChannelLayout` から生成すると、map が壊れても試験が一緒に壊れる
//! （試験規律 §9.1）。

use kirin_measure::channel_layout::{ChannelLayout, LayoutId, MAPPING_REVISION};
use kirin_measure::engine::SessionSummary;
use kirin_measure::plugin_data::MeasurementLayout;

#[test]
fn stereo_records_the_two_roles_and_the_map_they_were_measured_as() {
    let recorded = MeasurementLayout::new(ChannelLayout::stereo());
    assert_eq!(recorded.layout, "stereo");
    assert_eq!(recorded.channel_positions, ["L", "R"]);
    assert_eq!(recorded.loudness_map, ["Left", "Right"]);
    assert_eq!(recorded.channel_count(), 2);
}

#[test]
fn mono_is_recorded_as_centre_not_as_left() {
    // ebur128 の default map は 1ch を `Left` にする。Hypha は `Centre` を渡している。
    // 重みは同じ 1.0 なので値では見分けられず、**記録だけが両者を区別できる。**
    let recorded = MeasurementLayout::new(ChannelLayout::mono());
    assert_eq!(recorded.layout, "mono");
    assert_eq!(recorded.channel_positions, ["C"]);
    assert_eq!(recorded.loudness_map, ["Center"]);
}

#[test]
fn seven_one_four_records_lfe_as_excluded_from_loudness_and_still_a_channel() {
    let recorded = MeasurementLayout::new(ChannelLayout::by_id(LayoutId::Surround7_1_4));
    assert_eq!(recorded.layout, "7.1.4");
    // バッファ順。JUCE の ChannelType 昇順なので、天井が rear surround より前に来る。
    assert_eq!(
        recorded.channel_positions,
        ["L", "R", "C", "LFE", "Lss", "Rss", "TFL", "TFR", "TRL", "TRR", "Lsr", "Rsr"]
    );
    assert_eq!(
        recorded.loudness_map,
        [
            "Left", "Right", "Center", "unused", "Mp090", "Mm090", "Up045", "Um045", "Up135",
            "Um135", "Mp135", "Mm135"
        ]
    );
    // LFE は loudness から外れるが、チャンネルとしては記録に残る。「消えた」ではない。
    assert_eq!(recorded.channel_positions[3], "LFE");
    assert_eq!(recorded.loudness_map[3], "unused");
    assert_eq!(recorded.channel_count(), 12);
}

#[test]
fn the_map_revision_is_recorded_so_two_weightings_are_not_confused() {
    // 同じ "7.1.4" でも重み付けの解釈が違えば別の測定である（決定 §4.1）。
    // 配置名だけではそれを言えないので、版を併記する。
    let recorded = MeasurementLayout::new(ChannelLayout::by_id(LayoutId::Surround7_1_4));
    assert_eq!(recorded.mapping_revision, 1);
    assert_eq!(recorded.mapping_revision, MAPPING_REVISION);
}

#[test]
fn a_summary_without_a_layout_records_nothing_rather_than_guessing_stereo() {
    // 旧経路の集計には配置が無い。そこで "stereo" を補うと、mono の記録が stereo を名乗る。
    let summary = SessionSummary {
        lufs_i: Some(-14.0),
        lra: Some(3.0),
        max_true_peak: Some(-1.0),
        layout: None,
    };
    assert!(summary.layout.map(MeasurementLayout::new).is_none());
}
