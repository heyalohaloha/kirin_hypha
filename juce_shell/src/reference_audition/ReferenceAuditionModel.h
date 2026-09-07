#pragma once

#include <cstdint>
#include <vector>

#include <juce_core/juce_core.h>

namespace hypha::reference_audition
{
    enum class AlignmentMode
    {
        sampleLock,
        referenceCue,
    };

    struct RuntimeIdentity
    {
        juce::String runtimeInstanceId;
        juce::String workId;
        std::uint32_t hostProcessId = 0;

        bool valid() const noexcept;
    };

    struct Preparation
    {
        juce::String preparationId;
        juce::String workId;
        juce::String sourceKind;
        juce::String sourceId;
        juce::String sourceWorkId;
        juce::String versionId;
        juce::String catalogReferenceId;
        juce::String title;
        juce::String fileName;
        juce::String sourceSha256;
        juce::String receiptSha256;
        std::int64_t receiptBytes = 0;
        double maxSafePositiveGainDb = 0.0;
        AlignmentMode alignmentMode = AlignmentMode::referenceCue;
        double referenceCueSeconds = 0.0;
        std::int64_t preparedAtMs = 0;

        bool valid() const noexcept;
    };

    struct ObservationSignature
    {
        double sourceHopMs = 0.0;
        std::int64_t sourcePointCount = 0;
        std::int64_t downsampleStride = 0;
        double signatureHopMs = 0.0;
        std::vector<double> values;
        std::vector<bool> present;
    };

    struct SourceReceipt
    {
        juce::String sourceKind;
        juce::String sourceId;
        juce::String sourceWorkId;
        juce::String versionId;
        juce::String catalogReferenceId;
        juce::String title;
        juce::String fileName;
        juce::String filePath;
        juce::String sourceSha256;
        juce::String measurementReceiptSha256;
        juce::String revisionDevice;
        juce::String revisionInode;
        juce::String revisionSize;
        juce::String revisionModifiedMs;
        juce::String revisionChangedMs;
        double integratedLoudness = 0.0;
        double maximumTruePeakDbtp = 0.0;
        double durationSeconds = 0.0;
        std::int64_t sampleRateHz = 0;
        std::int64_t channels = 0;
        std::int64_t sampleCount = 0;
        juce::String decodedPcmSha256;
        ObservationSignature observation;
        bool hasDuration = false;
        bool hasSampleRate = false;
        bool hasChannels = false;
        bool hasSampleCount = false;
        bool hasDecodedPcmSha256 = false;
        bool hasObservation = false;

        bool valid() const noexcept;
        bool matches (const Preparation&) const noexcept;
    };

    enum class LoadState
    {
        unavailable,
        accepted,
        rejected,
    };

    struct LoadResult
    {
        LoadState state = LoadState::unavailable;
        juce::String rejectionCode;
        Preparation preparation;
        SourceReceipt receipt;

        bool accepted() const noexcept { return state == LoadState::accepted; }
    };
}
