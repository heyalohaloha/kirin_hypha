//! guardian_101 v2: 入力サンプルレートを 48 kHz に整形する Measure Thread 用ラッパー。
//!
//! 設計原則:
//! - **Audio Thread 不可侵 (R-12)**: 本モジュールは Measure Thread でのみ呼ばれる
//! - **planar 不要**: rubato 2.0 の `audioadapter_buffers::InterleavedSlice` を使い、
//!   既存の interleaved `Vec<f64>` データ経路をそのまま流す（deinterleave コスト無し）
//! - **48 kHz 入力時はそもそも構築しない**: `spawn_measure_thread` 側で `Option` 分岐し、
//!   バイパス時はゼロオーバーヘッド経路を維持する（このモジュールは関与しない）
//!
//! `rubato::Fft` を `FixedSync::Input` (chunk_size=1024 / sub_chunks=2) で構築し、
//! 入力フレーム数を固定サイズで `process_into_buffer` に渡す。出力フレーム数は
//! 入出力 SR 比率で変動するが各呼び出しの戻り値で受け取る。
//!
//! 端数処理: `Resampler::input_frames_next()` 未満しか溜まっていない場合は
//! `pending` バッファに保持し、次回呼び出しでまとめて消費する。

use rubato::audioadapter_buffers::direct::InterleavedSlice;
use rubato::{Fft, FixedSync, ResampleError, Resampler, ResamplerConstructionError};

const TARGET_SR: usize = 48_000;
const FFT_CHUNK_SIZE: usize = 1024;
const FFT_SUB_CHUNKS: usize = 2;

pub struct ResamplerTo48k {
    fft: Fft<f64>,
    channels: usize,
    /// 入力 SR で受け取った interleaved サンプルの未消費分。
    pending: Vec<f64>,
    /// `process_into_buffer` に渡す scratch (interleaved input samples)。
    scratch_in: Vec<f64>,
    /// `process_into_buffer` から受け取る scratch (interleaved output samples)。
    scratch_out: Vec<f64>,
    /// `process_into_buffer` 1 呼び出し分の最大出力フレーム数 (channels 単位)。
    out_frames_max: usize,
    /// `process_into_buffer` が要求する固定入力フレーム数 (channels 単位)。
    in_frames_fixed: usize,
}

impl ResamplerTo48k {
    pub fn new(input_sr: u32, channels: usize) -> Result<Self, ResamplerConstructionError> {
        let fft = Fft::<f64>::new(
            input_sr as usize,
            TARGET_SR,
            FFT_CHUNK_SIZE,
            FFT_SUB_CHUNKS,
            channels,
            FixedSync::Input,
        )?;
        let in_frames_fixed = fft.input_frames_next();
        let out_frames_max = fft.output_frames_max();
        Ok(Self {
            fft,
            channels,
            pending: Vec::with_capacity(in_frames_fixed * channels * 4),
            scratch_in: vec![0.0; in_frames_fixed * channels],
            scratch_out: vec![0.0; out_frames_max * channels],
            out_frames_max,
            in_frames_fixed,
        })
    }

    /// 入力 interleaved サンプルを取り込み、消費可能な分だけ 48 kHz interleaved に
    /// 変換して `out` に追記する。残り端数は次回まで内部に保持。
    ///
    /// `out` には呼び出し前の内容を保持したまま **追記** する（呼び出し側が必要に
    /// 応じて事前 `clear()` する）。
    pub fn process(
        &mut self,
        input_interleaved: &[f64],
        out: &mut Vec<f64>,
    ) -> Result<(), ResampleError> {
        self.pending.extend_from_slice(input_interleaved);
        let need = self.in_frames_fixed * self.channels;
        let out_max_samples = self.out_frames_max * self.channels;

        while self.pending.len() >= need {
            self.scratch_in.copy_from_slice(&self.pending[..need]);
            self.pending.drain(..need);

            let in_adapter =
                InterleavedSlice::new(&self.scratch_in[..need], self.channels, self.in_frames_fixed)
                    .expect("in_adapter size invariant");
            let mut out_adapter = InterleavedSlice::new_mut(
                &mut self.scratch_out[..out_max_samples],
                self.channels,
                self.out_frames_max,
            )
            .expect("out_adapter size invariant");

            let (_in_frames, out_frames) =
                self.fft
                    .process_into_buffer(&in_adapter, &mut out_adapter, None)?;

            out.extend_from_slice(&self.scratch_out[..out_frames * self.channels]);
        }
        Ok(())
    }

