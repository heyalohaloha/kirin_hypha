//! Streaming STFT for PSB High extension (Bark 21-24 + 15.5k-20kHz 補完).
//!
//! specific loudness パイプラインでは扱えない (LTQ/A0/DDF/DCB が
//! 20 バンドまでしか規格化されていない)。本モジュールは FFT による
//! 独立経路で Bark 21-24 + 15.5k-20kHz 帯域のエネルギーを取得する。
//!
//! 設計上の独立性 (ISO 532-1 側への影響なし):
//! - 入力: 48 kHz mono (filter_bank と同じ入力)
//! - 処理: FFT 1024 / hop 512 / Hann window / 50% overlap
//! - 出力: 片側パワースペクトル (513 bins)
//! - ISO 532-1 関連モジュール (core_loudness / calc_slopes /
//!   nonlinear_decay / temporal_weighting / sharpness / filter_bank)
//!   とは一切相互作用しない。

use rustfft::{num_complex::Complex, Fft, FftPlanner};
use std::sync::Arc;

/// FFT 窓長 (samples). 48 kHz で 21.3 ms 窓。
/// 周波数分解能: 48000 / 1024 ≈ 46.875 Hz/bin。
pub const STFT_FFT_SIZE: usize = 1024;

/// FFT hop size (samples). 50% overlap。
pub const STFT_HOP_SIZE: usize = 512;

/// 片側スペクトル長 (DC + Nyquist 含む)。
pub const STFT_SPEC_LEN: usize = STFT_FFT_SIZE / 2 + 1;

/// PSB High 補完帯域の下限 (Hz)。Bark 24 upper と一致。
pub const PSB_HIGH_EXT_LOWER_HZ: f64 = 15500.0;

pub const PSB_HIGH_EXT_UPPER_HZ: f64 = 20000.0;

/// ストリーミング STFT プロセッサ。
///
/// 48 kHz mono サンプルを受け取り、hop ごとに片側パワースペクトル
/// `[f64; STFT_SPEC_LEN]` を返す。Hann 窓適用済み。
///
/// 最初のフレームは `STFT_FFT_SIZE` サンプル蓄積後に emit。
/// 以降は `STFT_HOP_SIZE` サンプル受け取るごとに emit。
pub struct StftProcessor {
    fft: Arc<dyn Fft<f64>>,
    window: [f64; STFT_FFT_SIZE],
    ring: [f64; STFT_FFT_SIZE],
    ring_write: usize,
    samples_since_last_frame: usize,
    first_frame: bool,
    scratch: Vec<Complex<f64>>,
}

impl StftProcessor {
    pub fn new() -> Self {
        let mut planner = FftPlanner::<f64>::new();
        let fft = planner.plan_fft_forward(STFT_FFT_SIZE);

        // Hann window (periodic, FFT 用): w[n] = 0.5 * (1 - cos(2π n / N))
        let mut window = [0.0f64; STFT_FFT_SIZE];
        for (n, w) in window.iter_mut().enumerate() {
            let t = 2.0 * std::f64::consts::PI * n as f64 / STFT_FFT_SIZE as f64;
            *w = 0.5 * (1.0 - t.cos());
        }

        Self {
            fft,
            window,
            ring: [0.0f64; STFT_FFT_SIZE],
            ring_write: 0,
            samples_since_last_frame: 0,
            first_frame: true,
            scratch: vec![Complex::<f64>::new(0.0, 0.0); STFT_FFT_SIZE],
        }
    }

    /// サンプルを受け取り、完了した FFT フレームのパワースペクトルを返す。
    ///
    /// 各出力フレームは `[f64; STFT_SPEC_LEN]` (`Vec<f64>` として返却)。
    /// power_spec[k] = |X[k]|^2 (片側スペクトル、Hann 窓適用済み)。
    pub fn push(&mut self, samples: &[f64]) -> Vec<[f64; STFT_SPEC_LEN]> {
        let mut out = Vec::new();
        for &x in samples {
            self.ring[self.ring_write] = x;
            self.ring_write = (self.ring_write + 1) % STFT_FFT_SIZE;
            self.samples_since_last_frame += 1;

            let boundary = if self.first_frame { STFT_FFT_SIZE } else { STFT_HOP_SIZE };
            if self.samples_since_last_frame >= boundary {
                self.samples_since_last_frame = 0;
                self.first_frame = false;
                out.push(self.compute_power_spectrum());
            }
        }
        out
    }

    /// 内部状態クリア (SS-8: 非Active → Active 遷移時)。
    pub fn reset(&mut self) {
        self.ring = [0.0f64; STFT_FFT_SIZE];
        self.ring_write = 0;
        self.samples_since_last_frame = 0;
        self.first_frame = true;
    }

    fn compute_power_spectrum(&mut self) -> [f64; STFT_SPEC_LEN] {
        // リングバッファを時系列順 (最古 → 最新) で読み出し、Hann 窓適用。
        // ring_write は最新サンプルの次のスロット = 最古サンプルの位置。
        let start = self.ring_write;
        for i in 0..STFT_FFT_SIZE {
            let idx = (start + i) % STFT_FFT_SIZE;
            self.scratch[i] = Complex::new(self.ring[idx] * self.window[i], 0.0);
        }
        self.fft.process(&mut self.scratch);

        let mut power = [0.0f64; STFT_SPEC_LEN];
        for (k, p) in power.iter_mut().enumerate() {
            let c = self.scratch[k];
            *p = c.re * c.re + c.im * c.im;
        }
        power
    }
}

