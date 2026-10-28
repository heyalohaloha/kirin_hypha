//! Shared measurement snapshot; persistence has its own explicit schema.
use super::PsbSummary;

///
/// Measure Thread が更新し、IO Thread と GUI Thread が読む。
/// `Arc<Mutex<MeasureResult>>` で共有する。
#[derive(Debug, Clone, Default)]
pub struct MeasureResult {
    /// Measure Thread が共有結果を書き込むたびに進める単調 sequence。
    /// GUI の freshness 判定専用で、Record/TRACE/plugin_data の正本値には使わない。
    pub measure_sequence: u64,

    /// Audio Thread が transport playback pass 境界で進める id。
    /// Watch 表示の「再生 pass ごとの最大値」reset 境界として使う。
    pub playback_pass_id: u64,

    /// LUFS-M: ITU-R BS.1770-4 Momentary Loudness（400ms sliding）。
    /// 信号なし・ウィンドウ未満は `None`（GUI 表示 `---`）。
    pub lufs_m: Option<f64>,

    /// A full Momentary window measured a finite value at or below the loudness floor.
    /// Internal provenance for TRACE silence; false during window preparation and not serialized.
    pub momentary_silence_floor: bool,

    /// LUFS-S: ITU-R BS.1770-4 Short-term Loudness（3s sliding）。
    /// 信号なし・3秒窓未満は `None`（GUI 表示 `---`）。
    ///
    /// PSR 算出時に既に取得している同じ ebur128 値を表示経路へ露出するだけであり、
    /// Record/TRACE/plugin_data の既存 schema には追加しない。
    pub lufs_s: Option<f64>,

    /// 100ms の実測チャンクから生成された結果なら `true`。
    ///
    /// reset / Inactive / playback-pass cutover が公開する空スナップショットは `false`。
    /// Stop 後の最終 Record 表示から Watch へ戻す際、transport flag だけでなく
    /// 「新しい Watch 実測が届いた」ことを識別する表示専用メタデータ。
    pub computed: bool,

    /// True Peak「直近」tp_recent: ITU-R BS.1770-4 Annex 2（4× oversampling, dBTP）。
    /// 直近 400ms（LUFS-M と同窓・フレーム基準）の inter-sample 最大値。Watch 表示用。
    /// 0 dBTP 超 → GUI で赤表示。
    pub true_peak: Option<f64>,

    /// True Peak「セッション最大」tp_session_max: init（reset）以降の inter-sample running
    /// max（dBTP）。Record/.kirin の正本（`SessionSummary.max_true_peak` と同一定義）。
    /// Watch でも live に算出される（compute 毎）。
    pub tp_session_max: Option<f64>,

    /// Crest Factor: peak_dBFS − RMS_dBFS（400ms window）。
    /// peak はサンプルピーク（True Peak ではない。PSR 計算との整合）。
    pub crest: Option<f64>,

    /// PSR: peak_dBFS − LUFS_S（3s Short-term Loudness）。
    /// Watch GUI では非表示。IO Thread が /tmp/ に書き込む。
    /// 3 秒未満は `None`。
    pub psr: Option<f64>,

    /// ISO 532-1 Zwicker の総ラウドネス N (specific loudness N'(z) ではない)
    /// を `temporal_weighting` LP IIR で時系列平滑化した値。DAWの固定6セルからは外すが、
    /// Record/TRACE/plugin_data/Kirin OS向けの計測・保存は従来どおり継続する。
    /// field 名 `n_prime_total` は履歴互換維持 (Phase 2 で rename 候補)。
    /// Measure Thread が host sample rate から 48 kHz mono へ変換した Phase D 結果なので、
    /// 対応 sample rate では 48 kHz 以外も `Some` になり得る。
    pub n_prime_total: Option<f64>,

    pub sharpness: Option<f64>,

    pub psb_summary: Option<PsbSummary>,

    /// plugin_data/.../post/*.json `Frame.n_prime[20]` に直接書き込む値。
    pub n_prime: Option<[f64; 20]>,

    /// plugin_data/.../post/*.json `psb_snapshots[].psb` に直接書き込む値。
    /// 3 帯域集約 (low/mid/high, dB) は `psb_summary` 側。
    pub psb_bark: Option<[f64; 20]>,
}
