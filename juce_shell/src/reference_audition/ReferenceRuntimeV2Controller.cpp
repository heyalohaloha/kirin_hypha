#include "ReferenceRuntimeV2Controller.h"

#include <cmath>
#include <utility>

#if JUCE_WINDOWS
 #include <windows.h>
#else
 #include <unistd.h>
#endif

namespace hypha::reference_audition
{
    namespace
    {
        std::uint32_t currentProcessId() noexcept
        {
           #if JUCE_WINDOWS
            return static_cast<std::uint32_t> (::GetCurrentProcessId());
           #else
            return static_cast<std::uint32_t> (::getpid());
           #endif
        }

        // フェード（5 ms）は次の 1〜2 ブロックで終わる。8192 フレームのブロックでも足りる長さ。
        constexpr std::uint32_t revokeFadeLimitMs = 500;
    }

    RuntimeV2Controller::RuntimeV2Controller (juce::File transportRootIn,
                                              SelectionGate selectionGateIn, bool wholeVersionComparison,
                                              WorkflowCommitCallback workflowCommitCallbackIn)
        : juce::Thread ("Kirin Reference v2"),
          root (std::move (transportRootIn)),
          versionComparison (wholeVersionComparison),
          selectionGate (std::move (selectionGateIn)),
          workflowCommitCallback (std::move (workflowCommitCallbackIn)),
          repository (root),
          workflowRepository (root),
          aBindingRepository (root),
          aCapture (root),
          sourceRepository (root),
          measurementRepository (root),
          alignmentRepository (root),
          profileRepository (root),
          presentationRepository (root),
          recoveryTransport (root),
          presetSelectionTransport (root),
          candidatePreparationTransport (root),
          presetAdoptionTransport (root),
          eventTransport (root)
    {
        trackingEnabled.store (versionComparison, std::memory_order_release);  // H3: V は追従、C は固定
        outputRetirement.start ([this] { serviceOutputRetirement(); });
        startThread (juce::Thread::Priority::low);
    }

    RuntimeV2Controller::~RuntimeV2Controller()
    {
        selectA();
        signalThreadShouldExit();
        notify();
        if (! stopThread (-1))
            jassertfalse;
        outputRetirement.stop();
        blind.forceClearAfterAudioStopped();
        releaseActiveOutputGate();
        aCapture.disconnect();
        removeRuntimeFiles (activeRuntimeFiles);
    }

    void RuntimeV2Controller::configure (RuntimeIdentity identity, double hostSampleRate,
                                         int hostChannels)
    {
        normalFadeStep.store (static_cast<float> (1.0 / juce::jmax (1.0, hostSampleRate * 0.005)), std::memory_order_release);
        trackingRampFrames.store (static_cast<int> (juce::jmax (1.0, std::round (hostSampleRate * trackingRampSeconds))),
                                  std::memory_order_release);
        if (identity.hostProcessId == 0)
            identity.hostProcessId = currentProcessId();
        {
            const juce::ScopedLock lock (stateLock);
            const bool sameLibraryReceiver = identity.library && requestedConfiguration.identity.library
                && identity.runtimeInstanceId == requestedConfiguration.identity.runtimeInstanceId;
            requestedConfiguration.identity = std::move (identity);
            requestedConfiguration.sampleRate = hostSampleRate;
            requestedConfiguration.channels = hostChannels;
            ++requestedConfiguration.generation;
            if (! sameLibraryReceiver) requestedSelection = {};
            pendingApprovalKey.clear();
            currentSnapshot.sampleRateApprovalRequired = false;
            revokeAuditionPublication();
        }
        if (blind.ongoing())
            invalidateBlind();
        else
            selectA();
        notify();
    }

    void RuntimeV2Controller::disconnect()
    {
        configure ({}, 0.0, 0);
    }

