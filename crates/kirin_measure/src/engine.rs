//! リアルタイム計測エンジン。
//!
//! Measure Thread が呼び出す。Audio Thread のリングバッファから受け取った
//! インターリーブ f64 サンプルを ebur128 と独自スライディングウィンドウで処理し、
//! 4項目（LUFS-M / True Peak / Crest / PSR）を 100ms 単位で更新する。

use ebur128::{EbuR128, Mode};
use std::collections::VecDeque;

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
    // 400ms = sample_rate × 0.4 フレーム（per-channel）。
    (sample_rate as u64) * 4 / 10
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

    /// ebur128 への投入をまとめる 100ms 積算バッファ（インターリーブ f64）。
    /// 100ms 分の要素数に達したら ebur128 に add_frames_f64() する。
    accum: Vec<f64>,
    /// 100ms 分の要素数（sample_rate / 10 × n_channels）
    accum_target: usize,

    /// B-078: ebur128 投入用の再利用バッファ（事前確保）。100ms チャンクごとの
    /// `drain().collect()` 新 Vec 確保を排し、`clear()` + `extend` で使い回す。
    /// Measure Thread 上の alloc churn を除去（処理データ列は同一＝計測値 byte-identical）。
    chunk_buf: Vec<f64>,

    /// add_frames 済みの累積フレーム数（per-channel）。`tp_window` の失効判定に使う
    /// サンプル基準の時刻（B-074: wall-clock `Instant` を置換）。reset() では維持し、
    /// 相対距離で窓を判定する（窓側は reset() でクリアされる）。
    total_frames: u64,

    /// True Peak「直近」窓（B-074: フレーム基準）。
    ///
    /// エントリ = (per-chunk inter-sample true_peak linear値, 記録時点の total_frames)。
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
            chunk_buf: Vec::with_capacity(accum_target),
            total_frames: 0,
            // 最大 4 エントリ（400ms / 100ms）。フレーム基準で失効するので容量は余裕を持つ。
            tp_window: VecDeque::with_capacity(8),
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
        let chunk_frames = (self.accum_target / self.n_channels) as u64;
        let mut result: Option<MeasureResult> = None;
        while self.accum.len() >= self.accum_target {
            // B-078: 再利用バッファへ drain（per-chunk の新 Vec 確保を排す / Measure Thread）。
            // 処理する f64 列は drain().collect() と同一順・同一値 → 計測 byte-identical。
            self.chunk_buf.clear();
            self.chunk_buf.extend(self.accum.drain(..self.accum_target));
            // B-078: add_frames_f64 失敗を沈黙させない。正常運用では frames が n_channels で
            // 割り切れ mode も有効なので発生しないが、NoMem 等で失敗した場合この 100ms チャンクは
            // 計測に入らない（欠落相当）。UI には出さず log で可視化する（R-28 / integrity 思想）。
            if let Err(e) = self.ebu.add_frames_f64(&self.chunk_buf) {
                log::warn!(
                    "[engine] add_frames_f64 failed ({:?}): this 100ms chunk is not measured (frames lost)",
                    e
                );
            }

            // フレーム基準の時刻を進めてから、prev_true_peak をタイムスタンプ付きで窓に追加。
            // prev_true_peak は直近 add_frames チャンク内のピークのみを返す（running max でない）。
            self.total_frames += chunk_frames;
            let chunk_tp = (0..self.n_channels as u32)
                .filter_map(|ch| self.ebu.prev_true_peak(ch).ok())
                .fold(0.0_f64, f64::max);
            self.tp_window.push_back((chunk_tp, self.total_frames));

            // フレーム基準で 400ms より古いエントリを前から失効させる。
            while let Some(&(_, f)) = self.tp_window.front() {
                if self.total_frames - f >= self.tp_window_frames {
                    self.tp_window.pop_front();
                } else {
                    break;
                }
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

        max_tp_lin.and_then(|v| {
            if v > 0.0 {
                Some(20.0 * v.log10())
            } else {
                None
            }
        })
    }

    fn compute(&self) -> MeasureResult {
        // ── LUFS-M (ITU-R BS.1770-4 Momentary, 400ms sliding) ───────────
        // ebur128 が内部で 400ms ウィンドウを管理する。
        // 400ms 未満の場合は -inf を返すので filter(is_finite) で None にする。
        let lufs_m = self.ebu.loudness_momentary().ok().filter(|v| v.is_finite());

        // ── True Peak「直近」tp_recent (ITU-R BS.1770-4 Annex 2, 4× oversampling) ──
        // フレーム基準で直近 400ms 以内（LUFS-M と同窓）のエントリのみを最大化する（B-074）。
        // - 通常再生中: LUFS-M と同じ 400ms 窓を実現
        // - 高速/offline bounce: 窓は処理済みフレーム基準なので音声時間と一致（wall-clock 依存なし）
        // - transport 停止 → 再開: reset() が tp_window をクリアし停止前ピークを持ち越さない
        // 有効エントリ 0 件（無音 / reset 直後）は明示的に None（---）を返す。
        let valid_tp: f64 = self
            .tp_window
            .iter()
            .filter(|(_, f)| self.total_frames - f < self.tp_window_frames)
            .map(|(v, _)| *v)
            .fold(f64::NEG_INFINITY, f64::max);

        let tp_recent = if valid_tp.is_finite() && valid_tp > 0.0 {
            Some(20.0 * valid_tp.log10())
        } else {
            None // 有効エントリ 0 件（失効済み）または無音 → ---
        };

        // ── True Peak「セッション最大」tp_session_max（正本・Record/.kirin と同一定義）──
        // ebur128 の running max（init=reset 以降）。Watch でも live に見えるよう毎 compute 算出。
        let tp_session_max = self.session_true_peak_dbtp();

        // ── Crest Factor + PSR ───────────────────────────────────────────
        let (crest, psr) = self.compute_crest_psr();

        MeasureResult {
            lufs_m,
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
    fn compute_crest_psr(&self) -> (Option<f64>, Option<f64>) {
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
        let psr = self
            .ebu
            .loudness_shortterm()
            .ok()
            .filter(|v| v.is_finite())
            .map(|lufs_s| peak_db - lufs_s);

        (crest, psr)
    }
}

#[cfg(test)]
mod tp_recent_golden {
    //! B-074: tp_recent（400ms / frame 基準）と tp_session_max（running max）の挙動を
    //! 既知信号で実証する。期待値は信号定義からの理論値（帯域制限正弦の連続ピーク = 振幅）。
    use super::*;

    const SR: u32 = 48_000;

    /// 1000Hz 正弦 @ amp の 100ms チャンク（interleaved stereo L=R）。100ms@1kHz=100 周期で
    /// 末尾は zero-crossing（FIR 残差を最小化）。
    fn sine_100ms(amp: f64) -> Vec<f64> {
        let frames = (SR / 10) as usize; // 4800
        let mut v = Vec::with_capacity(frames * 2);
        for i in 0..frames {
            let t = i as f64 / SR as f64;
            let s = amp * (2.0 * std::f64::consts::PI * 1000.0 * t).sin();
            v.push(s);
            v.push(s);
        }
        v
    }

    fn silence_100ms() -> Vec<f64> {
        vec![0.0; (SR / 10) as usize * 2]
    }

    /// burst → 無音 で tp_recent が 400ms 窓で失効し、tp_session_max は hold し続ける。
    /// 同期 fast test 内でも frame 基準で chunk 4（=400ms）に失効する（wall-clock 2s 窓なら
    /// 微秒スケールの本 test では失効せず hold し続けるはず → frame 基準の実証）。
    #[test]
    fn tp_recent_expires_in_400ms_while_session_holds() {
        let amp: f64 = 0.5; // -6.02 dBFS
        let expected = 20.0 * amp.log10(); // ≈ -6.02 dBTP（正弦の連続ピーク = 振幅）

        let mut eng = MeasureEngine::new(SR, 2).unwrap();
        eng.reset();

        let mut chunks: Vec<Vec<f64>> = vec![sine_100ms(amp)];
        for _ in 0..5 {
            chunks.push(silence_100ms());
        }

        let mut recent: Vec<Option<f64>> = Vec::new();
        let mut session: Vec<Option<f64>> = Vec::new();
        for c in &chunks {
            if let Some(r) = eng.push(c) {
                recent.push(r.true_peak);
                session.push(r.tp_session_max);
            }
        }
        assert_eq!(recent.len(), 6, "1 result per 100ms chunk");

        // (a) burst 直後〜400ms 窓内（chunk 0..=3）: tp_recent = burst peak（理論 ±0.5 dBTP）。
        for (i, r) in recent.iter().take(4).enumerate() {
            let v = r.unwrap_or_else(|| panic!("recent[{i}] None (expected in 400ms window)"));
            assert!(
                (v - expected).abs() < 0.5,
                "recent[{i}]={v} expected≈{expected} dBTP (burst held in 400ms window)"
            );
        }
        // (b) burst が 400ms 窓を外れた後: tp_recent は burst peak を hold せず「窓内 max へ低下」。
        //     chunk 4（burst entry が frame dist 19200 で失効）: 窓内 max は burst 直後の FIR 残差
        //     （burst peak より低い）に落ちる → burst peak を hold しないことを判定（>2dB 低下）。
        assert!(
            recent[4].is_none_or(|v| v < expected - 2.0),
            "recent[4]={:?} must drop below burst {expected} (burst expired from 400ms window)",
            recent[4]
        );
        //     chunk 5: burst 直後 FIR 残差も 400ms 窓外 → 完全失効（clean silence のみ）→ None。
        assert!(
            recent[5].is_none(),
            "recent[5]={:?} must be None (all burst energy out of 400ms window)",
            recent[5]
        );
        // 対比: tp_session_max は running max なので全 chunk で burst peak を hold する。
        for (i, s) in session.iter().enumerate() {
            let v = s.unwrap_or_else(|| panic!("session[{i}] None"));
            assert!(
                (v - expected).abs() < 0.5,
                "session_max[{i}]={v} must HOLD burst peak ≈{expected} (running max, contrast with recent)"
            );
        }
    }

    /// frame 基準の実証: 同信号を異なる push ブロックサイズで通しても tp_recent 時系列が一致
    /// （内部 100ms チャンク処理は供給ブロック粒度に非依存 = offline/realtime 非依存）。
    #[test]
    fn tp_recent_independent_of_push_block_size() {
        let amp = 0.5;
        let mut sig: Vec<f64> = sine_100ms(amp);
        for _ in 0..5 {
            sig.extend(silence_100ms());
        }

        let drive = |block_frames: usize| -> Vec<Option<f64>> {
            let mut eng = MeasureEngine::new(SR, 2).unwrap();
            eng.reset();
            let mut out = Vec::new();
            let block = block_frames * 2; // interleaved
            let mut i = 0;
            while i < sig.len() {
                let e = (i + block).min(sig.len());
                if let Some(r) = eng.push(&sig[i..e]) {
                    out.push(r.true_peak);
                }
                i = e;
            }
            out
        };

        let a = drive(4800); // 100ms blocks (1 chunk per push)
        let b = drive(1200); // 25ms blocks (4 pushes per 100ms chunk)
        assert_eq!(
            a.len(),
            b.len(),
            "same number of 100ms results regardless of push block size"
        );
        for (i, (x, y)) in a.iter().zip(b.iter()).enumerate() {
            match (x, y) {
                (Some(xv), Some(yv)) => assert!(
                    (xv - yv).abs() < 1e-9,
                    "tp_recent differs by push block size @ {i}: {xv} vs {yv} (frame-base broken)"
                ),
                (None, None) => {}
                _ => panic!("tp_recent None mismatch by block size @ {i}: {x:?} vs {y:?}"),
            }
        }
    }
}

#[cfg(test)]
mod add_frames_integrity {
    //! B-078: add_frames_f64 失敗を沈黙させず log 化した（旧: `let _`）。ここでは ebur128 が
    //! 実際に Err を返す条件（n_channels で割り切れない frame 長 = Interleaved::new が NoMem）を
    //! 確認し、その Err が観測可能（沈黙でなく検出対象）であることを実証する。engine.push は
    //! accum_target（常に n_channels の倍数）のみ投入するため通常運用では発生しない。
    use ebur128::{EbuR128, Mode};

    #[test]
    fn add_frames_f64_errors_on_misaligned_frame_count_is_observable() {
        let mut ebu = EbuR128::new(2, 48_000, Mode::M).unwrap();
        // 2ch で 3 要素 = 割り切れない → Err（B-078 はこれをログ化 / 旧 let _ は沈黙）。
        assert!(
            ebu.add_frames_f64(&[0.0_f64; 3]).is_err(),
            "misaligned frame count must be an observable Err (now logged, not silently dropped)"
        );
        // 整列長（4 要素 = 2 frames）は Ok。
        assert!(ebu.add_frames_f64(&[0.0_f64; 4]).is_ok());
    }
}
