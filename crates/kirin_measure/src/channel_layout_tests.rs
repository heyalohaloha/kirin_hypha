//! The layout contract's guards, each with the mistake it is there to catch.

use super::*;

fn roles_of(id: LayoutId) -> Vec<ChannelRole> {
    ChannelLayout::by_id(id).roles().to_vec()
}

#[test]
fn every_known_layout_recognises_itself() {
    for id in [
        LayoutId::Mono,
        LayoutId::Stereo,
        LayoutId::Surround5_0,
        LayoutId::Surround5_1,
        LayoutId::Surround7_1_4,
    ] {
        let layout = ChannelLayout::recognise(&roles_of(id)).expect("known layout");
        assert_eq!(layout.id(), id, "{} did not recognise itself", id.as_str());
    }
}

#[test]
fn an_unknown_role_set_is_refused_rather_than_approximated() {
    // 7.1 without the ceiling: a real layout, but not one Hypha has a map for. Guessing would
    // measure it as something else and report a number that looks right.
    let seven_one = [
        ChannelRole::Left,
        ChannelRole::Right,
        ChannelRole::Centre,
        ChannelRole::Lfe,
        ChannelRole::LeftSurroundSide,
        ChannelRole::RightSurroundSide,
        ChannelRole::LeftSurroundRear,
        ChannelRole::RightSurroundRear,
    ];
    assert_eq!(
        ChannelLayout::recognise(&seven_one),
        Err(LayoutError::UnknownLayout)
    );
}

#[test]
fn the_same_roles_in_the_wrong_order_are_named_as_such() {
    // JUCE puts the ceiling before the rear surrounds at 7.1.4. Writing them the way the layout is
    // usually spoken produces the right roles in the wrong buffer order, which needs a different
    // fix from an unknown layout, so it is reported differently.
    let mut spoken_order = roles_of(LayoutId::Surround7_1_4);
    spoken_order.swap(6, 10);
    spoken_order.swap(7, 11);
    assert_eq!(
        ChannelLayout::recognise(&spoken_order),
        Err(LayoutError::WrongOrder(LayoutId::Surround7_1_4))
    );
}

#[test]
fn a_duplicated_role_is_refused() {
    let doubled = [ChannelRole::Left, ChannelRole::Left];
    assert_eq!(
        ChannelLayout::recognise(&doubled),
        Err(LayoutError::DuplicateRole(ChannelRole::Left))
    );
    assert_eq!(ChannelLayout::recognise(&[]), Err(LayoutError::Empty));
}

#[test]
fn dropping_lfe_from_five_one_is_five_zero_and_that_is_the_right_answer() {
    // Identifying by role rather than by count is what makes this safe. 5.1 without its LFE holds
    // exactly 5.0's roles in exactly 5.0's order, so it is 5.0, and every remaining channel still
    // means what it meant. An index-based scheme would instead have shifted Ls and Rs down one and
    // measured them as something else.
    let mut without_lfe = roles_of(LayoutId::Surround5_1);
    without_lfe.retain(|role| *role != ChannelRole::Lfe);
    let layout = ChannelLayout::recognise(&without_lfe).expect("5.1 minus LFE is 5.0");
    assert_eq!(layout.id(), LayoutId::Surround5_0);
    assert_eq!(layout.index_of(ChannelRole::LeftSurround), Some(3));
    assert_eq!(
        ChannelLayout::by_id(LayoutId::Surround5_1).index_of(ChannelRole::LeftSurround),
        Some(4),
        "the same role sits at a different index, and the role is what is measured"
    );
}

#[test]
fn the_loudness_map_reaches_every_channel_except_lfe() {
    let layout = ChannelLayout::by_id(LayoutId::Surround7_1_4);
    let map = layout.loudness_map();
    assert_eq!(map.len(), 12);
    // The default map would have left indices 6 through 11 as Unused, dropping six of twelve.
    let unused = map.iter().filter(|c| **c == Channel::Unused).count();
    assert_eq!(unused, 1, "only LFE is outside the loudness sum");
    assert_eq!(map[3], Channel::Unused, "index 3 is LFE at 7.1.4");
    assert_eq!(layout.loudness_channel_count(), 11);
}

#[test]
fn surround_side_carries_the_weighted_position_and_rear_does_not() {
    // BS.1770 weights M+-090 but not M+-135. Naming the rear surrounds LeftSurround would apply
    // +1.5 dB to a channel the standard does not weight.
    assert_eq!(
        ChannelRole::LeftSurroundSide.loudness_channel(),
        Channel::Mp090
    );
    assert_eq!(
        ChannelRole::LeftSurroundRear.loudness_channel(),
        Channel::Mp135
    );
    assert_eq!(
        ChannelRole::LeftSurround.loudness_channel(),
        Channel::LeftSurround
    );
}