    Snapshot RuntimeV2Controller::snapshot() const
    {
        const juce::ScopedLock lock (stateLock);
        auto result = currentSnapshot;
        result.libraryReceived = libraryReceived.load (std::memory_order_acquire);
        result.osOnline = libraryOnline.load (std::memory_order_acquire);
        result.libraryPreparation = libraryPreparation;
        const auto blindState = blind.snapshot();
        result.bSelected = bSelected.load (std::memory_order_acquire);
        result.transportPlaying = latestPlaying.load (std::memory_order_acquire);
        result.transportPositionValid = latestPositionValid.load (std::memory_order_acquire);
        const bool publishedReady = ready.load (std::memory_order_acquire);
        const auto mappingBefore = mappingGeneration.load (std::memory_order_acquire);
        const auto sourcePosition = result.transportPositionValid
            ? mappedSourcePosition (latestHostPosition.load (std::memory_order_acquire)) : -1;
        const auto mappingAfter = mappingGeneration.load (std::memory_order_acquire);
        // 押せば Cue の頭から鳴らし直す曲（restartCueAtPlayhead）は、今の位置が Cue の外でも「範囲外」にしない。
        const bool restarts = sourcePosition < 0 && restartsAtCueStart();
        result.auditionOutsideCue = publishedReady && ! versionComparison
            && result.transportPositionValid && sourcePosition < 0 && ! restarts
            && (mappingBefore & 1u) == 0 && mappingBefore == mappingAfter
            && cueEnd.load (std::memory_order_acquire) > cueStart.load (std::memory_order_acquire);
        result.auditionBuffered = publishedReady && (blindState.eligible
            || (result.transportPositionValid
                && pages.readyAt (restarts ? cueStart.load (std::memory_order_acquire) : sourcePosition, 1)));
        result.blindEligible = publishedReady && blindState.eligible;
        if (versionComparison && !blindState.eligible) result.auditionBuffered = false;
        result.blindPhase = blindState.phase;
        result.activeBlindStimulus = blindState.activeStimulus;
        result.pendingBlindStimulus = blindState.pendingStimulus;
        result.answeredBlindStimulus = blindState.answeredStimulus;
        const auto minimumFrames = blindState.wholeSong ? static_cast<std::uint64_t> (blindState.aSampleRateHz) * 3 : 1;
        result.blindStimulusOneHeard = blindState.stimulusOneAudibleFrames >= minimumFrames
            && blindState.stimulusOneConfirmedSwitches > 0;
        result.blindStimulusTwoHeard = blindState.stimulusTwoAudibleFrames >= minimumFrames
            && blindState.stimulusTwoConfirmedSwitches > 0;
        result.blindLowerAApprovalRequired = result.blindEligible
            && blindState.lowerAApprovalRequired;
        result.blindRequiredAAttenuationDb = result.blindEligible || blindState.attenuationHeld
            ? blindState.requiredAAttenuationDb : 0.0;
        if (result.presetSelectionStatus.isNotEmpty()
            || result.candidatePreparationStatus.isNotEmpty())
            result.auditionBuffered = false;
        result.blindReveal = blindState.phase == BlindPhase::revealed
            ? (blindState.revealedStimulusOneSide == 1
                ? "1 = V  /  2 = A" : "1 = A  /  2 = V")
            : juce::String {};
        return result;
    }

    void RuntimeV2Controller::publish (Snapshot next)
    {
        const juce::ScopedLock lock (stateLock);
        pendingApprovalKey.clear();
        approvalVisualSource.reset();
        publishedSource.reset();
        publishLocked (std::move (next));
    }

    void RuntimeV2Controller::publishReady (
        Snapshot next, std::shared_ptr<const RuntimeSource> source, const RuntimeCue& cue)
    {
        const juce::ScopedLock lock (stateLock);
        pendingApprovalKey.clear();
        approvalVisualSource.reset();
        visualSourceCueStart = cue.startSample;
        visualSourceCueEnd = cue.endSample;
        publishedSource = std::move (source);
        publishLocked (std::move (next));
        ready.store (true, std::memory_order_release);
    }

    void RuntimeV2Controller::publishApprovalRequired (
        Snapshot next, const juce::String& approvalKey, std::shared_ptr<const RuntimeSource> source,
        const RuntimeCue& cue)
    {
        const juce::ScopedLock lock (stateLock);
        pendingApprovalKey = approvalKey;
        approvalVisualSource = std::move (source);
        visualSourceCueStart = cue.startSample;
        visualSourceCueEnd = cue.endSample;
        publishedSource.reset();
        revokeAuditionPublication();
        publishLocked (std::move (next));
    }

    void RuntimeV2Controller::revokeAuditionPublication() noexcept
    {
        revokeAfterFade.store (false, std::memory_order_release);
        ready.store (false, std::memory_order_release);
        auditionEpoch.fetch_add (1, std::memory_order_acq_rel);
        normalSelectionGeneration.fetch_add (1, std::memory_order_acq_rel);
    }

