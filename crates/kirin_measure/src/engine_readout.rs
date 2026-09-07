//! Readout only; the input transaction and canonical observer cadence stay in engine.rs.
use super::*;

impl MeasureEngine {
    pub(super) fn compute(&self) -> MeasureResult {
        // ── LUFS-M (ITU-R BS.1770-4 Momentary, 400ms sliding) ───────────
        // ebur128 が内部で 400ms ウィンドウを管理する。
        // 未入力部分はゼロとして窓に含まれる。無音の -inf は None にする。
        // B-205: is_finite に加えサブサイレンス・フロアを要求（フロア未満は無信号扱い → None → ---）。
        let raw_momentary = self
            .ebu
            .loudness_momentary_cached()
            .ok()
            .filter(|v| v.is_finite());
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
        // S もゼロ埋めの未入力部分を含む。サブサイレンス・フロア以下は None。
        let raw_shortterm = self
            .ebu
            .loudness_shortterm_cached()
            .ok()
            .filter(|v| v.is_finite());
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
        // 無音やフロア以下では LUFS_S が None のため PSR も None になる。
        let psr = lufs_s.map(|shortterm| peak_db - shortterm);

        (crest, psr)
    }
}
