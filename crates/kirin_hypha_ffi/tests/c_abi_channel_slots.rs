//! 16 スロットの位置が往復で保たれること、`channel_count` 以降が測定でないこと（P-2 gate）。
//!
//! 期待値は**リテラル**。offset は `include/kirin_hypha_meter_session_ffi.h` の宣言順から
//! 独立に算出した値で、`abi_contract` の数値からは作らない（試験規律 §9.1）。

use kirin_hypha_ffi::abi_contract::abi_contract;
use kirin_hypha_ffi::channel_abi::{channel_positions, widen, CHANNEL_ROLE_NONE_ABI};
use kirin_hypha_ffi::KirinMeterSession;
use kirin_measure::channel_layout::{ChannelLayout, ChannelRole, LayoutId, MAX_ABI_CHANNELS};

/// 各スロットに違う値を入れた構造体を、生バイト経由で読み直す。
fn round_trip(session: &KirinMeterSession) -> KirinMeterSession {
    let bytes: &[u8] = unsafe {
        std::slice::from_raw_parts(
            std::ptr::from_ref(session).cast::<u8>(),
            std::mem::size_of::<KirinMeterSession>(),
        )
    };
    let mut copy: KirinMeterSession = unsafe { std::mem::zeroed() };
    unsafe {
        std::slice::from_raw_parts_mut(
            std::ptr::from_mut(&mut copy).cast::<u8>(),
            std::mem::size_of::<KirinMeterSession>(),
        )
    }
    .copy_from_slice(bytes);
    copy
}

#[test]
fn every_one_of_the_sixteen_slots_keeps_its_own_value() {
    // 同じ値が 2 か所に出ると位置の入れ替わりが見えないので、すべて異なる値を入れる。
    let mut session: KirinMeterSession = unsafe { std::mem::zeroed() };
    for slot in 0..MAX_ABI_CHANNELS {
        session.sample_peak_dbfs[slot] = -(slot as f64) - 1.0;
        session.sample_peak_hold_dbfs[slot] = -(slot as f64) - 101.0;
        session.channel_true_peak_dbtp[slot] = -(slot as f64) - 201.0;
        session.channel_max_true_peak_dbtp[slot] = -(slot as f64) - 301.0;
        session.channel_vu_dbfs[slot] = -(slot as f64) - 401.0;
        session.channel_instant_true_peak_dbtp[slot] = -(slot as f64) - 501.0;
        session.clip_events[slot] = 1_000 + slot as u64;
        session.channel_clip_latched[slot] = slot as u8;
        session.channel_positions[slot] = (slot as u8) % 14;
    }
    session.channels = MAX_ABI_CHANNELS as u8;
    session.measurement_epoch = 0xFEED_FACE_CAFE_1234;
    session.layout_id = 5;

    let back = round_trip(&session);
    for slot in 0..MAX_ABI_CHANNELS {
        assert_eq!(back.sample_peak_dbfs[slot], -(slot as f64) - 1.0, "{slot}");
        assert_eq!(
            back.sample_peak_hold_dbfs[slot],
            -(slot as f64) - 101.0,
            "{slot}"
        );
        assert_eq!(
            back.channel_true_peak_dbtp[slot],
            -(slot as f64) - 201.0,
            "{slot}"
        );
        assert_eq!(
            back.channel_max_true_peak_dbtp[slot],
            -(slot as f64) - 301.0,
            "{slot}"
        );
        assert_eq!(back.channel_vu_dbfs[slot], -(slot as f64) - 401.0, "{slot}");
        assert_eq!(
            back.channel_instant_true_peak_dbtp[slot],
            -(slot as f64) - 501.0,
            "{slot}"
        );
        assert_eq!(back.clip_events[slot], 1_000 + slot as u64, "{slot}");
        assert_eq!(back.channel_clip_latched[slot], slot as u8, "{slot}");
        assert_eq!(back.channel_positions[slot], (slot as u8) % 14, "{slot}");
    }
    assert_eq!(back.measurement_epoch, 0xFEED_FACE_CAFE_1234);
    assert_eq!(back.layout_id, 5);
    assert_eq!(back.channels, MAX_ABI_CHANNELS as u8);
}

#[test]
fn the_declared_offsets_are_where_the_fields_actually_are() {
    // 宣言順から手で足した値。abi_contract の返り値からは作らない。
    // 88: uint64 x3 (24) + uint32 (28) + u8 + u8[3] (32) + double x7 (88).
    // 90: channels, balance_state のあと。
    // 112: 90 + clip_latched[16] + reserved[6] = 112、ここから double が始まる。
    let contract = abi_contract();
    assert_eq!(contract.meter_session_channels_offset, 88);
    assert_eq!(contract.meter_session_sample_peak_offset, 112);
    assert_eq!(contract.max_channels, 16);
    assert_eq!(contract.meter_session_size, 1_840);
}

#[test]
fn positions_name_the_measured_slots_and_nothing_else() {
    for (id, expected) in [
        (LayoutId::Mono, vec![ChannelRole::Centre]),
        (
            LayoutId::Stereo,
            vec![ChannelRole::Left, ChannelRole::Right],
        ),
    ] {
        let layout = ChannelLayout::by_id(id);
        let positions = channel_positions(layout);
        for (slot, role) in expected.iter().enumerate() {
            assert_eq!(
                positions[slot],
                role.to_abi(),
                "{} slot {slot}",
                id.as_str()
            );
        }
        assert!(
            positions[expected.len()..]
                .iter()
                .all(|code| *code == CHANNEL_ROLE_NONE_ABI),
            "{} named a slot it does not have",
            id.as_str()
        );
    }
}

#[test]
fn widening_fills_the_unmeasured_slots_the_same_way_every_time() {
    // 埋め値が呼ぶたびに変わると、同じ観測が「変化した」と判定される。
    let measured = [-1.0_f64, -2.0];
    let first = widen(&measured, f64::NAN);
    let second = widen(&measured, f64::NAN);
    assert_eq!(first[..2], measured);
    assert!(first[2..].iter().all(|v| v.is_nan()));
    assert!(second[2..].iter().all(|v| v.is_nan()));
    // 整数には NaN が無いので、0 と「測っていない」を分けるのは channel_count しかない。
    assert_eq!(widen(&[7_u64, 9], 0)[..2], [7, 9]);
    assert!(widen(&[7_u64, 9], 0)[2..].iter().all(|v| *v == 0));
}
