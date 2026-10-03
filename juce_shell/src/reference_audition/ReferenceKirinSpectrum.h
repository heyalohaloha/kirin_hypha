#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

namespace hypha::reference_audition
{
// H12: A（ライブの入力）を、Kirin OS が参照曲の Cue に残す値（kirin_hypha_reference_ranges の
// spectrum と balance_millidbfs）と同じ定義で測る。定義が違うまま重ねると見かけの差になる（白色雑音で
// 高域が 4〜5 dB ずれる）ので、Kirin OS のコードは使わず、公開されている式だけを揃えて独立に計算する。
//  - フレーム：重ならない 100 ms（sr/10 サンプル）。そのフレームの長さの periodic Hann を掛け、
//    nextpow2(sr/6)（最小 2048）までゼロを詰めて FFT。power は両 ch の |X|² の平均。
//  - 64 帯域：中心は 20 Hz〜min(sr/2, 20 kHz) の等比。端は隣の中心との幾何平均（帯域 0 は 0 Hz から、
//    63 は Nyquist まで）で、中心に最も近い bin は必ず含める。値は帯域内の最大 bin の振幅
//    （4|X|²/(Σw)²、DC と Nyquist は ×1）の dBFS。
//  - 4 帯域 Balance：bin の中心が [20, 250)・[250, 2k)・[2k, 8k)・[8k, 20k) Hz の bin の
//    2|X|²/(N·Σw²)（DC と Nyquist は ×1）の和を RMS の dBFS にした値。
//  - 窓（C は Cue と同じ長さ、B は 10 秒）：帯域ごとにフレームの p10・中央値・p90（nearest-rank）、
//    Balance はフレームの power の平均。
// Audio Thread では動かさない（Reference の観測スレッド）。メモリは configure で先に確保する。
struct KirinSpectrumWindow
{
    std::vector<double> centersHz;
    std::vector<float> p10Db, medianDb, p90Db;
    std::array<double, 4> balanceDb { std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN(),
                                      std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::quiet_NaN() };
    int frames = 0;        // 窓に入ったフレーム（100 ms）の数
    int wantedFrames = 0;  // 窓の長さ（フレーム）
};

class KirinSpectrumMeter
{
public:
    static constexpr int bandCount = 64;
    static constexpr std::array<double, 5> balanceEdgesHz { 20.0, 250.0, 2000.0, 8000.0, 20000.0 };

    // 40 kHz〜768 kHz・1〜2 ch。maximumFrames は覚えておくフレーム数（窓の上限）。
    bool configure (int sampleRate, int channels, int maximumFrames);
    bool configuredFor (int sampleRate, int channels) const noexcept { return rate == sampleRate && channelCount == channels; }
    // 途切れ（シーク・停止・形式の変化）：途中のフレームと、覚えている窓を捨てる。
    void reset() noexcept;
    // interleaved の frames × channels（configure と同じ ch 数）。100 ms がたまるたびに 1 フレームを足す。
    void push (const float* interleaved, int frames) noexcept;
    int framesHeld() const noexcept { return held; }
    // 直近 wantedFrames フレーム（足りなければあるだけ）の要約。1 フレームも無ければ空。
    std::shared_ptr<const KirinSpectrumWindow> window (int wantedFrames) const;
    // 試験用：最後に足したフレームの 64 帯域（dBFS）と 4 帯域 Balance（dBFS）。
    bool lastFrame (std::array<float, bandCount>& bands, std::array<double, 4>& balance) const noexcept;
    const std::vector<double>& centersHz() const noexcept { return centers; }

private:
    void transform() noexcept;
    void fft() noexcept;

    int rate = 0, channelCount = 0, frameLength = 0, size = 0, capacity = 0;
    std::vector<double> centers, hann;
    std::vector<int> firstBin, lastBin, balanceBand;
    double hannSum = 0.0, hannSquared = 0.0;
    std::vector<std::vector<double>> pending;
    int pendingCount = 0;
    std::vector<std::complex<double>> spectrum, twiddle;
    std::vector<int> reversed;
    std::vector<double> power;
    std::vector<float> levels;          // capacity × 64（dBFS）
    std::vector<double> balancePower;   // capacity × 4（mean square）
    int head = 0, held = 0;
};
}
