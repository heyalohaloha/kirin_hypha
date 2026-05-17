//!
//! 単体テストで消費可能な形式で固定する。G-79-01 判断 DB:
//!   `34c59d4f-6faf-81ef-9477-e69af30721dc` (max_relative = 1e-3 確定)
//!
//! # スコープ (Phase 1 で落とし込む範囲)
//! - **tolerance 定数を単一ソースに集約** — 将来の MoSQITo golden 比較テストが
//!   同じ数値を参照するための唯一の正本
//! - **自己整合性テスト** — silence 入力で sharpness=0 / 同一入力二回で
//!   ビット一致を検証 (determinism は「計算の不確定性なし」の直接証拠)
//! - **MoSQITo golden 比較テスト** は現状 `#[ignore]` で雛形のみ。
//!   Python MoSQITo で生成した fixture (期待値 json) を
//!   `tests/fixtures/phase_d/` に配置後、`cargo test --ignored` で活性化する。
//!   fixture 生成自体は別タスク (運用側 or Daisuke 手動) 担当。
//!
//! # §2.5 B で "bit-identical 済" と認定された 5 モジュール
//! filter_bank / core_loudness / nonlinear_decay / temporal_weighting /
//! calc_slopes(N total). 本ファイルの tolerance 定数はこれら全モジュールで
//! 共通参照される想定。
//!
//! Runner: `cargo test --test phase_d_tolerances_test`
//! Ignored 起動 (fixture 配備後): `cargo test --test phase_d_tolerances_test -- --ignored`

use kirin_measure::phase_d::sharpness;
use kirin_measure::phase_d::calc_slopes;

// ── G-79-01 tolerance 定数 (Daisuke approved 2026-04-24) ─────────────────────

/// §2.5 B sharpness.rs: D-05 Gate 値。absolute diff 許容値 (acum)。
/// `assert_abs_diff_eq!(rust, mosqito, epsilon = SHARPNESS_EPSILON_ACUM)` で使用。
pub const SHARPNESS_EPSILON_ACUM: f64 = 0.05;

/// §2.5 B calc_slopes N_spec: relative diff 許容値 (0.1%)。G-79-01 確定値。
/// `assert_relative_eq!(rust, mosqito, max_relative = N_SPEC_MAX_RELATIVE)` で使用。
pub const N_SPEC_MAX_RELATIVE: f64 = 1e-3;

/// §2.5 B bit-identical モジュール共通: `round8` 精度 (1e-8 以下) と同位。
/// filter_bank / core_loudness / nonlinear_decay / temporal_weighting /
/// calc_slopes(N total) に適用。
pub const BIT_IDENTICAL_EPSILON: f64 = 1e-8;

// ── 定数値のガード (G-79-01 の数値リグレッション) ───────────────────────────

#[test]
fn tolerance_constants_match_g_79_01() {
    // G-79-01 (Daisuke approved 2026-04-24) の数値が誤って改変されていないことを
    // コンパイル時ではなく実行時にも再確認する sentinel。コミット時にこの値が
    // ずれたら Daisuke 裁定の再発行が必要。
    assert_eq!(SHARPNESS_EPSILON_ACUM, 0.05);
    assert_eq!(N_SPEC_MAX_RELATIVE, 1e-3);
    assert_eq!(BIT_IDENTICAL_EPSILON, 1e-8);
}

// ── sharpness 自己整合性 ─────────────────────────────────────────────────────

const N_SPEC_BINS: usize = 240;

fn zero_spec_frame() -> [f64; N_SPEC_BINS] {
    [0.0; N_SPEC_BINS]
}

#[test]
fn sharpness_silence_is_zero() {
    // §2.3 "完全無音入力": N'(t,z)=0 / N(t)=0 → sharpness=0
    // (sharpness.rs L24: `if n < 0.1` 分岐で 0 を返す設計)
    let n_specific = vec![zero_spec_frame(); 10];
    let n_total = vec![0.0_f64; 10];
    let out = sharpness::compute(&n_specific, &n_total);
    assert_eq!(out.len(), 10);
    for (i, &s) in out.iter().enumerate() {
        approx::assert_abs_diff_eq!(s, 0.0, epsilon = BIT_IDENTICAL_EPSILON);
        // 念のため明示的な等価 (IEEE 754 上で 0.0 を想定)
        assert!(s == 0.0, "frame {i} sharpness must be exact 0.0, got {s}");
    }
}

#[test]
fn sharpness_determinism() {
    // 同一入力二回実行 → ビット同一 (浮動小数非決定性なし)。
    // assert_abs_diff_eq! の epsilon は 0 でも通るはずだが G-79-01 の
    // `BIT_IDENTICAL_EPSILON` を使って「将来 unstable な最適化が入っても
    // MoSQITo 比較許容範囲には収まる」ことを自動記録する。
    let n_specific: Vec<[f64; N_SPEC_BINS]> = (0..32)
        .map(|t| {
            let mut f = [0.0_f64; N_SPEC_BINS];
            for (i, v) in f.iter_mut().enumerate() {
                // 疑似信号: low-pass biased distribution
                *v = 0.1 + ((i as f64) * 0.001) + ((t as f64) * 0.002);
            }
            f
        })
        .collect();
    let n_total: Vec<f64> = (0..32).map(|t| 1.0 + (t as f64) * 0.01).collect();

    let a = sharpness::compute(&n_specific, &n_total);
    let b = sharpness::compute(&n_specific, &n_total);
    assert_eq!(a.len(), b.len());
    for (i, (x, y)) in a.iter().zip(b.iter()).enumerate() {
        approx::assert_abs_diff_eq!(x, y, epsilon = BIT_IDENTICAL_EPSILON);
        // 真の決定性は exact eq で保証 (epsilon は G-79-01 文書化目的)
        assert!(x == y, "non-determinism at frame {i}: {x} vs {y}");
    }
}

