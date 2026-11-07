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

    namespace revision_detail
    {
        class SourceSnapshot;
        struct PreparedSourceAudio
        {
            juce::String key;
            std::shared_ptr<SourceSnapshot> snapshot;
        };
    }

    struct RuntimeSource
    {
        std::shared_ptr<revision_detail::PreparedSourceAudio> preparedAudio
            = std::make_shared<revision_detail::PreparedSourceAudio>();
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

    // Nonzero values gate prepared pages atomically; the reason also reaches the UI.
    inline std::uint64_t sourceFailureValue (const juce::String& reason)
    {
        if (reason == "reference_source_changed") return 2;
        if (reason == "reference_source_audio_mismatch") return 3;
        if (reason == "reference_source_decode_failed") return 4;
        return 1;
    }
    inline juce::String sourceFailureCode (std::uint64_t value)
    {
        if (value == 2) return "reference_source_changed";
        if (value == 3) return "reference_source_audio_mismatch";
        if (value == 4) return "reference_source_decode_failed";
        return value == 0 ? juce::String {} : juce::String ("reference_source_revalidating");
    }

    enum class SourceRevisionState { matched, revalidate, refused };
    struct SourceRevisionResult
    {
        SourceRevisionState state;
        juce::String reason;
        RuntimeFileRevision revision {};
    };

    class RuntimeV2SourceRepository final
    {
    public:
        explicit RuntimeV2SourceRepository (juce::File transportRootIn);

        RuntimeSourceLoadResult load (const RuntimeCandidate&) const;
        bool readSourceRevision (const RuntimeSource&, RuntimeFileRevision&) const;
        SourceRevisionResult checkSourceRevision (const RuntimeSource&) const;
        // Shared non-RT monitor: an immutable copy stays usable through short I/O failures;
        // a confirmed outage waits for automatic verification without ending the selection.
        juce::String verifySourceRevision (const RuntimeSource&) const;
        // freshAttempt: the caller's retry reservation has run out, so the worker's transient
        // failure is dropped and one new hash may start. A poll that waits for a running attempt
        // passes false and receives the worker's failure reason when that attempt ends.
        juce::String verifySourceFile (const RuntimeSource&, bool freshAttempt = false) const;

    private:
        const juce::File root;
    };
}
