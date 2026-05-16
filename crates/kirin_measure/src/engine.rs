//! リアルタイム計測エンジン。
//!
//! Measure Thread が呼び出す。Audio Thread のリングバッファから受け取った
//! インターリーブ f64 サンプルを ebur128 と独自スライディングウィンドウで処理し、
//! 4項目（LUFS-M / True Peak / Crest / PSR）を 100ms 単位で更新する。

use ebur128::{EbuR128, Mode};
use std::collections::VecDeque;
use std::time::{Duration, Instant};

use crate::MeasureResult;

/// Record セッション終了時の集計値（B-043）。
///
/// `MeasureEngine::finalize()` が `EbuR128` から抽出して返す。
/// 3層隔離維持のため IO Thread（`PluginDataWriter`）は `EbuR128` を直接持たない。
/// Measure Thread が finalize() を呼び、`Arc<Mutex<Option<SessionSummary>>>` 経由で
/// IO Thread に渡し、IO Thread が `PluginDataWriter::set_session_aggregates()` で
/// JSON に注入する。
#[derive(Debug, Clone, Copy, Default)]
pub struct SessionSummary {
    /// EBU R128 Integrated Loudness [LUFS]。`loudness_global()` の結果。
    pub lufs_i: Option<f64>,
    /// EBU R128 Loudness Range [LU]。`loudness_range()` の結果。
    pub lra: Option<f64>,
    /// セッション内 True Peak 全チャンネル最大値 [dBTP]。
    /// `ebu.true_peak(ch)` は init 以降の running max（linear）。20·log10 で dBTP 化済。
    pub max_true_peak: Option<f64>,
}

/// True Peak スライディング窓の幅。
///
/// Active 状態中のスレッドスケジューリング揺らぎで push() が 100ms 以上遅れても
/// TP が一瞬 --- に落ちないようにする猶予。2秒は Measure Thread 再起動の
/// 復帰時間をカバーする。
/// transport 停止時は SignalState=Inactive が即座に --- を宣言するため、
/// この窓は Active 中の安定性のみに寄与する。
const TP_WINDOW_DURATION: Duration = Duration::from_secs(2);

/// リアルタイム計測エンジン（Measure Thread 専用。Send 不要）。
pub struct MeasureEngine {
    ebu: EbuR128,
    n_channels: usize,

    /// 400ms スライディングウィンドウ（インターリーブ f64）。
    /// Crest Factor と PSR の peak_dBFS / RMS_dBFS 計算に使う。
    ///  "peak = サンプルピーク（True Peakではない。PSR計算との整合性のため）"
    window_400ms: VecDeque<f64>,
    /// 400ms ウィンドウの上限要素数（sample_rate × 0.4 × n_channels）
    window_400ms_cap: usize,

    /// ebur128 への投入をまとめる 100ms 積算バッファ（インターリーブ f64）。
    /// 100ms 分の要素数に達したら ebur128 に add_frames_f64() する。
    accum: Vec<f64>,
    /// 100ms 分の要素数（sample_rate / 10 × n_channels）
    accum_target: usize,

    /// True Peak タイムスタンプ付きスライディング窓。
    ///
    /// エントリ = (prev_true_peak linear値, 記録した実時刻)。
    /// ebur128 の true_peak() は累積最大値（running max）であり transport 停止後も
    /// 古いピークが残るため、prev_true_peak() を使い実時間 400ms 以内のエントリのみを
    /// 最大化する。これにより:
    /// 1. 通常再生中: LUFS-M と同じ 400ms 窓を実現（T-3 精度要件）
    /// 2. transport 停止 → 再開: 停止前のピークが 400ms 経過後に自動失効（TP 修正）
    tp_window: VecDeque<(f64, Instant)>,
}

