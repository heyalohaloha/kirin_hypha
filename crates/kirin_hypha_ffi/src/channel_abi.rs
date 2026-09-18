//! `kirin_hypha_create` と、その入口が受け取るチャンネル役割の C ABI。
//!
//! レイアウトはチャンネル数では決まらない（8ch は 7.1 と 5.1.2 のどちらでもある / D-4）。
//! そのため殻は数ではなく役割コード列を渡し、認識はここではなく `kirin_measure::channel_layout`
//! が行う。このモジュールは列を Rust の型へ戻し、受理範囲の門を一箇所に置くだけである。
//!
//! コード表の正本は `ChannelRole` の discriminant。C 側は `include/kirin_hypha_channels.h`。

use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::channel_layout::{ChannelRole, LayoutId};

/// この境界が生む型。殻とテストは kirin_measure を直接参照せずここから取る。
pub use kirin_measure::channel_layout::ChannelLayout;

use crate::KirinHyphaEngine;

/// ランタイムを生成して不透明ポインタを返す。拒否時と panic 時は null を返す。
///
/// `channel_roles` は `KirinChannelRole` のコード列（バッファ順 / `include/kirin_hypha_channels.h`）。
/// チャンネル数ではなくレイアウトを受け取るのは、8ch が 7.1 か 5.1.2 かを数が決めないためである
/// （D-4）。認識できない役割・重複・順序違い・未知のレイアウトはすべて null で拒否し、
/// 「それらしい」map で計測しない。
///
/// P-1 時点の受理範囲は mono / stereo に留める。`ChannelLayout` は 5.0 / 5.1 / 7.1.4 も認識するが、
/// engine 側の Nch 化が済むまでここで門を閉じる。呼び出し側の殻は `isBusesLayoutSupported` で
/// そもそも mono / stereo 以外を交渉しない。
///
/// # Safety
/// `channel_roles` は `channel_count` 要素以上の有効な配列か null であること。
/// 返り値は `kirin_hypha_destroy` でのみ解放すること。
#[no_mangle]
pub unsafe extern "C" fn kirin_hypha_create(
    sample_rate: u32,
    channel_roles: *const u8,
    channel_count: u32,
) -> *mut KirinHyphaEngine {
    // panic を C ABI 境界で止める。panic 時は null を返す（UB 回避）。論理は変えない。
    catch_unwind(AssertUnwindSafe(|| {
        let Some(layout) = layout_from_abi(channel_roles, channel_count) else {
            return std::ptr::null_mut();
        };
        Box::into_raw(Box::new(KirinHyphaEngine::new(sample_rate, layout)))
    }))
    .unwrap_or(std::ptr::null_mut())
}

/// 殻が渡した役割コード列を、受理可能なレイアウトへ解決する。拒否は `None`。
///
/// # Safety
/// `roles` は `count` 要素以上の有効な配列か null であること。
unsafe fn layout_from_abi(roles: *const u8, count: u32) -> Option<ChannelLayout> {
    if roles.is_null() || count == 0 || count as usize > MAX_ABI_CHANNELS {
        return None;
    }
    let codes = unsafe { std::slice::from_raw_parts(roles, count as usize) };
    let roles: Option<Vec<ChannelRole>> = codes
        .iter()
        .map(|code| ChannelRole::from_abi(*code))
        .collect();
    let layout = ChannelLayout::recognise(&roles?).ok()?;
    // P-1 の門。engine が Nch を測れるようになるまで、認識できることと受理することを分ける。
    matches!(layout.id(), LayoutId::Mono | LayoutId::Stereo).then_some(layout)
}

/// 役割コード列として受け取る最大長。`ChannelLayout` が認識する最大配置（7.1.4 = 12）を
/// 上回る値を弾き、殻の計算違いがそのまま巨大な確保にならないようにする。
const MAX_ABI_CHANNELS: usize = 16;

/// あるレイアウトの役割コード列（バッファ順）。殻は自分のチャンネル集合から同じ列を組む。
/// **コード表の検証には使わない**（製品から期待値を作ることになるため / 試験規律 §9.1）。
pub fn abi_codes(layout: ChannelLayout) -> Vec<u8> {
    layout.roles().iter().map(|role| role.to_abi()).collect()
}