    void RuntimeV2Controller::revokeAfterFadeLocked() noexcept
    {
        if (! normalAudible.load (std::memory_order_acquire) || ! ready.load (std::memory_order_acquire)
            || ! latestPlaying.load (std::memory_order_acquire) || blind.ongoing())
        {
            revokeAuditionPublication();
            return;
        }
        // 待っているあいだに古い音を選び直させない（selectB・resumeHeld は待ちの間は断る）。
        normalSelectionGeneration.fetch_add (1, std::memory_order_acq_rel);
        revokeFadeStartedMs.store (juce::Time::getMillisecondCounter(), std::memory_order_release);
        revokeAfterFade.store (true, std::memory_order_release);
    }

    bool RuntimeV2Controller::deferredRevokeWaiting() noexcept
    {
        if (! revokeAfterFade.load (std::memory_order_acquire)) return false;
        const auto elapsed = juce::Time::getMillisecondCounter() - revokeFadeStartedMs.load (std::memory_order_acquire);
        if (normalAudible.load (std::memory_order_acquire) && elapsed < revokeFadeLimitMs) return true;
        const juce::ScopedLock lock (stateLock);
        if (revokeAfterFade.load (std::memory_order_acquire)) revokeAuditionPublication();
        return false;
    }

    std::uint64_t RuntimeV2Controller::acquireOutputGate() noexcept
    {
        const juce::ScopedLock lock (outputGateLock);
        const bool retained = activeOutputGateToken.load (std::memory_order_acquire) != 0;
        if (retained && (bSelected.load (std::memory_order_acquire) || blind.ongoing())) return 0;
        auto token = nextOutputGateToken.fetch_add (1, std::memory_order_acq_rel);
        if (token == 0)
            token = nextOutputGateToken.fetch_add (1, std::memory_order_acq_rel);
        if (!retained && selectionGate && ! selectionGate (true))
            return 0;
        activeOutputGateToken.store (token, std::memory_order_release);
        return token;
    }

    void RuntimeV2Controller::releaseOutputGate (std::uint64_t token) noexcept
    {
        if (token == 0)
            return;
        const juce::ScopedLock lock (outputGateLock);
        if (activeOutputGateToken.load (std::memory_order_acquire) != token)
            return;
        activeOutputGateToken.store (0, std::memory_order_release);
        if (selectionGate)
            selectionGate (false);
    }

    void RuntimeV2Controller::releaseActiveOutputGate() noexcept
    {
        releaseOutputGate (activeOutputGateToken.load (std::memory_order_acquire));
    }

    void RuntimeV2Controller::publishLocked (Snapshot next)
    {
        next.workflowCatalog = workflowCatalog;
        if (next.playbackIdentity.isNotEmpty() && next.playbackIdentity == currentSnapshot.playbackIdentity)
            next.matchFailure = currentSnapshot.matchFailure;
        next.migratedVersionChoice = legacyVersionChoice;
        next.selectionGeneration = appliedSelectionGeneration.load (std::memory_order_acquire);
        next.bSelected = bSelected.load (std::memory_order_acquire);
        if (next.bSelected || blind.ongoing())
        {
            next.appliedGainDb = currentSnapshot.appliedGainDb;
            next.gainLimited = currentSnapshot.gainLimited;
            next.comparisonFallbackOriginal = currentSnapshot.comparisonFallbackOriginal;
            next.aIntegratedLoudness = currentSnapshot.aIntegratedLoudness;
            next.aMaximumTruePeakDbtp = currentSnapshot.aMaximumTruePeakDbtp;
            next.adjustedBIntegratedLoudness = currentSnapshot.adjustedBIntegratedLoudness;
            next.adjustedBMaximumTruePeakDbtp = currentSnapshot.adjustedBMaximumTruePeakDbtp;
            next.loudnessDeltaBMinusA = currentSnapshot.loudnessDeltaBMinusA;
            next.truePeakDeltaBMinusA = currentSnapshot.truePeakDeltaBMinusA;
            next.tracking = currentSnapshot.tracking;
        }
        if (currentSnapshot.recoveryStatus.isNotEmpty())
            next.recoveryStatus = currentSnapshot.recoveryStatus;
        if (currentSnapshot.presetSelectionStatus.isNotEmpty())
        {
            next.presetSelectionStatus = currentSnapshot.presetSelectionStatus;
            next.presetSelectionAction = currentSnapshot.presetSelectionAction;
            next.presetSelectionTargetId = currentSnapshot.presetSelectionTargetId;
        }
        if (currentSnapshot.candidatePreparationStatus.isNotEmpty())
        {
            next.candidatePreparationStatus = currentSnapshot.candidatePreparationStatus;
            next.candidatePreparationAction = currentSnapshot.candidatePreparationAction;
            next.candidatePreparationTargetId = currentSnapshot.candidatePreparationTargetId;
        }
        currentSnapshot = std::move (next);
    }


}
