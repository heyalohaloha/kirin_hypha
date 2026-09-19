//! `DeltaResult` の C ABI への写し。
//!
//! B-976 で配置不一致 / 配置不明の 2 コードを足したときに `lib.rs` から分けた（行数規律）。
//! 殻は `== KIRIN_DELTA_MODE_ACTIVE` で表示を決めるので、新コードは「Δ を出さない」として
//! 正しく扱われる。理由の提示は Gate D（R-28 transport）。

use kirin_measure::{DeltaMode, DeltaResult};

use crate::{opt_arr20, opt_f64, KirinDelta};

pub(crate) fn delta_mode_to_abi(mode: &DeltaMode) -> u8 {
    match mode {
        DeltaMode::Active => 0,
        DeltaMode::Stale => 1,
        DeltaMode::NoPre => 2,
        DeltaMode::Bypassed => 3,
        DeltaMode::PreInactive => 4,
        // B-976: 殻は `== ACTIVE` で表示を決め、それ以外は Δ を出さない。したがって新コードは
        // 「Δ を出さない」として正しく扱われる。理由の提示は Gate D（R-28 transport）。
        DeltaMode::LayoutMismatch => 5,
        DeltaMode::LayoutUnknown => 6,
    }
}

pub(crate) fn to_c_delta(d: &DeltaResult) -> KirinDelta {
    KirinDelta {
        mode: delta_mode_to_abi(&d.mode),
        lufs: opt_f64(d.lufs),
        true_peak: opt_f64(d.tp),
        crest: opt_f64(d.crest),
        psr: opt_f64(d.psr),
        n_prime_total: opt_f64(d.n_prime_total),
        sharpness: opt_f64(d.sharpness),
        lufs_s: opt_f64(d.lufs_s),
        psb_bark: opt_arr20(d.psb_bark),
    }
}
