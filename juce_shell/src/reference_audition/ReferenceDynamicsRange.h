#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

#include "ReferenceRuntimeV2Measurement.h"
#include "kirin_hypha_reference_visual_ffi.h"

namespace hypha::reference_audition
{
// 2026-10-04（Daisuke が「範囲の帯で比べる」を選んだ）：Dynamics・Loudness・Stereo・Waveform・Transient の Check を、
// A と比べる側（C・V）の区間ごとの値の p10・中央値・p90 で比べる。値は Kirin OS（native/src/reference_analysis.rs）と
// 同じ定義：
//  - 区間（hop）は 100 ms の整数倍。A は VisualMeter の 100 ms の bin を、比べる側の hop にまとめ直して出す。
//  - クレスト：20·log10(TP / RMS)。TP は区間の true peak（100 ms ごとの prev_true_peak の最大）、RMS は全 ch。
//  - LUFS-M・LUFS-S：区間の終わりの値。始めてから 0.4 秒・3 秒までは無い。
//  - 相関：Σlr / √(ΣL²·ΣR²)（−1〜1）。幅：√(S²の平均) / (√(M²の平均) + 1e−10) × 100（150 % まで）。M = (L+R)/2、S = (L−R)/2。
//  - ピーク：区間の sample peak の大きい ch（dBFS）。RMS：全 ch の電力の平均（dBFS）。
//  - 立ち上がり：max(0, RMS − 前の区間の RMS) / RMS（0〜1。Kirin OS の onset_strength_q15 / 32767）。
// 測れない区間は NaN（無音・mono の相関と幅・始めの LUFS）。
enum class DynamicsFact : std::size_t { crest, lufsM, lufsS, width, correlation, peak, rms, onset, count };

struct DynamicsHops
{
    std::array<std::vector<double>, static_cast<std::size_t> (DynamicsFact::count)> values;
    std::int64_t hopSamples = 0;  // 比べる側の元の sample（A は A の sample）での区間の長さ
    std::size_t size() const noexcept { return values[0].size(); }
    const std::vector<double>& operator[] (DynamicsFact fact) const noexcept { return values[static_cast<std::size_t> (fact)]; }
    std::vector<double>& operator[] (DynamicsFact fact) noexcept { return values[static_cast<std::size_t> (fact)]; }
};

// A：100 ms の bin（古い順）を binsPerHop ずつまとめる。最後の足りない端は捨てる（区間を短くしない）。
DynamicsHops aggregateHops (const KirinReferenceVisualBin* bins, std::size_t count, int binsPerHop, int channels);

// C・V：Kirin OS の詳しい値を、元の曲の [startSample, endSample) に入る区間で切り出す（区間が丸ごと入るもの）。
DynamicsHops kirinHops (const RuntimeDetailedMeasurement&, std::int64_t startSample, std::int64_t endSample);

struct FactRange
{
    double p10 = std::numeric_limits<double>::quiet_NaN();
    double median = std::numeric_limits<double>::quiet_NaN();
    double p90 = std::numeric_limits<double>::quiet_NaN();
    int count = 0;
    bool valid() const noexcept { return count > 0; }
};

// NaN を除いた値の p10・中央値・p90（nearest-rank：Kirin OS の Cue の集計と同じ並べ方）。
FactRange rangeOf (const std::vector<double>&);

// A の 100 ms の bin を直近 60 秒まで貯める（Reference の観測スレッドだけ。Audio Thread では動かさない）。
// 途切れ（シーク・停止・形式の変化・壊れた値）では捨てて、新しい計器で数え直す（LUFS の窓を前の音から続けない）。
class DynamicsTicks
{
public:
    static constexpr int capacity = 600;
    DynamicsTicks() = default;
    ~DynamicsTicks();
    DynamicsTicks (const DynamicsTicks&) = delete;
    DynamicsTicks& operator= (const DynamicsTicks&) = delete;
    // interleaved の frames × channels（1〜2 ch、8 kHz〜768 kHz）。レート・ch 数が変われば作り直す。
    void push (const float* interleaved, int frames, int rate, int channels);
    void reset() noexcept;
    // 古い順の bin（100 ms ごとに新しくなる。無ければ null）。
    std::shared_ptr<const std::vector<KirinReferenceVisualBin>> bins() const noexcept { return published; }
    int channels() const noexcept { return meterChannels; }

private:
    KirinReferenceVisualMeter* meter = nullptr;
    int meterRate = 0, meterChannels = 0, fill = 0;
    std::vector<KirinReferenceVisualBin> held;
    std::shared_ptr<const std::vector<KirinReferenceVisualBin>> published;
};
}
