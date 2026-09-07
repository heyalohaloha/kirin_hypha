#pragma once

#include <optional>

#include "ReferenceRuntimeV2Model.h"

namespace hypha::reference_audition
{
    struct RuntimeFileRevision
    {
        juce::String deviceId;
        juce::String fileId;
        juce::String sizeBytes;
        juce::String modifiedTimeNs;
        juce::String changedTimeNs;
    };

    struct RuntimeAudioFacts
    {
        std::int64_t sampleRateHz = 0;
        std::int64_t channels = 0;
        std::int64_t totalSampleFrames = 0;
    };

    struct RuntimeMeasurementSummary
    {
        juce::String measuredAt;
        std::optional<double> loudnessLufsI;
        std::optional<double> maximumTruePeakDbtp;
        std::optional<double> loudnessRangeLu;
        std::optional<double> meanPsrDb;
        std::optional<double> crestFactorDb;
        std::optional<double> stereoWidthPercent;
    };

    struct RuntimeSource
    {
        juce::String sourceKind;
        juce::String sourceIdentityKey;
        juce::String sourceFileSha256;
        juce::String sourcePcmSha256;
        juce::String absolutePath;
        RuntimeFileRevision revision;
        RuntimeAudioFacts audio;
        std::optional<RuntimeMeasurementSummary> measurementSummary;
        std::optional<RuntimeContentReceipt> measurementArtifact;
        std::optional<RuntimeContentReceipt> alignmentArtifact;
    };

    struct RuntimeSourceLoadResult
    {
        std::shared_ptr<const RuntimeSource> source;
        juce::String rejectionCode;

        bool accepted() const noexcept { return source != nullptr; }
    };

    class RuntimeV2SourceRepository final
    {
    public:
        explicit RuntimeV2SourceRepository (juce::File transportRootIn);

        RuntimeSourceLoadResult load (const RuntimeCandidate&) const;
        juce::String verifySourceRevision (const RuntimeSource&) const;
        juce::String verifySourceFile (const RuntimeSource&) const;

    private:
        const juce::File root;
    };
}
