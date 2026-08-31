//! リアルタイム計測エンジン。
//!
//! Measure Thread が呼び出す。Audio Thread のリングバッファから受け取った
//! インターリーブ f64 サンプルを ebur128 と独自スライディングウィンドウで処理する。
//! EBU Tech 3341の20ms alignment testを取りこぼさないよう内部解析は10ms、既存の
//! MeasureResult / TRACE / GUI observer公開は100msのまま分離する。

use ebur128::{EbuR128, Mode};
use std::collections::VecDeque;

use crate::MeasureResult;

/// B-205: サブサイレンス・フロア。実プログラム素材（おおむね -60..0 LUFS / dBTP）の遥か下に置く。
/// 無音ゲート（-140 dBFS / Audio Thread）を僅かに越える微小残渣（dither / denormal / fade tail）が
/// momentary loudness / true peak を巨大負値（例: -180 LUFS）として描画するのを防ぐ。フロア未満は
/// 「無信号」とみなし `None`（GUI `---`）にする。Measure Thread 単一点での floor なので JSON / IO /
/// 両 GUI が一斉に `---` へ収束する（B-205 信号状態修正の defense-in-depth）。
const LUFS_VALID_FLOOR_LUFS: f64 = -100.0;
const TP_VALID_FLOOR_DBTP: f64 = -100.0;

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
    /// セッション内 True Peak 全チャンネル最大値 [dBTP] = tp_session_max（正本）。
    /// `ebu.true_peak(ch)` は init（reset）以降の inter-sample running max（linear）。
    /// 20·log10 で dBTP 化済。`compute()` が返す `MeasureResult.tp_session_max` と同一定義。
    pub max_true_peak: Option<f64>,
}

/// True Peak「直近」窓の幅（フレーム）。LUFS-M と同じ 400ms（B-074）。
///
/// B-074 以前は wall-clock 2 秒窓（`Instant` 比較）だったが、(1) コメントが主張する
/// 400ms と実値 2s が乖離していた、(2) wall-clock は offline/高速 bounce で
/// 音声時間と測定窓がズレた。本定数はサンプル（フレーム）基準で、accum 投入済み
/// フレーム数を基準に失効させる。スレッドスケジューリング揺らぎで push() が遅れても、
/// 窓は処理済みフレームで進むため（壁時計でない）TP が一瞬 --- に落ちない。
/// transport 停止時は SignalState=Inactive が即座に --- を宣言し、再開時は
/// `reset()` が tp_window をクリアするため、停止前ピークの持ち越しは起きない。
fn tp_recent_window_frames(sample_rate: u32) -> u64 {
    // ebur128 と100ms境界を共有する。丸めも100msを4倍し、400ms窓にする。
    ((sample_rate as u64 + 5) / 10) * 4
}

/// ebur128と同じ100msフレーム数を10個の解析区間へ分配する。
///
/// 標準sample rateでは各10msは同数フレームになる。例えば44,105Hzのように
/// 100で割り切れない場合は、10区間の合計が必ず丸め後の100msと一致するよう、
/// 441/442フレームの可変長区間に分ける。
fn analysis_chunk_frames(publish_frames: usize, phase: u8) -> usize {
    debug_assert!(phase < 10);
    let start = usize::from(phase) * publish_frames / 10;
    let end = (usize::from(phase) + 1) * publish_frames / 10;
    end - start
}

