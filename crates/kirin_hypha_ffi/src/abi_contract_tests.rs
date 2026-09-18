//! ABI 契約の実測値。
//!
//! 期待値は**リテラルで書く**。`size_of` から期待値を作ると「今ビルドしたものは今ビルドしたものと
//! 同じ」しか言わず、offset が動いたことを誰も知らせない（試験規律 §9.1）。
//! これらの数値は `include/kirin_hypha_abi_contract.h` / `HyphaObservationEquality.h` と同じである。

use super::*;

/// 隔離された参照値。製品の定数を読まない。
const EXPECTED: KirinAbiContract = KirinAbiContract {
    revision: 5,
    observatory_frame_version: 5,
    max_channels: 16,
    mono_sum_band_count: 32,
    stereo_field_bins: 625,
    reserved: 0,
    meter_session_size: 1840,
    meter_session_align: 8,
    observatory_frame_size: 2080,
    measure_result_size: 416,
    delta_size: 224,
    meter_history_entry_size: 248,
    meter_history_entry_epoch_offset: 0,
    meter_session_channels_offset: 88,
    meter_session_sample_peak_offset: 112,
    meter_session_channel_positions_offset: 1808,
    meter_session_measurement_epoch_offset: 1832,
};

#[test]
fn the_library_reports_the_abi_it_was_built_with() {
    assert_eq!(abi_contract(), EXPECTED);
}

#[test]
fn the_c_entry_point_writes_the_same_contract_and_tolerates_null() {
    let mut out = KirinAbiContract {
        revision: 0,
        observatory_frame_version: 0,
        max_channels: 0,
        mono_sum_band_count: 0,
        stereo_field_bins: 0,
        reserved: 0,
        meter_session_size: 0,
        meter_session_align: 0,
        observatory_frame_size: 0,
        measure_result_size: 0,
        delta_size: 0,
        meter_history_entry_size: 0,
        meter_history_entry_epoch_offset: 0,
        meter_session_channels_offset: 0,
        meter_session_sample_peak_offset: 0,
        meter_session_channel_positions_offset: 0,
        meter_session_measurement_epoch_offset: 0,
    };
    unsafe { kirin_hypha_abi_contract(&mut out) };
    assert_eq!(out, EXPECTED);
    // 殻が渡し損ねても落ちない。契約が書かれないので照合は失敗し、engine は作られない。
    unsafe { kirin_hypha_abi_contract(std::ptr::null_mut()) };
}

#[test]
fn a_revision_change_and_an_offset_change_cannot_happen_separately() {
    // offset を動かしたのに revision を据え置くと、古い staticlib が新しい殻に「一致」と答える。
    // 上の EXPECTED がリテラルなので、片方だけ変えるとどちらかの assert が落ちる。
    assert_eq!(KIRIN_ABI_REVISION, EXPECTED.revision);
    assert_eq!(
        KIRIN_OBSERVATORY_FRAME_VERSION,
        EXPECTED.observatory_frame_version
    );
}

#[test]
fn the_meter_session_fields_sit_where_the_header_says() {
    assert_eq!(std::mem::size_of::<crate::KirinMeterHistoryRange>(), 24);
    // B-962: clip_event_count [2] -> [16] and measurement_epoch first, so a zeroed struct cannot
    // read as span 0 of a real measurement.
    assert_eq!(std::mem::size_of::<KirinMeterHistoryEntry>(), 248);
    // 7 x 8 = 56 (epoch, generation, run_id, first/last observed, first/last timeline),
    // then u16 + u8 + u8 = 60. No padding before the u32 array.
    assert_eq!(
        std::mem::offset_of!(KirinMeterHistoryEntry, clip_event_count),
        60
    );
    // B-958 widened the input-channel arrays from [2] to [16], so every offset from
    // `channel_clip_latched` onwards moved. The same numbers are asserted in C++
    // (HyphaObservationEquality.h) and handed to callers by `kirin_hypha_abi_contract`.
    assert_eq!(std::mem::size_of::<KirinMeterSession>(), 1840);
    assert_eq!(std::mem::align_of::<KirinMeterSession>(), 8);
    assert_eq!(std::mem::offset_of!(KirinMeterSession, channels), 88);
    assert_eq!(
        std::mem::offset_of!(KirinMeterSession, channel_clip_latched),
        90
    );
    assert_eq!(
        std::mem::offset_of!(KirinMeterSession, sample_peak_dbfs),
        112
    );
    assert_eq!(std::mem::offset_of!(KirinMeterSession, clip_events), 624);
    assert_eq!(std::mem::offset_of!(KirinMeterSession, field_density), 776);
    assert_eq!(std::mem::offset_of!(KirinMeterSession, max_lufs_m), 1408);
    assert_eq!(
        std::mem::offset_of!(KirinMeterSession, channel_vu_dbfs),
        1416
    );
    assert_eq!(
        std::mem::offset_of!(KirinMeterSession, channel_instant_true_peak_dbtp),
        1544
    );
    assert_eq!(
        std::mem::offset_of!(KirinMeterSession, mono_sum_band_count),
        1672
    );
    assert_eq!(
        std::mem::offset_of!(KirinMeterSession, mono_sum_approximate_below_hz),
        1676
    );
    assert_eq!(std::mem::offset_of!(KirinMeterSession, mono_sum_db), 1680);
    assert_eq!(
        std::mem::offset_of!(KirinMeterSession, channel_positions),
        1808
    );
    assert_eq!(std::mem::offset_of!(KirinMeterSession, layout_id), 1824);
    assert_eq!(
        std::mem::offset_of!(KirinMeterSession, measurement_epoch),
        1832
    );
    assert_eq!(KIRIN_MONO_SUM_BAND_COUNT, 32);
    assert_eq!(MAX_ABI_CHANNELS, 16);
}
