//! `kirin_hypha_create` の受理と拒否（P-1 gate）。
//!
//! 役割コードは **リテラルで書く**。製品の `ChannelRole::to_abi()` から期待値を作ると、コード表が
//! ずれても試験が一緒にずれて気づけない（試験規律 §9.1）。正本は
//! `crates/kirin_hypha_ffi/include/kirin_hypha_channels.h` の `KirinChannelRole`。

use kirin_hypha_ffi::channel_abi::kirin_hypha_create;
use kirin_hypha_ffi::kirin_hypha_destroy;

const SR: u32 = 48_000;

const CENTRE: u8 = 0;
const LEFT: u8 = 1;
const RIGHT: u8 = 2;
const LFE: u8 = 3;
const LS: u8 = 4;
const RS: u8 = 5;

/// 受理されたら handle を解放し、受理されたかどうかを返す。
fn accepts(roles: &[u8]) -> bool {
    let handle = unsafe { kirin_hypha_create(SR, roles.as_ptr(), roles.len() as u32) };
    if handle.is_null() {
        return false;
    }
    unsafe { kirin_hypha_destroy(handle) };
    true
}

#[test]
fn mono_and_stereo_are_accepted() {
    assert!(accepts(&[CENTRE]), "mono");
    assert!(accepts(&[LEFT, RIGHT]), "stereo");
}

#[test]
fn nothing_at_all_is_refused() {
    assert!(
        unsafe { kirin_hypha_create(SR, std::ptr::null(), 0) }.is_null(),
        "null pointer"
    );
    assert!(
        unsafe { kirin_hypha_create(SR, [LEFT, RIGHT].as_ptr(), 0) }.is_null(),
        "zero count"
    );
}

/// `count > MAX_ABI_CHANNELS` は slice を作る前に落ちる。**この境界より下は落ちない。**
///
/// `layout_from_abi` の長さ検査は `MAX_ABI_CHANNELS`（16）だけを見る。したがって
/// count が 1..=16 で配列がそれより短い場合、`slice::from_raw_parts` は配列の外を読む。
/// これは検出できない呼び出し側の誤りであり、`# Safety` の契約（`count` 要素以上の
/// 有効な配列）そのものである。**ここで守られているのは 16 を超える count だけである。**
/// 出荷経路は `ChannelRoles.h:83` が `roles.data()` と `roles.size()` を同じ vector から
/// 渡すので、count と配列長が食い違わない。
#[test]
fn only_a_count_past_the_abi_maximum_is_refused_before_the_slice_is_formed() {
    // 16 を超える count。殻の計算違いが巨大な確保にならないこと。
    assert!(
        unsafe { kirin_hypha_create(SR, [LEFT, RIGHT].as_ptr(), 4096) }.is_null(),
        "an absurd count must be refused before the slice is formed"
    );
    // 境界そのもの。17 は配列を読まずに落ちる。
    assert!(
        unsafe { kirin_hypha_create(SR, [LEFT, RIGHT].as_ptr(), 17) }.is_null(),
        "one past MAX_ABI_CHANNELS must be refused before the slice is formed"
    );
    // 16 までは長さでは落ちない。ここは実在する 16 要素を渡して、拒否の理由が長さではなく
    // レイアウト照合であることを示す（役割は 14 種しかないので 16 は必ず未知コードを含む）。
    let sixteen: [u8; 16] = std::array::from_fn(|slot| slot as u8);
    assert!(
        unsafe { kirin_hypha_create(SR, sixteen.as_ptr(), 16) }.is_null(),
        "16 is inside the ABI capacity; it is refused by the role table and layout, not by length"
    );
}

#[test]
fn a_code_this_build_has_no_role_for_is_refused() {
    // 新しい殻 + 古い staticlib。未知コードを近い役割へ丸める / 巻き戻すと、そのチャンネルは
    // 別物として無言で計測される。
    //
    // 単独の未知コードが要点: 巻き戻し（例 `code % 14`）は 14 を CENTRE にするので、丸めが
    // 入った瞬間ここが mono として**受理**される。2ch 側は丸めても既知レイアウトにならないため
    // レイアウト照合の側で落ちる。コード表そのものの拒否は
    // `channel_layout` の `a_code_no_role_claims_is_refused_not_clamped` が受け持つ。
    assert!(!accepts(&[14]), "one past the last role, on its own");
    assert!(!accepts(&[u8::MAX]), "255, on its own");
    assert!(!accepts(&[LEFT, 14]), "one past the last role");
    assert!(!accepts(&[LEFT, u8::MAX]), "255");
}

#[test]
fn a_repeated_role_is_refused() {
    assert!(!accepts(&[LEFT, LEFT]), "Left twice");
    assert!(!accepts(&[LEFT, RIGHT, RIGHT]), "Right twice");
}

#[test]
fn the_same_roles_in_the_wrong_order_are_refused() {
    // 順序はバッファ順そのもの。入れ替えを許すと L と R が逆のまま「stereo」として通る。
    assert!(!accepts(&[RIGHT, LEFT]), "Right before Left");
}

#[test]
fn a_declared_count_shorter_than_the_layout_is_refused() {
    // count が 1 なら読まれるのは [LEFT] だけで、それは既知のどのレイアウトでもない。
    assert!(
        unsafe { kirin_hypha_create(SR, [LEFT, RIGHT].as_ptr(), 1) }.is_null(),
        "a stereo buffer declared as one channel"
    );
}

#[test]
fn a_layout_missing_a_non_lfe_channel_is_refused() {
    // 5.1 から Centre を抜いたもの。既知のどのレイアウトでもないので推測しない。
    assert!(!accepts(&[LEFT, RIGHT, LFE, LS, RS]), "5.1 without Centre");
}

#[test]
fn surround_is_recognised_but_not_yet_accepted() {
    // `ChannelLayout` は 5.0 / 5.1 / 7.1.4 を認識する。P-1 の門はそれとは別で、engine が
    // Nch を測れるようになるまで閉じている。門が先に開くと、認識できるだけの配置が
    // ステレオ用の経路へ入る。
    assert!(!accepts(&[LEFT, RIGHT, CENTRE, LS, RS]), "5.0");
    assert!(!accepts(&[LEFT, RIGHT, CENTRE, LFE, LS, RS]), "5.1");
}
