#pragma once

#include "ReferenceKirinSpectrum.h"
#include "ReferenceRuntimeV2Measurement.h"

#include <memory>

namespace hypha::reference_audition
{
// 2026-10-06：C の Cue の値がまだ無いときの代わり（Kirin OS の曲全体のスペクトル）。A の窓と同じ定義（100 ms の区間ごとの
// p10・中央値・p90）になるのは、同じ測定の区切りが 100 ms の曲だけ（長い曲は区切りが長く、低域で数 dB・全体で 1〜2 dB
// ずれた）。区切りは同じ測定のファイルの loudness の時間軸（無ければ波形の bin）から取る（スペクトルのオブジェクトには
// キーを足さない：読み手が余分なキーを断る）。比べてよいかはここだけで決める。
enum class WholeSongSpectrum { comparable, differentDefinition, missing };

inline std::int64_t measurementHopSamples (const RuntimeDetailedMeasurement& measurement) noexcept
{
    if (measurement.loudness && measurement.loudness->hopSamples > 0) return measurement.loudness->hopSamples;
    if (measurement.waveform && measurement.waveform->framesPerBin > 0) return measurement.waveform->framesPerBin;
    return 0;
}

inline WholeSongSpectrum wholeSongSpectrum (const RuntimeDetailedMeasurement& measurement) noexcept
{
    if (! measurement.spectrum || measurement.spectrum->bandCentersHz.empty()) return WholeSongSpectrum::missing;
    const auto hop = measurementHopSamples (measurement);
    return hop > 0 && hop * 10 == measurement.audio.sampleRateHz ? WholeSongSpectrum::comparable
                                                                 : WholeSongSpectrum::differentDefinition;
}

// 比べてよいときだけ窓（ほかは null）。
inline std::shared_ptr<KirinSpectrumWindow> wholeSongWindow (const RuntimeDetailedMeasurement& measurement)
{
    if (wholeSongSpectrum (measurement) != WholeSongSpectrum::comparable) return nullptr;
    const auto& whole = *measurement.spectrum;
    const auto bands = whole.bandCentersHz.size();
    if (whole.p10Millidbfs.size() != bands || whole.medianMillidbfs.size() != bands || whole.p90Millidbfs.size() != bands)
        return nullptr;
    auto window = std::make_shared<KirinSpectrumWindow>();
    window->centersHz = whole.bandCentersHz;
    const auto toDb = [] (const std::vector<std::int64_t>& values)
    {
        std::vector<float> result;
        for (const auto value : values) result.push_back (static_cast<float> (static_cast<double> (value) / 1000.0));
        return result;
    };
    window->p10Db = toDb (whole.p10Millidbfs);
    window->medianDb = toDb (whole.medianMillidbfs);
    window->p90Db = toDb (whole.p90Millidbfs);
    window->frames = window->wantedFrames = 1;
    return window;
}
}