#[test]
fn five_one_maps_onto_what_the_ebu_assets_already_measure() {
    // The official 5ch and 6ch assets pass today on the crate's default map. The explicit map has
    // to agree with it exactly, or the regression that proves the table right cannot run.
    let map = ChannelLayout::by_id(LayoutId::Surround5_1).loudness_map();
    assert_eq!(
        map,
        vec![
            Channel::Left,
            Channel::Right,
            Channel::Center,
            Channel::Unused,
            Channel::LeftSurround,
            Channel::RightSurround,
        ]
    );
}

#[test]
fn pairs_come_from_the_mirror_rule_not_from_a_table() {
    let expect = |id: LayoutId, wanted: &[(ChannelRole, ChannelRole)]| {
        let pairs = ChannelLayout::by_id(id).pairs();
        let actual: Vec<(ChannelRole, ChannelRole)> =
            pairs.iter().map(|p| (p.left, p.right)).collect();
        assert_eq!(actual, wanted, "{}", id.as_str());
    };
    expect(LayoutId::Mono, &[]);
    expect(LayoutId::Stereo, &[(ChannelRole::Left, ChannelRole::Right)]);
    expect(
        LayoutId::Surround5_1,
        &[
            (ChannelRole::Left, ChannelRole::Right),
            (ChannelRole::LeftSurround, ChannelRole::RightSurround),
        ],
    );
    expect(
        LayoutId::Surround7_1_4,
        &[
            (ChannelRole::Left, ChannelRole::Right),
            (
                ChannelRole::LeftSurroundSide,
                ChannelRole::RightSurroundSide,
            ),
            (ChannelRole::TopFrontLeft, ChannelRole::TopFrontRight),
            (ChannelRole::TopRearLeft, ChannelRole::TopRearRight),
            (
                ChannelRole::LeftSurroundRear,
                ChannelRole::RightSurroundRear,
            ),
        ],
    );
}

#[test]
fn the_median_plane_and_lfe_have_no_mirror() {
    assert_eq!(ChannelRole::Centre.mirror(), None);
    assert_eq!(ChannelRole::Lfe.mirror(), None);
    assert!(!ChannelRole::Centre.is_left_of_pair());
    assert!(!ChannelRole::Lfe.is_left_of_pair());
}

#[test]
fn every_mirror_is_reciprocal_and_changes_side() {
    for role in ALL_ROLES {
        let Some(mirror) = role.mirror() else {
            continue;
        };
        assert_eq!(
            mirror.mirror(),
            Some(role),
            "{} is not reciprocal",
            role.as_str()
        );
        assert_ne!(
            role.is_left_of_pair(),
            mirror.is_left_of_pair(),
            "{} and its mirror are on the same side",
            role.as_str()
        );
    }
}

#[test]
fn roles_are_found_by_name_not_by_position() {
    // The same role sits at different indices in different layouts. Anything that remembers an
    // index rather than a role is remembering the wrong thing.
    let five_one = ChannelLayout::by_id(LayoutId::Surround5_1);
    let seven_one_four = ChannelLayout::by_id(LayoutId::Surround7_1_4);
    assert_eq!(five_one.index_of(ChannelRole::LeftSurround), Some(4));
    assert_eq!(seven_one_four.index_of(ChannelRole::LeftSurround), None);
    assert_eq!(
        seven_one_four.index_of(ChannelRole::LeftSurroundRear),
        Some(10)
    );
}

#[test]
fn pair_count_does_not_identify_a_layout_either() {
    // 7.1 and 5.1.2 are both eight channels and both yield three pairs; comparing counts would
    // call them the same. Only the roles differ, which is why comparison is by role.
    let five_one_four = ChannelLayout::by_id(LayoutId::Surround7_1_4);
    assert_eq!(five_one_four.pairs().len(), 5);
    assert_eq!(five_one_four.channel_count(), 12);
}

#[test]
fn every_role_survives_the_abi_and_no_two_share_a_code() {
    // The table and the discriminants are written out separately, so they can disagree. If they
    // do, a shell's Left arrives as Right and the channels are silently swapped.
    let mut seen: Vec<u8> = Vec::new();
    for role in ROLES_BY_ABI {
        let code = role.to_abi();
        assert_eq!(
            ChannelRole::from_abi(code),
            Some(role),
            "{} did not survive its own ABI code {code}",
            role.as_str()
        );
        assert!(!seen.contains(&code), "code {code} is used twice");
        seen.push(code);
    }
    assert_eq!(seen.len(), 14, "a role was added without an ABI code");
}

#[test]
fn a_code_no_role_claims_is_refused_not_clamped() {
    // A shell built against a newer header sends a code this build has no role for. Clamping it
    // to the nearest known role would measure that channel as something it is not.
    assert_eq!(ChannelRole::from_abi(14), None);
    assert_eq!(ChannelRole::from_abi(u8::MAX), None);
}
