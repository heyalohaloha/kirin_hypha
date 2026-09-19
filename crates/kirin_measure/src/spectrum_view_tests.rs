//! selector の値空間。期待値はリテラルで書く（試験規律 §9.1）。

use super::*;

#[test]
fn the_three_existing_codes_never_move() {
    // 旧殻が 0/1/2 を送ってくる。振り直したら、そのまま別の view が選ばれる。
    assert_eq!(SpectrumView::Lr.to_abi(), 0);
    assert_eq!(SpectrumView::Mid.to_abi(), 1);
    assert_eq!(SpectrumView::Side.to_abi(), 2);
    assert_eq!(SpectrumView::from_abi(0), Some(SpectrumView::Lr));
    assert_eq!(SpectrumView::from_abi(1), Some(SpectrumView::Mid));
    assert_eq!(SpectrumView::from_abi(2), Some(SpectrumView::Side));
}

#[test]
fn every_role_survives_the_selector_abi() {
    for role in [
        ChannelRole::Centre,
        ChannelRole::Left,
        ChannelRole::Right,
        ChannelRole::Lfe,
        ChannelRole::LeftSurroundSide,
        ChannelRole::TopRearRight,
    ] {
        let view = SpectrumView::Channel(role);
        assert_eq!(SpectrumView::from_abi(view.to_abi()), Some(view));
    }
    // Centre は役割コード 0。基点を足さないと LR と衝突する。
    assert_eq!(SpectrumView::Channel(ChannelRole::Centre).to_abi(), 16);
    assert_ne!(SpectrumView::Channel(ChannelRole::Centre).to_abi(), 0);
}

#[test]
fn an_unknown_code_is_refused_not_rounded() {
    // 3..15 は空き。新しい殻が使い始めたコードを、古いビルドが近い view として解釈しない。
    for code in [3_u8, 15, 30, 200, u8::MAX] {
        assert_eq!(SpectrumView::from_abi(code), None, "code {code}");
    }
}

#[test]
fn mid_and_side_stay_stereo_only() {
    // 5.1 の L と R から 0.5·(L ± R) は作れるが、それは前方ペアの M/S であって作品の M/S では
    // ない。同じラベルのまま意味を変えない（D-6 / 契約表 §6）。
    let stereo = ChannelLayout::stereo();
    assert!(SpectrumView::Mid.is_available_in(stereo));
    assert!(SpectrumView::Side.is_available_in(stereo));

    for id in [
        LayoutId::Mono,
        LayoutId::Surround5_1,
        LayoutId::Surround7_1_4,
    ] {
        let layout = ChannelLayout::by_id(id);
        assert!(
            !SpectrumView::Side.is_available_in(layout),
            "{} must not offer SIDE",
            id.as_str()
        );
    }
    assert!(!SpectrumView::Mid.is_available_in(ChannelLayout::by_id(LayoutId::Surround5_1)));
    // mono の現行挙動は変えない: LR と MID を受け、SIDE を拒否する。
    assert!(SpectrumView::Lr.is_available_in(ChannelLayout::mono()));
    assert!(SpectrumView::Mid.is_available_in(ChannelLayout::mono()));
    assert!(!SpectrumView::Side.is_available_in(ChannelLayout::mono()));
}

#[test]
fn a_channel_view_is_offered_only_where_that_role_exists() {
    let stereo = ChannelLayout::stereo();
    assert!(SpectrumView::Channel(ChannelRole::Left).is_available_in(stereo));
    assert!(!SpectrumView::Channel(ChannelRole::Centre).is_available_in(stereo));
    assert!(!SpectrumView::Channel(ChannelRole::Lfe).is_available_in(stereo));

    let surround = ChannelLayout::by_id(LayoutId::Surround5_1);
    assert!(SpectrumView::Channel(ChannelRole::Centre).is_available_in(surround));
    assert!(SpectrumView::Channel(ChannelRole::Lfe).is_available_in(surround));
    // 5.1 は side surround を持たない。7.1.4 の選択が 5.1 でそのまま有効にならない。
    assert!(!SpectrumView::Channel(ChannelRole::LeftSurroundSide).is_available_in(surround));
}

#[test]
fn the_offered_set_is_the_layout_itself_plus_what_derives_from_it() {
    let names: Vec<&str> = SpectrumView::available_in(ChannelLayout::stereo())
        .into_iter()
        .map(SpectrumView::as_str)
        .collect();
    assert_eq!(names, ["LR", "MID", "SIDE", "L", "R"]);

    let names: Vec<&str> = SpectrumView::available_in(ChannelLayout::by_id(LayoutId::Surround5_1))
        .into_iter()
        .map(SpectrumView::as_str)
        .collect();
    assert_eq!(names, ["L", "R", "C", "LFE", "Ls", "Rs"]);
}

#[test]
fn every_layout_has_a_default_and_it_is_one_of_the_offered_views() {
    for id in [
        LayoutId::Mono,
        LayoutId::Stereo,
        LayoutId::Surround5_0,
        LayoutId::Surround5_1,
        LayoutId::Surround7_1_4,
    ] {
        let layout = ChannelLayout::by_id(id);
        let default = SpectrumView::default_for(layout);
        assert!(
            default.is_available_in(layout),
            "{} defaults to a view it does not offer",
            id.as_str()
        );
        assert!(SpectrumView::available_in(layout).contains(&default));
    }
    // 既定は現行のまま。mono / stereo の挙動を変えない。
    assert_eq!(
        SpectrumView::default_for(ChannelLayout::mono()),
        SpectrumView::Lr
    );
    assert_eq!(
        SpectrumView::default_for(ChannelLayout::stereo()),
        SpectrumView::Lr
    );
    // LR を持たない layout はバッファ先頭の役割。5.1 の先頭は L。
    assert_eq!(
        SpectrumView::default_for(ChannelLayout::by_id(LayoutId::Surround5_1)),
        SpectrumView::Channel(ChannelRole::Left)
    );
}

#[test]
fn a_layout_change_either_keeps_the_role_or_loses_it_and_both_are_visible() {
    // 契約表 §11.3.1 の核心。7.1.4 → 5.1.4 を index で指すと「index 7 は有効なまま、指す
    // チャンネルだけが変わる」。役割で指すと、残るか消えるかのどちらかになる。
    //
    // 7.1.4 の index 7 は TFR、5.1 の index 7 は存在しない。index が同じでも意味は同じでない。
    let wide = ChannelLayout::by_id(LayoutId::Surround7_1_4);
    let narrow = ChannelLayout::by_id(LayoutId::Surround5_1);

    // 残る役割: 同じコードが両方で有効。同じチャンネルなので履歴は連続してよい。
    let centre = SpectrumView::Channel(ChannelRole::Centre);
    assert!(centre.is_available_in(wide) && centre.is_available_in(narrow));

    // 消える役割: 5.1 に side surround は無い。選択は無効になり、検出できる。
    let side = SpectrumView::Channel(ChannelRole::LeftSurroundSide);
    assert!(side.is_available_in(wide));
    assert!(!side.is_available_in(narrow));

    // index なら見逃していたこと: 7.1.4 の index 7 と 5.1 の index 5 は別の役割である。
    assert_eq!(wide.roles()[7], ChannelRole::TopFrontRight);
    assert_eq!(narrow.roles()[5], ChannelRole::RightSurround);
    assert_ne!(wide.roles()[5], narrow.roles()[5]);
}