fn maximum(current: Option<f64>, candidate: Option<f64>) -> Option<f64> {
    match (current, candidate) {
        (Some(current), Some(candidate)) => Some(current.max(candidate)),
        (current, candidate) => current.or(candidate),
    }
}

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

    /// ebur128 への投入をまとめる10ms解析バッファ（インターリーブ f64）。
    accum: Vec<f64>,
    /// 次の約10ms解析区間の要素数。10区間の合計は常に`publish_target`。
    analysis_target: usize,
    /// 100ms内の次の解析区間番号（0..=9）。
    analysis_phase: u8,

    /// B-078/B-605: ebur128投入用の再利用バッファ（事前確保）。10msチャンクごとの
    /// `drain().collect()` 新 Vec 確保を排し、`clear()` + `extend` で使い回す。
    /// Measure Thread上のalloc churnを除去する。
    chunk_buf: Vec<f64>,

    /// 既存observerへ渡す正本100ms PCM。内部10ms解析とUI/TRACE cadenceを分離する。
    publish_buf: Vec<f64>,
    /// ebur128と同じ丸め規則の100ms要素数。
    publish_target: usize,

    /// EBU Tech 3341 #10/#11/#13/#14用の、reset以降10ms cadence最大値。
    /// 現在値の100ms公開やSessionSummary ABIには混ぜない。
    max_lufs_m: Option<f64>,
    max_lufs_s: Option<f64>,

    /// observer公開済みの累積フレーム数（per-channel / 100ms境界）。既存TRACE clock契約。
    total_frames: u64,

    /// ebur128投入済みの累積フレーム数（per-channel / 10ms境界）。`tp_window`専用clock。
    /// 公開clockから分離し、pending PCMを二重計上しない。
    analysis_frames: u64,

    /// True Peak「直近」窓（B-074: フレーム基準）。
    ///
    /// エントリ = (per-chunk inter-sample true_peak linear値, 記録時点の analysis_frames)。
    /// ebur128 の true_peak() は累積最大値（running max）で transport 停止後も古いピークが
    /// 残るため、prev_true_peak()（直近 add_frames チャンク内ピーク）を使い、フレーム基準で
    /// 直近 400ms 以内のエントリのみを最大化して tp_recent を得る（LUFS-M と同窓 / T-3）。
    tp_window: VecDeque<(f64, u64)>,
    /// tp_window の失効しきい値（フレーム / = 400ms）。
    tp_window_frames: u64,
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

        // ebur128が採用する丸め後の100msフレーム数を観測時刻の正本にする。
        let publish_frames = (sample_rate as usize + 5) / 10;
        if publish_frames < 10 {
            return Err(format!(
                "sample rate {sample_rate} Hz is too low for 10ms analysis cadence"
            ));
        }
        let publish_target = publish_frames * n_channels;
        let analysis_capacity = publish_frames.div_ceil(10) * n_channels;
        let analysis_target = analysis_chunk_frames(publish_frames, 0) * n_channels;

        // 400ms = 丸め後100ms × 4 × n_channels（インターリーブ要素数）
        let window_400ms_cap = publish_frames * 4 * n_channels;

        Ok(Self {
            ebu,
            n_channels,
            window_400ms: VecDeque::with_capacity(window_400ms_cap + 16),
            window_400ms_cap,
            accum: Vec::with_capacity(analysis_capacity * 2),
            analysis_target,
            analysis_phase: 0,
            chunk_buf: Vec::with_capacity(analysis_capacity),
            publish_buf: Vec::with_capacity(publish_target),
            publish_target,
            max_lufs_m: None,
            max_lufs_s: None,
            total_frames: 0,
            analysis_frames: 0,
            // 最大40エントリ（400ms / 10ms）。フレーム基準で失効するので容量は余裕を持つ。
            tp_window: VecDeque::with_capacity(48),
            tp_window_frames: tp_recent_window_frames(sample_rate),
        })
    }

    /// エンジン状態を完全リセットする。
    ///
    /// - ebur128 内部状態（FIR 補間フィルタ遅延ライン・LUFS 履歴・true_peak running max）をクリア
    /// - tp_window / window_400ms / accum をクリア
    ///
    /// SignalState が非Active → Active に遷移したとき Measure Thread が呼ぶ。
    /// 前のセッションの FIR フィルタ遅延ライン（12 タップ）が新セッション最初の
    /// TP に影響するのを防ぐ。`total_frames` は相対距離専用なので維持する。
    pub fn reset(&mut self) {
        self.ebu.reset();
        self.tp_window.clear();
        self.window_400ms.clear();
        self.accum.clear();
        self.analysis_phase = 0;
        self.analysis_target =
            analysis_chunk_frames(self.publish_target / self.n_channels, 0) * self.n_channels;
        self.publish_buf.clear();
        self.max_lufs_m = None;
        self.max_lufs_s = None;
    }

    /// 100ms observerへ公開済みの累積フレーム数（48k/engine sample time）。
    pub fn total_frames(&self) -> u64 {
        self.total_frames
    }

    /// Input frames retained below the next 100 ms observer boundary.
    ///
    /// A caller that causally primes this engine may begin a real-audio push with an already
    /// populated 10ms analysis buffer or 100ms publication buffer. Observer `total_frames` counts
    /// the completed 100ms slot, including that prefix; this is the bridge to the callback offset.
    pub(crate) fn pending_frames(&self) -> u64 {
        ((self.publish_buf.len() + self.accum.len()) / self.n_channels) as u64
    }

    /// reset以降の10ms cadence Maximum Momentary Loudness。UI current値とは独立。
    pub fn max_lufs_m(&self) -> Option<f64> {
        self.max_lufs_m
    }

    /// reset以降の10ms cadence Maximum Short-term Loudness。UI current値とは独立。
    pub fn max_lufs_s(&self) -> Option<f64> {
        self.max_lufs_s
    }

    /// インターリーブ f64 サンプルを受け取り、100ms チャンクが揃うたびに
    /// MeasureResult を返す。チャンク未満の場合は None を返す。
    pub fn push(&mut self, samples: &[f64]) -> Option<MeasureResult> {
        self.push_observed(samples, |_, _, _| {})
    }

    /// `push()` と同じ処理を行い、100ms チャンクごとの結果を observer に渡す。
    ///
    /// Offline bounce では 1 回の drain で複数 100ms チャンクが処理されるため、最後の
    /// 1 件だけでなく中間結果も Record TRACE に渡す。observer の第 3 引数は、
    /// その結果を生成した 100ms 分の interleaved input samples。
    pub fn push_observed(
        &mut self,
        samples: &[f64],
        mut observe: impl FnMut(u64, &MeasureResult, &[f64]),
    ) -> Option<MeasureResult> {
        self.push_observed_internal(samples, false, |frames, result, observed, _| {
            observe(frames, result, observed);
        })
    }

    /// Meter Session専用observer。各100 ms境界のIとMaxTPから確定したPLRを、その境界の
    /// current値と同時に返す。LRAはpush全体の最後にだけqueryし、通常の`push_observed`には
    /// Integrated queryの追加costを負わせない。
    pub fn push_observed_with_plr(
        &mut self,
        samples: &[f64],
        mut observe: impl FnMut(u64, &MeasureResult, &[f64], Option<f64>),
    ) -> Option<MeasureResult> {
        self.push_observed_internal(samples, true, |frames, result, observed, plr| {
            observe(frames, result, observed, plr);
        })
    }

    fn push_observed_internal(
        &mut self,
        samples: &[f64],
        include_plr: bool,
        mut observe: impl FnMut(u64, &MeasureResult, &[f64], Option<f64>),
    ) -> Option<MeasureResult> {
        self.accum.extend_from_slice(samples);

        // 10msごとにebur128と公式maximaを更新し、10個揃った100ms境界だけを公開する。
        let mut result: Option<MeasureResult> = None;
        while self.accum.len() >= self.analysis_target {
            let chunk_frames = (self.analysis_target / self.n_channels) as u64;
            // 再利用バッファへdrainし、同じPCMを100ms公開バッファにも保持する。
            self.chunk_buf.clear();
            self.chunk_buf
                .extend(self.accum.drain(..self.analysis_target));
            self.publish_buf.extend_from_slice(&self.chunk_buf);

            // Crest/PSRの400ms窓も解析時刻に合わせて10msずつ進める。
            self.window_400ms.extend(self.chunk_buf.iter().copied());
            while self.window_400ms.len() > self.window_400ms_cap {
                self.window_400ms.pop_front();
            }
            // add_frames_f64失敗を沈黙させず、利用者操作と非紐づきなのでlogだけに残す。
            if let Err(e) = self.ebu.add_frames_f64(&self.chunk_buf) {
                log::warn!(
                    "[engine] add_frames_f64 failed ({:?}): this 10ms chunk is not measured (frames lost)",
                    e
                );
            }

            // フレーム基準の時刻を進めてから、prev_true_peak をタイムスタンプ付きで窓に追加。
            // prev_true_peak は直近 add_frames チャンク内のピークのみを返す（running max でない）。
            self.analysis_frames += chunk_frames;
            let chunk_tp = (0..self.n_channels as u32)
                .filter_map(|ch| self.ebu.prev_true_peak(ch).ok())
                .fold(0.0_f64, f64::max);
            self.tp_window.push_back((chunk_tp, self.analysis_frames));

            // フレーム基準で 400ms より古いエントリを前から失効させる。
            while let Some(&(_, f)) = self.tp_window.front() {
                if self.analysis_frames - f >= self.tp_window_frames {
                    self.tp_window.pop_front();
                } else {
                    break;
                }
            }

            self.update_loudness_maxima();
            self.analysis_phase = (self.analysis_phase + 1) % 10;
            self.analysis_target =
                analysis_chunk_frames(self.publish_target / self.n_channels, self.analysis_phase)
                    * self.n_channels;
            if self.publish_buf.len() < self.publish_target {
                continue;
            }
            debug_assert_eq!(self.publish_buf.len(), self.publish_target);
            self.total_frames += (self.publish_target / self.n_channels) as u64;

            let computed = self.compute();
            let plr = include_plr
                .then(|| {
                    computed.tp_session_max.zip(
                        self.ebu
                            .loudness_global()
                            .ok()
                            .filter(|value| value.is_finite()),
                    )
                })
                .flatten()
                .map(|(peak, loudness)| peak - loudness)
                .filter(|value| value.is_finite());
            observe(self.total_frames, &computed, &self.publish_buf, plr);
            result = Some(computed);
            self.publish_buf.clear();
        }
        result
    }

    fn update_loudness_maxima(&mut self) {
        let momentary = self
            .ebu
            .loudness_momentary()
            .ok()
            .filter(|value| value.is_finite() && *value > LUFS_VALID_FLOOR_LUFS);
        let shortterm = self
            .ebu
            .loudness_shortterm()
            .ok()
            .filter(|value| value.is_finite() && *value > LUFS_VALID_FLOOR_LUFS);
        self.max_lufs_m = maximum(self.max_lufs_m, momentary);
        self.max_lufs_s = maximum(self.max_lufs_s, shortterm);
    }

    /// Record セッション終了時に呼び、ebur128 から LUFS-I / LRA / max TP を抽出（B-043）。
    ///
    /// - LUFS-I: `loudness_global()` の結果（10s 以上の素材が必要）
    /// - LRA: `loudness_range()` の結果（60s 以上の素材が推奨）
    /// - max_true_peak: 全チャンネル `true_peak(ch)` の linear running max を 20·log10 dB 化
    ///   = tp_session_max（`compute()` の `MeasureResult.tp_session_max` と同一定義）
    ///
    /// いずれも `is_finite()` でないものは `None` にして返す。
    /// 3層隔離: 呼ぶのは Measure Thread のみ。IO Thread からは直接呼ばない。
    pub fn finalize(&self) -> SessionSummary {
        let lufs_i = self.ebu.loudness_global().ok().filter(|v| v.is_finite());
        let lra = self.ebu.loudness_range().ok().filter(|v| v.is_finite());
        let max_true_peak = self.session_true_peak_dbtp();
        SessionSummary {
            lufs_i,
            lra,
            max_true_peak,
        }
    }

    /// init（reset）以降の inter-sample running max（dBTP）= tp_session_max。
    /// `ebu.true_peak(ch)`（linear running max）を全 ch 最大化して 20·log10 で dBTP 化。
    /// DSP（4× oversample inter-sample 検出）は ebur128 内部で不変（B-074 は窓・露出のみ変更）。
    fn session_true_peak_dbtp(&self) -> Option<f64> {
        let max_tp_lin = (0..self.n_channels as u32)
            .filter_map(|ch| self.ebu.true_peak(ch).ok())
            .filter(|v| v.is_finite())
            .fold(None::<f64>, |acc, v| Some(acc.map_or(v, |a| a.max(v))));

        // B-205: 線形ピーク>0 を dBTP 化し、サブサイレンス・フロア未満は無信号扱い（None → ---）。
        max_tp_lin
            .filter(|v| *v > 0.0)
            .map(|v| 20.0 * v.log10())
            .filter(|db| *db > TP_VALID_FLOOR_DBTP)
    }

    fn compute(&self) -> MeasureResult {
        // ── LUFS-M (ITU-R BS.1770-4 Momentary, 400ms sliding) ───────────
        // ebur128 が内部で 400ms ウィンドウを管理する。
        // 400ms 未満の場合は -inf を返すので filter(is_finite) で None にする。
        // B-205: is_finite に加えサブサイレンス・フロアを要求（フロア未満は無信号扱い → None → ---）。
        let raw_momentary = self.ebu.loudness_momentary().ok().filter(|v| v.is_finite());
        let lufs_m = raw_momentary.filter(|v| *v > LUFS_VALID_FLOOR_LUFS);
        // B-207 #2: フロア未満（有限だが <= LUFS_VALID_FLOOR）= 残留エネルギーのみの near-silence。
        // この帯では Crest/PSR（窓内の比）だけが有限値として残り「LUFS --- / Crest 3.0」の半埋まり行に
        // なるため、行全体を一斉に --- へ収束させる。warmup の -inf（raw=None）は対象外＝Crest は通常表示。
        let momentary_floored = raw_momentary.is_some_and(|v| v <= LUFS_VALID_FLOOR_LUFS);

        // ── True Peak「直近」tp_recent (ITU-R BS.1770-4 Annex 2, 4× oversampling) ──
        // フレーム基準で直近 400ms 以内（LUFS-M と同窓）のエントリのみを最大化する（B-074）。
        // - 通常再生中: LUFS-M と同じ 400ms 窓を実現
        // - 高速/offline bounce: 窓は処理済みフレーム基準なので音声時間と一致（wall-clock 依存なし）
        // - transport 停止 → 再開: reset() が tp_window をクリアし停止前ピークを持ち越さない
        // 有効エントリ 0 件（無音 / reset 直後）は明示的に None（---）を返す。
        let valid_tp: f64 = self
            .tp_window
            .iter()
            .filter(|(_, f)| self.analysis_frames - f < self.tp_window_frames)
            .map(|(v, _)| *v)
            .fold(f64::NEG_INFINITY, f64::max);

        let tp_recent = if valid_tp.is_finite() && valid_tp > 0.0 {
            // B-205: dBTP 化後、サブサイレンス・フロア未満は無信号扱い（None → ---）。
            Some(20.0 * valid_tp.log10()).filter(|db| *db > TP_VALID_FLOOR_DBTP)
        } else {
            None // 有効エントリ 0 件（失効済み）または無音 → ---
        };

        // ── True Peak「セッション最大」tp_session_max（正本・Record/.kirin と同一定義）──
        // ebur128 の running max（init=reset 以降）。Watch でも live に見えるよう毎 compute 算出。
        let tp_session_max = self.session_true_peak_dbtp();

        // ── LUFS-S + Crest Factor + PSR ──────────────────────────────────
        // PSR が従来から読んでいた同じ short-term 値を一度だけ取得し、表示経路にも露出する。
        // 3秒未満は -inf、サブサイレンス・フロア以下は None（GUI は ---）。
        let raw_shortterm = self.ebu.loudness_shortterm().ok().filter(|v| v.is_finite());
        let lufs_s = raw_shortterm.filter(|v| *v > LUFS_VALID_FLOOR_LUFS);

        // B-207 #2: サブサイレンス・フロア帯では Crest/PSR も None に倒し、絶対値グリッドの行を
        // 一斉に --- へ収束させる（半埋まり行の回避）。それ以外は通常算出。
        let (crest, psr) = if momentary_floored {
            (None, None)
        } else {
            self.compute_crest_psr(lufs_s)
        };

        MeasureResult {
            lufs_m,
            // S owns its 3 s window. A quiet final 400 ms can floor M/Crest/PSR without erasing
            // valid earlier energy that is still inside the Short-term window.
            lufs_s,
            computed: true,
            true_peak: tp_recent, // B-074: `true_peak` フィールドは直近 400ms（tp_recent）の値
            tp_session_max,
            crest,
            psr,
            ..Default::default()
        }
    }

    /// 400ms ウィンドウから Crest Factor と PSR を算出する。
    ///
    /// - Crest = peak_dBFS - RMS_dBFS（400ms, サンプルピーク）
    /// - PSR   = peak_dBFS - LUFS_S（3s Short-term）
    ///
    ///  "peak = サンプルピーク（True Peakではない）"
    fn compute_crest_psr(&self, lufs_s: Option<f64>) -> (Option<f64>, Option<f64>) {
        if self.window_400ms.is_empty() {
            return (None, None);
        }

        let peak = self
            .window_400ms
            .iter()
            .map(|s| s.abs())
            .fold(0.0_f64, f64::max);
        let sum_sq: f64 = self.window_400ms.iter().map(|s| s * s).sum();
        let rms = (sum_sq / self.window_400ms.len() as f64).sqrt();

        if peak <= 0.0 || rms <= 0.0 {
            return (None, None);
        }

        let peak_db = 20.0 * peak.log10(); // dBFS
        let rms_db = 20.0 * rms.log10(); // dBFS

        // Crest Factor
        let crest = Some(peak_db - rms_db);

        // PSR: peak_dBFS - LUFS_S。
        // LUFS_S は 3 秒ウィンドウが揃うまで -inf を返す → None になる。
        let psr = lufs_s.map(|shortterm| peak_db - shortterm);

        (crest, psr)
    }
}

#[cfg(test)]
#[path = "engine_tests.rs"]
mod tests;