impl MeasureEngine {
    /// 新規計測エンジンを生成する。
    ///
    /// # Errors
    /// ebur128 の初期化失敗時に Err を返す。
    pub fn new(sample_rate: u32, n_channels: usize) -> Result<Self, String> {

        // LUFS-M (Mode::M) + LUFS-S(PSR用, Mode::S) + Integrated (Mode::I)
        // + LRA (Mode::LRA) + True Peak (Mode::TRUE_PEAK)
        // B-043: Integrated / LRA は Record 終了時に finalize() で集計
        // （loudness_global / loudness_range）。Watch 中は読み出さない。
        let mode = Mode::M | Mode::S | Mode::I | Mode::LRA | Mode::TRUE_PEAK;
        let ebu = EbuR128::new(n_channels as u32, sample_rate, mode)
            .map_err(|e| format!("EbuR128::new: {:?}", e))?;

        // 400ms = sample_rate × 0.4 × n_channels（インターリーブ要素数）
        let window_400ms_cap = (sample_rate as usize) * 4 / 10 * n_channels;
        // 100ms = sample_rate / 10 × n_channels
        let accum_target = (sample_rate as usize) / 10 * n_channels;

        Ok(Self {
            ebu,
            n_channels,
            window_400ms: VecDeque::with_capacity(window_400ms_cap + 16),
            window_400ms_cap,
            accum: Vec::with_capacity(accum_target * 2),
            accum_target,
            // 最大 4 エントリ（400ms / 100ms）。実時間フィルタで失効するので容量は余裕を持つ。
            tp_window: VecDeque::with_capacity(8),
        })
    }

    /// エンジン状態を完全リセットする。
    ///
    /// - ebur128 内部状態（FIR 補間フィルタ遅延ライン・LUFS 履歴）をクリア
    /// - tp_window / window_400ms / accum をクリア
    ///
    /// SignalState が非Active → Active に遷移したとき Measure Thread が呼ぶ。
    /// 前のセッションの FIR フィルタ遅延ライン（12 タップ）が新セッション最初の
    /// TP に影響するのを防ぐ。
    pub fn reset(&mut self) {
        self.ebu.reset();
        self.tp_window.clear();
        self.window_400ms.clear();
        self.accum.clear();
    }

    /// インターリーブ f64 サンプルを受け取り、100ms チャンクが揃うたびに
    /// MeasureResult を返す。チャンク未満の場合は None を返す。
    pub fn push(&mut self, samples: &[f64]) -> Option<MeasureResult> {
        for &s in samples {
            self.window_400ms.push_back(s);
            self.accum.push(s);
        }

        // 400ms ウィンドウを制限（古いサンプルを破棄）
        while self.window_400ms.len() > self.window_400ms_cap {
            self.window_400ms.pop_front();
        }

        // 100ms チャンクが揃ったら ebur128 に投入 → 結果を更新
        // 複数チャンク分溜まっている場合は全て処理し、最後の結果を返す
        let mut result: Option<MeasureResult> = None;
        while self.accum.len() >= self.accum_target {
            let chunk: Vec<f64> = self.accum.drain(..self.accum_target).collect();
            // add_frames_f64 のエラーは無視（積算不足は ebur128 が内部処理する）
            let _ = self.ebu.add_frames_f64(&chunk);

            // add_frames 直後に prev_true_peak を取得してタイムスタンプ付きで窓に追加。
            // prev_true_peak は直近 add_frames チャンク内のピークのみを返す（running max でない）。
            let chunk_tp = (0..self.n_channels as u32)
                .filter_map(|ch| self.ebu.prev_true_peak(ch).ok())
                .fold(0.0_f64, f64::max);
            self.tp_window.push_back((chunk_tp, Instant::now()));

            // 窓サイズ上限（スペース節約。実時間フィルタが主な失効手段）
            while self.tp_window.len() > 8 {
                self.tp_window.pop_front();
            }

            result = Some(self.compute());
        }
        result
    }

