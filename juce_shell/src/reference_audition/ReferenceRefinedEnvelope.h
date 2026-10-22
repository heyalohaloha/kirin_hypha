#pragma once
#include "ReferenceContentCorrelation.h"
#include "ReferenceProbeAudio.h"
#include "ReferenceRuntimeACapture.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>

namespace hypha::reference_audition
{
struct RefinedReferenceEnvelope
{
    std::int64_t position = 0, spread = 0;
    double minimumCorrelation = 1.0, minimumAmbiguity = 300.0;
    ContentEnvelopeCorrelation identity;
    std::vector<float> probe;
};

// Worker only. A new musical layer can weaken waveform identity while still
// leaving precise timing in three separated passages. Such timing is only a
// proposal: the complete, aligned multi-band identity must independently pass.
inline std::optional<RefinedReferenceEnvelope> refineReferenceEnvelope (
    const RuntimeACaptureAudio& a, juce::AudioFormatReader& reader, std::int64_t position,
    std::int64_t window, int lag, int channel, const std::array<std::int64_t, 3>& starts)
{
    RefinedReferenceEnvelope result;
    std::array<std::int64_t, 3> offsets {};
    for (size_t index = 0; index < starts.size(); ++index)
    {
        const auto first = starts[index];
        const auto sourceFirst = position + static_cast<std::int64_t> (std::llround (
            static_cast<double> (first) * reader.sampleRate / a.sampleRateHz));
        std::vector<float> raw, left (static_cast<size_t> (window)), right (static_cast<size_t> (window));
        if (!readReferenceProbe (reader, sourceFirst, window, static_cast<int> (a.sampleRateHz), a.channels, raw))
            return std::nullopt;
        for (std::int64_t frame = 0; frame < window; ++frame)
        {
            left[static_cast<size_t> (frame)] = a.interleaved[static_cast<size_t> ((first + frame) * a.channels + channel)];
            right[static_cast<size_t> (frame)] = raw[static_cast<size_t> (frame * a.channels + channel)];
        }
        const auto useful = [lag] (const ContentCorrelation& proof) {
            return proof.correlation >= 0.30 && proof.ambiguityDb >= 1.5 && std::abs (proof.offsetSamples) < lag;
        };
        auto timing = correlateReferenceContent (left, right, static_cast<int> (a.sampleRateHz), lag);
        const auto detail = correlateReferenceContent (left, right, static_cast<int> (a.sampleRateHz), lag, true);
        if (useful (detail) && (!useful (timing) || std::abs (detail.offsetSamples - timing.offsetSamples) <= 2))
            timing = detail;
        if (!useful (timing)) return std::nullopt;
        offsets[index] = timing.offsetSamples;
        result.minimumCorrelation = std::min (result.minimumCorrelation, timing.correlation);
        result.minimumAmbiguity = std::min (result.minimumAmbiguity, timing.ambiguityDb);
    }
    std::sort (offsets.begin(), offsets.end());
    result.spread = offsets.back() - offsets.front();
    if (result.spread > std::max<std::int64_t> (2, a.sampleRateHz / 20000)) return std::nullopt;
    result.position = position + static_cast<std::int64_t> (std::llround (
        static_cast<double> (offsets[1]) * reader.sampleRate / a.sampleRateHz));
    if (!readReferenceProbe (reader, result.position, a.frameCount,
                             static_cast<int> (a.sampleRateHz), a.channels, result.probe)) return std::nullopt;
    result.identity = correlateReferenceEnvelope (a.interleaved, result.probe, static_cast<int> (a.sampleRateHz), a.channels);
    return result.identity.accepted ? std::optional<RefinedReferenceEnvelope> { std::move (result) } : std::nullopt;
}
}
