//! ヘッダとライブラリが同じ ABI で作られたかを、実行時に照合できるようにする。
//!
//! `static_assert` はヘッダと殻のコンパイル単位しか照合しない。staticlib は先にビルド済みなので、
//! 古い staticlib と新しいヘッダの組合せはコンパイルもリンクも通り、offset だけが全部ずれる。
//! そこで出てくるのは「値が出ているのに意味が違う」状態であり、D-13 が最優先で消すと決めたものなので、
//! 殻が最初の数値を受け取る前にここで弾けるようにする。
//!
//! C 側の正本は `include/kirin_hypha_abi_contract.h`。

use std::mem::{align_of, offset_of, size_of};

use kirin_measure::channel_layout::MAX_ABI_CHANNELS;

use crate::{
    KirinDelta, KirinMeasureResult, KirinMeterHistoryEntry, KirinMeterSession,
    KirinObservatoryFrame, KIRIN_MONO_SUM_BAND_COUNT, KIRIN_STEREO_FIELD_BINS,
};

/// ABI 全体の版。offset を動かす変更のたびに 1 つ上げる。
/// 4 = B-958（`KirinMeterSession` の入力チャンネル配列 `[2]` → `[MAX_ABI_CHANNELS]`）。
/// 5 = B-962（`KirinMeterHistoryEntry` の `clip_event_count` 同上 + `measurement_epoch`）。
/// 6 = B-981（Observatory comparison state/reason/generation/identity）。
pub const KIRIN_ABI_REVISION: u32 = 6;

/// `KirinObservatoryFrame.version`。フレーム 1 個ごとに載る版で、殻はこれが自分のヘッダの値と
/// 違うフレームを捨てる（`HyphaObservatoryFrame.cpp:42`）。ABI 版とは別に数える。
pub const KIRIN_OBSERVATORY_FRAME_VERSION: u32 = 6;

/// `include/kirin_hypha_abi_contract.h` の `KirinAbiContract`。
#[repr(C)]
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct KirinAbiContract {
    pub revision: u32,
    pub observatory_frame_version: u32,
    pub max_channels: u32,
    pub mono_sum_band_count: u32,
    pub stereo_field_bins: u32,
    pub reserved: u32,
    pub meter_session_size: u64,
    pub meter_session_align: u64,
    pub observatory_frame_size: u64,
    pub measure_result_size: u64,
    pub delta_size: u64,
    pub meter_history_entry_size: u64,
    pub meter_history_entry_epoch_offset: u64,
    pub meter_session_channels_offset: u64,
    pub meter_session_sample_peak_offset: u64,
    pub meter_session_channel_positions_offset: u64,
    pub meter_session_measurement_epoch_offset: u64,
}

/// このライブラリがビルドされた ABI。
pub fn abi_contract() -> KirinAbiContract {
    KirinAbiContract {
        revision: KIRIN_ABI_REVISION,
        observatory_frame_version: KIRIN_OBSERVATORY_FRAME_VERSION,
        max_channels: MAX_ABI_CHANNELS as u32,
        mono_sum_band_count: KIRIN_MONO_SUM_BAND_COUNT as u32,
        stereo_field_bins: KIRIN_STEREO_FIELD_BINS as u32,
        reserved: 0,
        meter_session_size: size_of::<KirinMeterSession>() as u64,
        meter_session_align: align_of::<KirinMeterSession>() as u64,
        observatory_frame_size: size_of::<KirinObservatoryFrame>() as u64,
        measure_result_size: size_of::<KirinMeasureResult>() as u64,
        delta_size: size_of::<KirinDelta>() as u64,
        meter_history_entry_size: size_of::<KirinMeterHistoryEntry>() as u64,
        meter_history_entry_epoch_offset: offset_of!(KirinMeterHistoryEntry, measurement_epoch)
            as u64,
        meter_session_channels_offset: offset_of!(KirinMeterSession, channels) as u64,
        meter_session_sample_peak_offset: offset_of!(KirinMeterSession, sample_peak_dbfs) as u64,
        meter_session_channel_positions_offset: offset_of!(KirinMeterSession, channel_positions)
            as u64,
        meter_session_measurement_epoch_offset: offset_of!(KirinMeterSession, measurement_epoch)
            as u64,
    }
}

/// このライブラリがビルドされた ABI を書き出す。
///
/// # Safety
/// `out` は null か、`KirinAbiContract` 1 個分の書き込み可能な領域であること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_abi_contract(out: *mut KirinAbiContract) {
    if out.is_null() {
        return;
    }
    unsafe { out.write(abi_contract()) };
}

#[cfg(test)]
#[path = "abi_contract_tests.rs"]
mod tests;