    /// Record セッション終了時に呼び、ebur128 から LUFS-I / LRA / max TP を抽出（B-043）。
    ///
    /// - LUFS-I: `loudness_global()` の結果（10s 以上の素材が必要）
    /// - LRA: `loudness_range()` の結果（60s 以上の素材が推奨）
    /// - max_true_peak: 全チャンネル `true_peak(ch)` の linear running max を 20·log10 dB 化
    ///
    /// いずれも `is_finite()` でないものは `None` にして返す。
    /// 3層隔離: 呼ぶのは Measure Thread のみ。IO Thread からは直接呼ばない。
    pub fn finalize(&self) -> SessionSummary {
        let lufs_i = self.ebu.loudness_global().ok().filter(|v| v.is_finite());
        let lra = self.ebu.loudness_range().ok().filter(|v| v.is_finite());

        let max_tp_lin = (0..self.n_channels as u32)
            .filter_map(|ch| self.ebu.true_peak(ch).ok())
            .filter(|v| v.is_finite())
            .fold(None::<f64>, |acc, v| Some(acc.map_or(v, |a| a.max(v))));

        let max_true_peak = max_tp_lin.and_then(|v| {
            if v > 0.0 {
                Some(20.0 * v.log10())
            } else {
                None
            }
        });

        SessionSummary { lufs_i, lra, max_true_peak }
    }

    fn compute(&self) -> MeasureResult {
        // ── LUFS-M (ITU-R BS.1770-4 Momentary, 400ms sliding) ───────────
        // ebur128 が内部で 400ms ウィンドウを管理する。
        // 400ms 未満の場合は -inf を返すので filter(is_finite) で None にする。
        let lufs_m = self.ebu.loudness_momentary().ok().filter(|v| v.is_finite());

        // ── True Peak (ITU-R BS.1770-4 Annex 2, 4× oversampling) ────────
        // 実時間 400ms 以内のエントリのみを使って最大値を算出する。
        // この実時間フィルタにより:
        // - 通常再生中: LUFS-M と同じ 400ms 窓を実現
        // - transport 停止 → 再開: 前のセッションのピークが 400ms 経過後に自動失効
        //   （process() が止まれば push() も止まり、古いエントリは時刻で自然に消える）
        // 有効エントリが 0 件の場合は明示的に None（---）を返す。
        // これは transport 停止後に tp_window が全て失効した状態に対応する。
        let now = Instant::now();
        let valid_tp: f64 = self.tp_window
            .iter()
            .filter(|(_, t)| now.duration_since(*t) <= TP_WINDOW_DURATION)
            .map(|(v, _)| *v)
            .fold(f64::NEG_INFINITY, f64::max);

        let true_peak = if valid_tp.is_finite() && valid_tp > 0.0 {
            Some(20.0 * valid_tp.log10())
        } else {
            None  // 有効エントリ 0 件（失効済み）または無音 → ---
        };

        // ── Crest Factor + PSR ───────────────────────────────────────────
        let (crest, psr) = self.compute_crest_psr();

        MeasureResult { lufs_m, true_peak, crest, psr, ..Default::default() }
    }

    /// 400ms ウィンドウから Crest Factor と PSR を算出する。
    ///
    /// - Crest = peak_dBFS - RMS_dBFS（400ms, サンプルピーク）
    /// - PSR   = peak_dBFS - LUFS_S（3s Short-term）
    ///
    ///  "peak = サンプルピーク（True Peakではない）"
    fn compute_crest_psr(&self) -> (Option<f64>, Option<f64>) {
        if self.window_400ms.is_empty() {
            return (None, None);
        }

        let peak = self.window_400ms.iter().map(|s| s.abs()).fold(0.0_f64, f64::max);
        let sum_sq: f64 = self.window_400ms.iter().map(|s| s * s).sum();
        let rms = (sum_sq / self.window_400ms.len() as f64).sqrt();

        if peak <= 0.0 || rms <= 0.0 {
            return (None, None);
        }

        let peak_db = 20.0 * peak.log10(); // dBFS
        let rms_db = 20.0 * rms.log10();   // dBFS

        // Crest Factor
        let crest = Some(peak_db - rms_db);

        // PSR: peak_dBFS - LUFS_S。
        // LUFS_S は 3 秒ウィンドウが揃うまで -inf を返す → None になる。
        let psr = self.ebu.loudness_shortterm().ok()
            .filter(|v| v.is_finite())
            .map(|lufs_s| peak_db - lufs_s);

        (crest, psr)
    }
}