impl Default for StftProcessor {
    fn default() -> Self {
        Self::new()
    }
}

// ── Tests ───────────────────────────────────────────────────────────

#[cfg(test)]
mod tests {
    use super::*;
    use std::f64::consts::PI;

    #[test]
    fn test_first_frame_needs_full_window() {
        let mut stft = StftProcessor::new();
        // 1023 samples: no frame yet.
        assert_eq!(stft.push(&vec![0.1; 1023]).len(), 0);
        // 1 more = 1024 total → first frame emitted.
        assert_eq!(stft.push(&[0.1]).len(), 1);
    }

    #[test]
    fn test_subsequent_frames_at_hop_rate() {
        let mut stft = StftProcessor::new();
        // First frame
        let _ = stft.push(&vec![0.1; STFT_FFT_SIZE]);
        // After first frame, each 512 samples → 1 new frame.
        assert_eq!(stft.push(&vec![0.1; 511]).len(), 0);
        assert_eq!(stft.push(&[0.1]).len(), 1);
        assert_eq!(stft.push(&vec![0.1; 512]).len(), 1);
        // Larger chunk producing multiple frames
        assert_eq!(stft.push(&vec![0.1; 1024]).len(), 2);
    }

    #[test]
    fn test_reset_clears_buffer() {
        let mut stft = StftProcessor::new();
        let _ = stft.push(&vec![0.5; 800]);
        stft.reset();
        // After reset, need full FFT_SIZE again for first frame
        assert_eq!(stft.push(&vec![0.5; STFT_FFT_SIZE - 1]).len(), 0);
        assert_eq!(stft.push(&[0.5]).len(), 1);
    }

    #[test]
    fn test_dc_input_peak_is_at_bin_0() {
        // DC × Hann window の片側パワースペクトルは理論的に
        //   bin 0: |0.5 N|² = 0.25 N² (全体の 80%)
        //   bin 1: |0.25 N|² = 0.0625 N² (全体の 20%; Hann の cos 項)
        //   その他: ~0
        // となる。bin 0 がピークであり全体の 70% 以上を保持することを確認する。
        let mut stft = StftProcessor::new();
        let frames = stft.push(&vec![1.0; STFT_FFT_SIZE]);
        assert_eq!(frames.len(), 1);
        let spec = &frames[0];
        let (peak_bin, _) = spec
            .iter()
            .enumerate()
            .max_by(|a, b| a.1.partial_cmp(b.1).unwrap())
            .unwrap();
        assert_eq!(peak_bin, 0, "DC → peak must be at bin 0");

        let total: f64 = spec.iter().sum();
        let dc_share = spec[0] / total;
        assert!(
            dc_share > 0.7,
            "DC share should be >70% (theory ~80%): got {dc_share}"
        );
    }

    #[test]
    fn test_hanning_periodic() {
        // Periodic Hann (FFT 用): w[n] = 0.5 * (1 - cos(2π n / N))
        // - w[0] = 0
        // - w[N-1] != 0 (symmetric form では w[N-1] = 0 となる点が決定的差異)
        // - w[N/2] = 1.0 (ピーク)
        // - sum(w) = N/2 (periodic Hann の厳密性質)
        let stft = StftProcessor::new();
        let w = &stft.window;
        let n = STFT_FFT_SIZE;

        assert_eq!(w[0], 0.0, "w[0] must be 0");
        assert!(
            w[n - 1] > 1e-6,
            "w[N-1] must be non-zero for periodic Hann (got {})",
            w[n - 1]
        );
        assert!(
            (w[n / 2] - 1.0).abs() < 1e-12,
            "w[N/2] must be 1.0 (got {})",
            w[n / 2]
        );
        let sum: f64 = w.iter().sum();
        assert!(
            (sum - (n as f64) / 2.0).abs() < 1e-9,
            "sum(w) must equal N/2 = {} for periodic Hann (got {})",
            n as f64 / 2.0,
            sum
        );
    }

    #[test]
    fn test_sine_energy_at_expected_bin() {
        // 7000 Hz sine at 48 kHz, FFT size 1024: bin = 7000 / (48000/1024) ≈ 149.33
        // Expect peak at bin 149 ± 1
        let fs = 48000.0;
        let freq = 7000.0;
        let n_samples = STFT_FFT_SIZE * 4;
        let signal: Vec<f64> = (0..n_samples)
            .map(|i| (2.0 * PI * freq * i as f64 / fs).sin())
            .collect();

        let mut stft = StftProcessor::new();
        let frames = stft.push(&signal);
        assert!(!frames.is_empty());

        // Use last (steady-state) frame
        let spec = frames.last().unwrap();

        // Find peak bin
        let (peak_bin, _) = spec
            .iter()
            .enumerate()
            .max_by(|a, b| a.1.partial_cmp(b.1).unwrap())
            .unwrap();

        let expected_bin = (freq / (fs / STFT_FFT_SIZE as f64)).round() as usize;
        assert!(
            peak_bin.abs_diff(expected_bin) <= 1,
            "Peak bin {} should be within 1 of expected {}",
            peak_bin,
            expected_bin
        );
    }
}
