#pragma once

#include <array>
#include <optional>

#include "ReferenceRuntimeV2Source.h"

namespace hypha::reference_audition
{
    using RuntimeAlignmentNullableSeries = std::vector<std::optional<std::int64_t>>;

    struct RuntimeAlignmentGrid
    {
        std::int64_t hopSamples = 0;
        std::int64_t pointCount = 0;
    };

    struct RuntimeAlignmentFeatures
    {
        std::vector<std::int64_t> onsetStrengthQ15;
        RuntimeAlignmentNullableSeries subEnergyMillidbfs;
        RuntimeAlignmentNullableSeries bassEnergyMillidbfs;
        RuntimeAlignmentNullableSeries midEnergyMillidbfs;
        RuntimeAlignmentNullableSeries highEnergyMillidbfs;
        std::array<RuntimeAlignmentNullableSeries, 12> chromaQ15;
        RuntimeAlignmentNullableSeries loudnessMillilu;
    };

    struct RuntimeAlignment
    {
        juce::String sourceFileSha256;
        juce::String sourcePcmSha256;
        RuntimeAudioFacts audio;
        RuntimeAlignmentGrid grid;
        RuntimeAlignmentFeatures features;
    };

    struct RuntimeAlignmentLoadResult
    {
        std::shared_ptr<const RuntimeAlignment> alignment;
        juce::String rejectionCode;

        bool accepted() const noexcept { return alignment != nullptr; }
    };

    class RuntimeV2AlignmentRepository final
    {
    public:
        explicit RuntimeV2AlignmentRepository (juce::File transportRootIn);
        RuntimeAlignmentLoadResult load (const RuntimeSource&) const;

    private:
        const juce::File root;
    };
}
