//! `kirin_hypha_create` と、その入口が受け取るチャンネル役割の C ABI。
//!
//! レイアウトはチャンネル数では決まらない（8ch は 7.1 と 5.1.2 のどちらでもある / D-4）。
//! そのため殻は数ではなく役割コード列を渡し、認識はここではなく `kirin_measure::channel_layout`
//! が行う。このモジュールは列を Rust の型へ戻し、受理範囲の門を一箇所に置くだけである。
//!
//! コード表の正本は `ChannelRole` の discriminant。C 側は `include/kirin_hypha_channels.h`。

use std::panic::{catch_unwind, AssertUnwindSafe};

use kirin_measure::channel_layout::{ChannelRole, LayoutId, MAX_ABI_CHANNELS};

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
/// 受理範囲は mono / stereo / exact 5.1。`ChannelLayout` は 5.0 / 7.1.4 も認識するが、製品表示と
/// 検証を閉じた配置だけをここで開く。数が6というだけのunknown配置を5.1として受理しない。
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
    matches!(
        layout.id(),
        LayoutId::Mono | LayoutId::Stereo | LayoutId::Surround5_1
    )
    .then_some(layout)
}

/// あるレイアウトの役割コード列（バッファ順）。殻は自分のチャンネル集合から同じ列を組む。
/// **コード表の検証には使わない**（製品から期待値を作ることになるため / 試験規律 §9.1）。
pub fn abi_codes(layout: ChannelLayout) -> Vec<u8> {
    layout.roles().iter().map(|role| role.to_abi()).collect()
}

/// スロットに役割が無いことを表す ABI 値（`KIRIN_CHANNEL_ROLE_NONE`）。
pub const CHANNEL_ROLE_NONE_ABI: u8 = 255;

/// `channel_positions[]`。`layout.channel_count()` 未満は役割コード、以降は `NONE`。
pub fn channel_positions(layout: ChannelLayout) -> [u8; MAX_ABI_CHANNELS] {
    let roles = layout.roles();
    std::array::from_fn(|slot| {
        roles
            .get(slot)
            .map_or(CHANNEL_ROLE_NONE_ABI, |role| role.to_abi())
    })
}

/// 測定済みスロットを前から詰め、残りを「測っていない」で埋める。
///
/// 埋め値は毎回同じものにする。同じ観測を 2 回読んだときに、使っていないスロットの中身が
/// 違うせいで「変化した」と判定されるのを防ぐ（等値判定は `channels` を見ない）。
pub fn widen<T: Copy>(measured: &[T], unmeasured: T) -> [T; MAX_ABI_CHANNELS] {
    std::array::from_fn(|slot| measured.get(slot).copied().unwrap_or(unmeasured))
}
