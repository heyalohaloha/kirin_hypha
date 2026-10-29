#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceRuntimeV2PlaybackIdentity.h"
#include "ReferenceContentAlignment.h"
#include "ReferenceCopyCorrespondence.h"
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
        auto capturedA = aCapture.currentAudio();
        std::optional<std::uint64_t> confirmedContinuity;
        bool confirmedCopy = false;
        const bool libraryVersion = versionComparison && configuration.identity.library
            && candidate.sourceKind == "work_version" && candidate.sourceWorkId.isNotEmpty()
            && candidate.sourceRecordingId.isNotEmpty() && candidate.sourceVersionId.isNotEmpty();
        // For a copy profile, retain calibration as a candidate after a seek.
        // Normal V verifies current PCM before every output block, so a short
        // loop need not supply another full capture. Blind keeps its full proof.
        if (libraryVersion && capturedA && capturedA->continuity != aCapture.continuity())
            capturedA.reset();
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
            ? referenceObservationIdentity (*capturedA) + ":" + juce::String (capturedA->continuity)
                + ":" + candidate.sourceArtifact.sha256 + ":"
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
            do
            {
                if (libraryVersion && next.detailedMeasurement)
                {
                    const auto previous = blind.snapshot();
                    const auto predicted = previous.bStartSample + outputSample (
                        capturedA->startSample - previous.aStartSample, hostRate, previous.bSampleRateHz);
                    auto matched = alignReferenceContent (*capturedA, *selectedSource, *next.detailedMeasurement, true,
                        previous.eligible ? std::optional<std::int64_t> { predicted } : std::nullopt);
                    const auto revision = sourceRepository.checkSourceRevision (*selectedSource);
                    if (revision.state == SourceRevisionState::revalidate) break;
                    if (revision.state == SourceRevisionState::refused)
                    { failClosedToA(); blind.clear(); next.state = RuntimeState::rejected; next.rejectionCode = revision.reason; return; }
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
                            if (mappingChanged)
                                matched = alignReferenceContent (*capturedA, *selectedSource, *next.detailedMeasurement, true);
                            const auto revisionAfter = sourceRepository.checkSourceRevision (*selectedSource);
                            if (revisionAfter.state == SourceRevisionState::revalidate) break;
                            if (revisionAfter.state == SourceRevisionState::refused)
                            { failClosedToA(); next.state = RuntimeState::rejected; next.rejectionCode = revisionAfter.reason; return; }
                            failClosedToAFromAudioThread(); blind.clear(); calibrationObservation.clear();
                            if (matched.established) prepared = blind.prepareWholeSong (*capturedA, *selectedSource, matched);
                        }
                    }
                    else if (matched.established && !previous.eligible && !blind.ongoing())
                        prepared = blind.prepareWholeSong (*capturedA, *selectedSource, matched);
                    if (prepared || (matched.established && previous.eligible && !mappingChanged && !calibrationChanged))
                    {
                        calibrationObservation.remember (capturedA, matched.sourceStartSample, selectedSource->audio.sampleRateHz);
                        confirmedCopy = referenceCopyCorresponds (capturedA->interleaved.size(),
                            [&] (size_t i) { return capturedA->interleaved[i]; },
                            [&] (size_t i) { return matched.alignedProbe[i]; });
                        confirmedContinuity = capturedA->continuity;
                    }
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
            } while (false);
        }
        const auto blindState = blind.snapshot();
        const auto nextContentMappingKey = blindState.eligible
            ? blindContextKey + ":" + juce::String (blindState.aStartSample) + ":"
                + juce::String (blindState.bStartSample)
            : juce::String {};
        if (nextContentMappingKey.isNotEmpty()
            && nextContentMappingKey != activeContentMappingKey)
        {
            const juce::ScopedLock mappingLock (mappingWriteLock);
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
            const juce::ScopedLock mappingLock (mappingWriteLock);
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
        if (confirmedContinuity)
        {
            const juce::ScopedLock mappingLock (mappingWriteLock);
            // These stamps confirm the existing anchors; they do not replace the
            // map or discard its plotted history. Anchor changes above own the seqlock.
            contentMappingContinuity.store (*confirmedContinuity, std::memory_order_release);
            copyCorrespondence.store (confirmedCopy, std::memory_order_release);
        }
        currentPages().request (mappedSourcePosition (latestHostPosition.load()));
        currentPages().service();
        const bool currentMap = !copyCorrespondence.load (std::memory_order_acquire)
            || contentMappingContinuity.load (std::memory_order_acquire) == aCapture.continuity();
        next.state = libraryVersion && !blindState.eligible && !blind.ongoing() ? RuntimeState::waiting : RuntimeState::ready;
        if (next.state == RuntimeState::waiting && next.rejectionCode.isEmpty())
            next.rejectionCode = "reference_alignment_waiting_for_content";
        next.blindEligible = blindState.eligible && (blind.ongoing() || currentMap);
        next.blindLowerAApprovalRequired = blindState.lowerAApprovalRequired;
        next.blindRequiredAAttenuationDb = blindState.requiredAAttenuationDb;
    }
}
