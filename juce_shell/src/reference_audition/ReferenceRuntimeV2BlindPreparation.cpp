#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceRuntimeV2PlaybackIdentity.h"
#include "ReferenceContentAlignment.h"
#include <cmath>
namespace hypha::reference_audition
{
    namespace {
        std::int64_t outputSample (std::int64_t sample, std::int64_t from, std::int64_t to)
        { return static_cast<std::int64_t> (std::llround (static_cast<long double> (sample) * to / from)); }
    }
    void RuntimeV2Controller::serviceBlindPreparation (const Configuration& configuration,
        const RuntimeCandidate& candidate, const RuntimeCue& cue,
        const std::shared_ptr<const RuntimeSource>& selectedSource, Snapshot& next)
    {
        const auto hostRate = next.hostSampleRateHz;
        const auto mappedCueStart = outputSample (cue.startSample, cue.sampleRateHz, hostRate);
        const auto mappedCueEnd = outputSample (cue.endSample, cue.sampleRateHz, hostRate);
        const auto cueKey = runtimeCuePlaybackIdentity (cue);
        const auto capturedA = aCapture.currentAudio();
        const bool libraryVersion = versionComparison && configuration.identity.library
            && candidate.sourceKind == "work_version" && candidate.sourceWorkId.isNotEmpty()
            && candidate.sourceRecordingId.isNotEmpty() && candidate.sourceVersionId.isNotEmpty();
        const bool blindIdentityMatches = libraryVersion || (activeABinding
            && activeABinding->workId == configuration.identity.workId
            && activeABinding->recordingId == candidate.sourceRecordingId
            && candidate.sourceKind == "work_version"
            && candidate.sourceWorkId == configuration.identity.workId
            && candidate.sourceVersionId.isNotEmpty());
        const auto nextBlindContextKey = blindIdentityMatches
            ? (libraryVersion ? configuration.identity.runtimeInstanceId : activeABinding->bindingId)
                + ":" + runtimeSourceAudioIdentity (*selectedSource) + ":"
                + cueKey + ":" + juce::String (next.hostSampleRateHz)
            : juce::String {};
        if (nextBlindContextKey != blindContextKey)
        {
            if (blind.ongoing())
                invalidateBlind();
            blind.clear();
            blindPreparationKey.clear();
            calibrationObservation.clear();
            blindContextKey = nextBlindContextKey;
        }
        const auto nextBlindKey = blindIdentityMatches && capturedA != nullptr
            ? referenceObservationIdentity (*capturedA) + ":" + candidate.sourceArtifact.sha256 + ":"
                + cueKey + ":" + juce::String (next.hostSampleRateHz)
            : juce::String {};
        if (!libraryVersion && nextBlindKey.isEmpty() && blindPreparationKey.isNotEmpty()
            && ! blind.ongoing())
        {
            blind.clear();
            blindPreparationKey.clear();
        }
        if (blindIdentityMatches && capturedA != nullptr
            && nextBlindKey != blindPreparationKey)
        {
            if (libraryVersion && next.detailedMeasurement)
            {
                const auto previous = blind.snapshot();
                const auto predicted = previous.bStartSample + outputSample (
                    capturedA->startSample - previous.aStartSample, hostRate, previous.bSampleRateHz);
                auto matched = alignReferenceContent (*capturedA, *selectedSource, *next.detailedMeasurement, true,
                    previous.eligible ? std::optional<std::int64_t> { predicted } : std::nullopt);
                if (sourceRepository.verifySourceRevision (*selectedSource).isNotEmpty())
                { failClosedToA(); blind.clear(); next.state = RuntimeState::rejected; next.rejectionCode = "reference_source_changed"; return; }
                const bool mappingChanged = previous.eligible
                    && ((capturedA->cuePcmSha256 == previous.aCuePcmSha256 && capturedA->startSample != previous.aStartSample)
                        || (matched.established && std::abs (matched.sourceStartSample - predicted) > 1)
                        || matched.reason == "reference_alignment_content_changed"
                        || matched.reason == "reference_alignment_timing_changed");
                const bool calibrationChanged = previous.eligible && matched.established
                    && calibrationObservation.changed (*capturedA, matched.sourceStartSample, selectedSource->audio.sampleRateHz);
                bool prepared = false;
                if (mappingChanged || calibrationChanged)
                {
                    if (blind.ongoing()) invalidateBlind();
                    else
                    {
                        failClosedToA(); blind.clear(); calibrationObservation.clear();
                        if (mappingChanged)
                            matched = alignReferenceContent (*capturedA, *selectedSource, *next.detailedMeasurement, true);
                        if (sourceRepository.verifySourceRevision (*selectedSource).isNotEmpty())
                        { failClosedToA(); next.state = RuntimeState::rejected; next.rejectionCode = "reference_source_changed"; return; }
                        if (matched.established) prepared = blind.prepareWholeSong (*capturedA, *selectedSource, matched);
                    }
                }
                else if (matched.established && !previous.eligible && !blind.ongoing())
                    prepared = blind.prepareWholeSong (*capturedA, *selectedSource, matched);
                if (prepared || (matched.established && previous.eligible && !mappingChanged && !calibrationChanged))
                    calibrationObservation.remember (capturedA, matched.sourceStartSample, selectedSource->audio.sampleRateHz);
                // Silence or an ambiguous passage cannot replace a proven map.
                // A positively detected timing change returns to A before rematching.
                if (matched.reason != "reference_alignment_busy"
                    && (!blind.ongoing() || (previous.eligible && !mappingChanged && !calibrationChanged)))
                    blindPreparationKey = nextBlindKey;
                if (!previous.eligible && !matched.established) next.rejectionCode = matched.reason;
            }
            else if (!libraryVersion)
            {
                if (blind.ongoing()) invalidateBlind();
                if (blind.prepare (capturedA, candidate, cue, selectedSource, true)) blindPreparationKey = nextBlindKey;
            }
        }
        const auto blindState = blind.snapshot();
        const auto nextContentMappingKey = blindState.eligible
            ? blindContextKey + ":" + juce::String (blindState.aStartSample) + ":"
                + juce::String (blindState.bStartSample)
            : juce::String {};
        if (nextContentMappingKey.isNotEmpty()
            && nextContentMappingKey != activeContentMappingKey)
        {
            mappingGeneration.fetch_add (1, std::memory_order_acq_rel);
            cueStart.store (mappedCueStart, std::memory_order_relaxed);
            cueEnd.store (mappedCueEnd, std::memory_order_relaxed);
            cueLoops.store (cue.loopEnabled, std::memory_order_relaxed);
            bHostAnchor.store (outputSample (
                blindState.aStartSample, blindState.aSampleRateHz, hostRate),
                std::memory_order_relaxed);
            bSourceAnchor.store (outputSample (
                blindState.bStartSample, blindState.bSampleRateHz, hostRate),
                std::memory_order_relaxed);
            sampleLocked.store (false, std::memory_order_relaxed);
            mappingGeneration.fetch_add (1, std::memory_order_release);
            activeContentMappingKey = nextContentMappingKey;
        }
        else if (nextContentMappingKey.isEmpty() && activeContentMappingKey.isNotEmpty())
        {
            mappingGeneration.fetch_add (1, std::memory_order_acq_rel);
            cueStart.store (mappedCueStart, std::memory_order_relaxed);
            cueEnd.store (mappedCueEnd, std::memory_order_relaxed);
            cueLoops.store (cue.loopEnabled, std::memory_order_relaxed);
            sampleLocked.store (candidate.sourceKind == "work_version"
                                && candidate.sourceWorkId == configuration.identity.workId,
                                std::memory_order_relaxed);
            bHostAnchor.store (latestHostPosition.load (std::memory_order_acquire),
                               std::memory_order_relaxed);
            bSourceAnchor.store (mappedCueStart, std::memory_order_relaxed);
            mappingGeneration.fetch_add (1, std::memory_order_release);
            activeContentMappingKey.clear();
        }
        pages.request (mappedSourcePosition (latestHostPosition.load()));
        pages.service();
        next.state = libraryVersion && !blindState.eligible && !blind.ongoing() ? RuntimeState::waiting : RuntimeState::ready;
        if (next.state == RuntimeState::waiting && next.rejectionCode.isEmpty())
            next.rejectionCode = "reference_alignment_waiting_for_content";
        next.blindEligible = blindState.eligible;
        next.blindLowerAApprovalRequired = blindState.lowerAApprovalRequired;
        next.blindRequiredAAttenuationDb = blindState.requiredAAttenuationDb;
    }
}
