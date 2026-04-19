//! Phase D: ISO 532-1 Psychoacoustic Analysis Engine
//!
//! Kirin Hypha 移植版（Lens `native/src/psychoacoustic/` からアルゴリズム同一移植）。
//! napi-rs / symphonia 依存なし。定数テーブル・フィルタ係数は MoSQITo bit-identical。
//!
//! パイプライン:
//! ```text
//! mono 48kHz → filter_bank (28-band SOS IIR → SPL @2kHz)
//!            → core_loudness (28 → 20 Bark + 1 padding = 21 bands)
//!            → nonlinear_decay (temporal decay simulation)
//!            → calc_slopes (21 core → 240-bin specific loudness + N total)
//!            → temporal_weighting (N(t) smoothing)
//!            → sharpness (DIN 45692 Widmann)
//!            → spectral_balance (240-bin → 20 Bark PSB)
//! ```
//!
//! T-1: バッチ処理インターフェース（Lens 互換テスト用）。
//! T-2 でストリーミングアダプタを追加する。

pub mod tables;
pub mod filter_bank;
pub mod core_loudness;
pub mod nonlinear_decay;
pub mod calc_slopes;
pub mod temporal_weighting;
pub mod sharpness;
pub mod spectral_balance;
pub mod stream;
