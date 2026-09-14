#pragma once

#include "ReferenceRuntimeV2Source.h"

#include <array>
#include <memory>

namespace hypha::reference_audition
{
struct ReferenceTonalCurve
{
    std::array<float, 60> p10 {}, median {}, p90 {};
    std::array<double, 60> centersHz {};
    std::uint64_t validBits = 0;
    juce::String sourceFileSha256, sourcePcmSha256, artifactSha256;
    juce::String genreId, displayLabel;
    std::int64_t rangeStartSample = 0, rangeEndSample = 0;
};

class ReferenceTonalRepository final
{
public:
    explicit ReferenceTonalRepository (juce::File runtimeRoot) : root (std::move (runtimeRoot)) {}
    std::shared_ptr<const ReferenceTonalCurve> load (
        const RuntimeSource&, std::int64_t rangeStartSample, std::int64_t rangeEndSample) const;
    std::shared_ptr<const ReferenceTonalCurve> loadGenre (
        const juce::String& presetId, const juce::String& presetRevisionId,
        const juce::String& checkId) const;
    juce::String publicationKey() const;

private:
    juce::File root;
};
}
