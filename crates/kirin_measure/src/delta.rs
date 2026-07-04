//! Δ算出型 — POST側 IO Thread が PRE ファイルを読んで算出した差分結果。

#[derive(Debug, Clone, PartialEq, Default)]
pub enum DeltaMode {
    /// PRE ファイルなし（Δ非表示モード）
    #[default]
    NoPre,

    /// PRE が明示的にバイパス/OFF（POST 単独表示へ戻す）
    Bypassed,

    /// PRE ファイルはあるが t が 2〜10 秒古い（GUI グレーアウト）
    Stale,

    /// PRE ファイルが 2 秒以内に更新されている（通常表示）
    Active,
}

/// 直近 `DeltaMode::Active` 時の Δ 6 軸スナップショット (B-048 / G-115-245 Last Known Good)。
///
/// `DeltaResult::last_active` で保持し、PRE pre.json の fs lag で `DeltaMode::NoPre` /
/// `Stale` に落ちた区間に **凍結値を `COL_MUTED` で表示**することで、ユーザに
/// 「数値が消える / 絶対値表示に切り替わる」体験を与えないための保持機構。
///
/// 不変条件: 本 struct は `DeltaResult` の field としてのみ存在し、`DeltaMode::Active`
/// 経路で `run_tick` が `Some(snapshot)` を書き込む (`compute_delta_with_state` は
/// 触らない / B-048 §4-1, §4-2)。
#[derive(Debug, Clone, Default)]
pub struct DeltaSnapshot {
    pub lufs: Option<f64>,
    pub psr: Option<f64>,
    pub tp: Option<f64>,
    pub n_prime_total: Option<f64>,
    pub crest: Option<f64>,
    pub sharpness: Option<f64>,
}

/// POST − PRE の差分結果。
///
/// IO Thread POST が更新し、GUI Thread が読む。
/// `Arc<Mutex<DeltaResult>>` で共有する。
///
/// Lens 側へ分離。本 struct はそれに合わせ Δ 6 軸を網羅する (lufs / psr / tp /
/// n_prime_total / crest / sharpness)。`mode` は鮮度判定で全 Δ フィールド共有。
#[derive(Debug, Clone, Default)]
pub struct DeltaResult {
    /// Δ LUFS-M = POST_lufs_m − PRE_lufs_m。PRE/POST どちらかが None → None。
    pub lufs: Option<f64>,

    /// Δ PSR = POST_psr − PRE_psr。PRE/POST どちらかが None → None。
    pub psr: Option<f64>,

    /// Δ True Peak = POST_tp − PRE_tp。PRE/POST どちらかが None → None。
    pub tp: Option<f64>,

    /// Δ N (filtered total loudness) = POST_n_prime_total − PRE_n_prime_total [sone]。
    /// PRE/POST どちらかが None → None。
    pub n_prime_total: Option<f64>,

    /// Δ Crest = POST_crest − PRE_crest。PRE/POST どちらかが None → None。
    pub crest: Option<f64>,

    /// Δ Sharpness = POST_sharpness − PRE_sharpness [acum]。PRE/POST どちらかが None → None。
    pub sharpness: Option<f64>,

    /// PREファイルの鮮度状態。NoPre/Stale/Bypassed 時は全 Δ フィールドを表示しない。
    pub mode: DeltaMode,

    /// 直近 `DeltaMode::Active` 時の Δ 6 軸スナップショット (B-048 / G-115-245)。
    ///
    /// PRE pre.json の fs lag で `DeltaMode::NoPre` / `Stale` に落ちた区間に
    /// GUI 側で凍結値を `COL_MUTED` 色で表示する Last Known Good 機構。
    /// `run_tick` でのみ書き込まれる (`compute_delta_with_state` は触らない /
    /// §4-2)。`#[derive(Default)]` 自動派生で `None` 初期化。
    pub last_active: Option<DeltaSnapshot>,
}

#[cfg(test)]
mod tests {
    use super::*;

    /// B-048 / G-115-245: `DeltaSnapshot::default()` で 6 field 全 None。
    /// `#[derive(Default)]` + `Option<f64>::default() = None` の自動派生確認 (§4-5)。
    #[test]
    fn delta_snapshot_default_all_none() {
        let snap = DeltaSnapshot::default();
        assert!(snap.lufs.is_none());
        assert!(snap.psr.is_none());
        assert!(snap.tp.is_none());
        assert!(snap.n_prime_total.is_none());
        assert!(snap.crest.is_none());
        assert!(snap.sharpness.is_none());
    }

    /// B-048 / G-115-245: `DeltaResult::default()` で `last_active = None`。
    /// 既存 6 field + mode の default も併せて検証 (順序維持の guard-rail / §4-5)。
    #[test]
    fn delta_result_default_last_active_none() {
        let result = DeltaResult::default();
        assert!(result.last_active.is_none());
        assert!(result.lufs.is_none());
        assert!(result.psr.is_none());
        assert!(result.tp.is_none());
        assert!(result.n_prime_total.is_none());
        assert!(result.crest.is_none());
        assert!(result.sharpness.is_none());
        assert_eq!(result.mode, DeltaMode::NoPre);
    }
}