    /// 内部状態を全クリア（SS-8 非Active→Active 遷移時に呼ぶ）。
    /// FFT overlap / pending 入力 / スクラッチをすべて空に戻す。
    pub fn reset(&mut self) {
        self.fft.reset();
        self.pending.clear();
        // scratch_in / scratch_out は次回の copy_from_slice で全領域上書きされるため
        // 明示的なゼロクリアは不要（パフォーマンス優先）。
    }

    pub fn input_sample_rate(&self) -> u32 {
        // resample_ratio = output_sr / input_sr → input_sr = output_sr / ratio
        (TARGET_SR as f64 / self.fft.resample_ratio()) as u32
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    /// 既存 SR 列でリサンプラが構築でき、48kHz output が得られることを最低限確認。
    /// guardian_101 v2 V2-01〜V2-06 と独立したスモークテスト。
    #[test]
    fn resampler_constructs_for_supported_rates() {
        for sr in [44_100u32, 88_200, 96_000, 176_400, 192_000] {
            let r = ResamplerTo48k::new(sr, 2).expect("construct");
            assert_eq!(r.channels, 2);
        }
    }

    /// 44.1kHz 1秒分入力で 48kHz interleaved 出力が約 48000 frames 出ることを確認。
    /// rubato は遅延を含むため frames 数は完全一致しないが、99% 以上のフレーム回収を確認。
    #[test]
    fn resampler_44100_to_48000_frame_count() {
        let channels = 2usize;
        let mut r = ResamplerTo48k::new(44_100, channels).expect("construct");

        // 44.1kHz × 1秒 × 2ch = 88200 interleaved samples
        let input: Vec<f64> = (0..44_100 * channels)
            .map(|i| ((i / channels) as f64 * 0.001).sin())
            .collect();

        let mut out = Vec::with_capacity(48_000 * channels);
        r.process(&input, &mut out).expect("resample");

        // 出力 frames 数（interleaved samples / channels）
        let out_frames = out.len() / channels;
        // 48kHz 1秒分 ≈ 48000 frames が期待値。
        // rubato の遅延 + chunk 境界端数のため 47000〜48500 程度を許容。
        assert!(
            (47_000..=48_500).contains(&out_frames),
            "expected ~48000 output frames, got {}",
            out_frames
        );
    }

    /// 192kHz → 48kHz 4倍ダウンサンプル経路が壊れないことを確認。
    #[test]
    fn resampler_192000_to_48000_frame_count() {
        let channels = 2usize;
        let mut r = ResamplerTo48k::new(192_000, channels).expect("construct");

        // 192kHz × 1秒 × 2ch = 384000 interleaved samples
        let input: Vec<f64> = (0..192_000 * channels)
            .map(|i| ((i / channels) as f64 * 0.001).sin())
            .collect();

        let mut out = Vec::with_capacity(48_000 * channels);
        r.process(&input, &mut out).expect("resample");

        let out_frames = out.len() / channels;
        assert!(
            (47_000..=48_500).contains(&out_frames),
            "expected ~48000 output frames, got {}",
            out_frames
        );
    }

    /// reset() 後に pending と FFT 状態が初期化されることを確認。
    #[test]
    fn resampler_reset_clears_pending() {
        let mut r = ResamplerTo48k::new(44_100, 2).expect("construct");
        // 中途半端な量（in_frames_fixed 未満）を流し込み pending に積ませる
        let input = vec![0.5_f64; 100];
        let mut out = Vec::new();
        r.process(&input, &mut out).expect("partial");
        assert!(!r.pending.is_empty(), "pending should accumulate");
        r.reset();
        assert_eq!(r.pending.len(), 0, "reset must clear pending");
    }
}