#[test]
fn sharpness_output_is_finite() {
    // ISO 532-1 範囲内で sharpness が NaN / Inf を発射しないことを保証。
    let n_specific = vec![[0.5_f64; N_SPEC_BINS]; 16];
    let n_total = vec![10.0_f64; 16];
    let out = sharpness::compute(&n_specific, &n_total);
    for (i, &s) in out.iter().enumerate() {
        assert!(s.is_finite(), "frame {i} sharpness not finite: {s}");
        assert!(s >= 0.0, "frame {i} sharpness negative: {s}");
        // DIN 45692 Widmann の一般的な実音源での上限 (<10 acum)。
        // 合成 constant 入力でもこの範囲に収まるのは sanity check として十分。
        assert!(s < 20.0, "frame {i} sharpness unreasonably large: {s}");
    }
}

// ── calc_slopes N_spec 自己整合性 ──────────────────────────────────────────

const N_CORE: usize = 21; // 20 Bark + 1 padding (phase_d/mod.rs §パイプライン)

#[test]
fn n_spec_determinism_under_g_79_01_tolerance() {
    // calc_slopes::compute の N_spec 出力を G-79-01 相対許容値で決定性テスト。
    // 同一入力二回 → max_relative=1e-3 よりはるかに厳しく一致するはず。
    let core: Vec<[f64; N_CORE]> = (0..16)
        .map(|t| {
            let mut f = [0.0_f64; N_CORE];
            for (i, v) in f.iter_mut().enumerate() {
                *v = 0.2 + ((i as f64) * 0.05) + ((t as f64) * 0.01);
            }
            f
        })
        .collect();

    let a = calc_slopes::compute(&core);
    let b = calc_slopes::compute(&core);

    assert_eq!(a.n_specific.len(), b.n_specific.len());
    assert_eq!(a.n_total.len(), b.n_total.len());

    // N_spec (bin-by-bin) — G-79-01 max_relative=1e-3 使用 (実際は exact eq のはず)
    for (fa, fb) in a.n_specific.iter().zip(b.n_specific.iter()) {
        for (x, y) in fa.iter().zip(fb.iter()) {
            if x.abs() < f64::EPSILON && y.abs() < f64::EPSILON {
                continue; // 両方 0 近傍は relative 判定不安定 — skip
            }
            approx::assert_relative_eq!(x, y, max_relative = N_SPEC_MAX_RELATIVE);
        }
    }

    // N total — bit-identical モジュールなので epsilon=1e-8 で照合
    for (x, y) in a.n_total.iter().zip(b.n_total.iter()) {
        approx::assert_abs_diff_eq!(x, y, epsilon = BIT_IDENTICAL_EPSILON);
    }
}

// ── MoSQITo golden 比較 (fixture 配備後に活性化) ─────────────────────────────

/// fixture 配備後に活性化する想定の stub。Python MoSQITo で生成した
/// `tests/fixtures/phase_d/{input,expected}_*.json` を読み込み、
/// G-79-01 tolerance で rust 出力と照合する。
///
/// 配置 convention (未配備):
///   tests/fixtures/phase_d/
///     pink_noise_60s_48k_core.json        — N_CORE × frames 入力
///     pink_noise_60s_48k_n_spec.json      — 期待 N_spec
///     pink_noise_60s_48k_sharpness.json   — 期待 sharpness
///
/// cargo test --ignored で呼び出されたとき、fixture が無ければ明示 panic して
/// "MoSQITo golden fixtures not generated yet" を表示する。fixture 生成は別タスク。
#[test]
#[ignore = "requires MoSQITo golden fixtures under tests/fixtures/phase_d/ (separate task)"]
fn sharpness_matches_mosqito_golden() {
    let fixture_dir = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/fixtures/phase_d");
    assert!(
        std::path::Path::new(fixture_dir).exists(),
        "MoSQITo golden fixtures missing at {}. Generate with Python MoSQITo and retry.",
        fixture_dir
    );
    // 実 fixture 配備時にここで:
    //   let input_core = load_json_core(...);
    //   let expected = load_json_f64_vec("sharpness.json");
    //   let actual = sharpness::compute(&input_n_spec, &input_n_total);
    //   for (x, y) in actual.iter().zip(expected.iter()) {
    //       approx::assert_abs_diff_eq!(x, y, epsilon = SHARPNESS_EPSILON_ACUM);
    //   }
    unreachable!("fixture check above must fail first");
}

#[test]
#[ignore = "requires MoSQITo golden fixtures under tests/fixtures/phase_d/ (separate task)"]
fn n_spec_matches_mosqito_golden() {
    let fixture_dir = concat!(env!("CARGO_MANIFEST_DIR"), "/tests/fixtures/phase_d");
    assert!(
        std::path::Path::new(fixture_dir).exists(),
        "MoSQITo golden fixtures missing at {}. Generate with Python MoSQITo and retry.",
        fixture_dir
    );
    // 実 fixture 配備時にここで N_spec bin-by-bin で
    //   approx::assert_relative_eq!(x, y, max_relative = N_SPEC_MAX_RELATIVE);
    unreachable!("fixture check above must fail first");
}
