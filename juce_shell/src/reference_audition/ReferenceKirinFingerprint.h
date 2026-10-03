#pragma once

#include <array>
#include <complex>
#include <cstdint>
#include <vector>

#include <juce_core/juce_core.h>

namespace hypha::reference_audition
{
// H7: Kirin 指紋（kirin_hypha_reference_ranges の fingerprint、K6 の kirin_chroma_sign_v1）と同じ定義で A を
// 測り、Version の指紋と照合する。Kirin OS のコードは使わず、公開されている式だけを揃えて独立に計算する。
//  - 100 ms ごと（sr/10 サンプル）に、直近 N = nextpow2(sr/6)（最小 2048）サンプルの左右の平均に periodic
//    Hann を掛けた FFT。55 Hz〜5 kHz の bin の power を、最も近い 2 つの音名（C = 0、A4 = 440 Hz）へ半音の
//    距離で線形に分ける（クロマ）。
//  - 同じ区切りで LUFS-M（BS.1770 の K 特性、400 ms。400 ms に満たないあいだは無し）。
//  - ビット p：その区切りを中心とする 0.5 秒（前後 2 区切り）のクロマの和の対数が、12 音名の平均より大きい。
//    音量は 0.5 LU 刻み（−70 以下と無しは −70）。
//  - 照合：両方が −50 LUFS より大きい区切りで、ずれ（±30 秒）ごとにビットの一致率（0.5 秒ごと → 最良の
//    前後 0.4 秒を 100 ms ごと）。一致率が最良から 0.01 以内のずれでは音量の流れの相関の高い方を取る。
//    一致率 0.84 以上かつ相関 0.95 以上は「ほぼ同じ音源」、0.70 以上か 0.62 以上かつ相関 0.5 以上は「同じ曲」。
// Audio Thread では動かさない（Reference の観測スレッドと、メッセージスレッドの照合）。
struct KirinFingerprint
{
    std::vector<std::uint16_t> bits;  // 区切りごとの下位 12 ビット
    std::vector<float> lufs;          // 区切りごとの LUFS-M（0.5 LU 刻み、−70 以下と無しは −70）
};

struct FingerprintMatch
{
    enum class Relation { unknown, different, sameSong, nearIdentical };
    Relation relation = Relation::unknown;
    double agreement = 0.0, loudnessCorrelation = 0.0;
    int offsetTicks = 0;  // b の t + offset に、a の t と同じ内容がある
};

FingerprintMatch compareFingerprints (const KirinFingerprint& a, const KirinFingerprint& b);
// 同じ照合をずれの範囲を指定して（b の t + offset に a の t、offset は [minimumOffset, maximumOffset]）。
// V の自動特定で、曲が DAW の時間軸のどこにあっても探すために使う（しきい値・刻み・同点の扱いは同じ）。
FingerprintMatch compareFingerprints (const KirinFingerprint& a, const KirinFingerprint& b, int minimumOffset, int maximumOffset);
// ranges の chroma_signs（区切りごとの 16 bit、リトルエンディアン）と loudness（1 バイト）から。
KirinFingerprint decodeFingerprint (const juce::MemoryBlock& chromaSigns, const juce::MemoryBlock& loudness, std::int64_t ticks);
// 区切りごとのクロマ（power）と LUFS-M から、Kirin OS と同じ並べ方の指紋を作る。
KirinFingerprint fingerprintFrom (const std::vector<std::array<double, 12>>& chroma, const std::vector<double>& lufs);

class KirinFingerprintMeter
{
public:
    bool configure (int sampleRate, int channels, int maximumTicks);
    bool configuredFor (int sampleRate, int channels) const noexcept { return rate == sampleRate && channelCount == channels; }
    void reset() noexcept;
    void push (const float* interleaved, int frames) noexcept;
    int ticksHeld() const noexcept { return held; }
    int pendingSamples() const noexcept { return sinceTick; }  // 最後の区切りの後に入ったサンプル数
    // 直近 ticks 区切りの指紋（端は使えるだけの前後で和を取る）。
    KirinFingerprint fingerprint (int ticks) const;
    // 試験用：最後の区切りのクロマ（10·log10 power、dB）と LUFS-M（無ければ NaN）。
    bool lastTick (std::array<double, 12>& chromaDb, double& lufs) const noexcept;

private:
    void tick() noexcept;
    void fft() noexcept;
    double filter (int channel, double sample) noexcept;

    int rate = 0, channelCount = 0, tickLength = 0, size = 0, capacity = 0;
    std::vector<double> ring, hann;
    int ringWrite = 0, sinceTick = 0;
    std::vector<std::complex<double>> spectrum, twiddle;
    std::vector<int> reversed;
    struct Weight { int bin, pitch; double lower, upper; };
    std::vector<Weight> weights;
    // K 特性（前段の shelf と RLB の high-pass、ch ごとの状態）と 400 ms の二乗の和。
    std::array<double, 5> shelfB {}, shelfA {}, passB {}, passA {};
    std::vector<std::array<double, 4>> shelfState, passState;
    std::vector<double> squares;  // 400 ms ぶんの、ch を足した K 特性の二乗
    int squaresWrite = 0, squaresHeld = 0;
    std::vector<std::array<double, 12>> chroma;  // capacity 区切りの輪
    std::vector<double> loudness;
    int head = 0, held = 0;
};
}
