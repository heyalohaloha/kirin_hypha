//! Δ算出型 — POST側 IO Thread が PRE ファイルを読んで算出した差分結果。

/// PRE ファイルの鮮度状態（guardian_53 T-5 Δ鮮度判定）。
#[derive(Debug, Clone, PartialEq, Default)]
pub enum DeltaMode {
    /// PRE ファイルなし（Δ非表示モード）
    #[default]
    NoPre,

    /// PRE ファイルはあるが t が 2〜10 秒古い（GUI グレーアウト）
    Stale,

    /// PRE ファイルが 2 秒以内に更新されている（通常表示）
    Active,
}

/// POST − PRE の差分結果。
///
/// IO Thread POST が更新し、GUI Thread が読む。
/// `Arc<Mutex<DeltaResult>>` で共有する。
#[derive(Debug, Clone, Default)]
pub struct DeltaResult {
    /// Δ LUFS-M = POST_lufs_m − PRE_lufs_m。PRE/POST どちらかが None → None。
    pub lufs: Option<f64>,

    /// Δ True Peak = POST_tp − PRE_tp。PRE/POST どちらかが None → None。
    pub tp: Option<f64>,

    /// Δ Crest = POST_crest − PRE_crest。PRE/POST どちらかが None → None。
    pub crest: Option<f64>,

    /// PREファイルの鮮度状態。NoPre/Stale 時は lufs/tp/crest を表示しない。
    pub mode: DeltaMode,
}
