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

    }

    RuntimeV2Controller::RuntimeV2Controller (juce::File transportRootIn,
                                              SelectionGate selectionGateIn)
        : juce::Thread ("Kirin Reference v2"),
          root (std::move (transportRootIn)),
          selectionGate (std::move (selectionGateIn)),
          repository (root),
          aBindingRepository (root),
          aCapture (root),
          sourceRepository (root),
          measurementRepository (root),
          alignmentRepository (root),
          profileRepository (root),
          presentationRepository (root),
          recoveryTransport (root),
          presetSelectionTransport (root),
          presetAdoptionTransport (root),
          eventTransport (root)
    {
        startThread (juce::Thread::Priority::low);
    }

    RuntimeV2Controller::~RuntimeV2Controller()
    {
        selectA();
        signalThreadShouldExit();
        notify();
        if (! stopThread (-1))
            jassertfalse;
        aCapture.disconnect();
        removeRuntimeFiles (activeRuntimeFiles);
    }

    void RuntimeV2Controller::configure (RuntimeIdentity identity, double hostSampleRate,
                                         int hostChannels)
    {
        if (identity.hostProcessId == 0)
            identity.hostProcessId = currentProcessId();
        {
            const juce::ScopedLock lock (stateLock);
            requestedConfiguration.identity = std::move (identity);
            requestedConfiguration.sampleRate = hostSampleRate;
            requestedConfiguration.channels = hostChannels;
            ++requestedConfiguration.generation;
            requestedSelection = {};
            pendingApprovalKey.clear();
            currentSnapshot.sampleRateApprovalRequired = false;
            ready.store (false, std::memory_order_release);
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
        const auto blindState = blind.snapshot();
        result.bSelected = bSelected.load (std::memory_order_acquire);
        result.transportPlaying = latestPlaying.load (std::memory_order_acquire);
        result.transportPositionValid = latestPositionValid.load (std::memory_order_acquire);
        const bool publishedReady = ready.load (std::memory_order_acquire);
        result.auditionBuffered = publishedReady && (blindState.eligible
            || (result.transportPositionValid
                && pages.readyAt (mappedSourcePosition (latestHostPosition.load()), 1)));
        result.blindEligible = publishedReady && blindState.eligible;
        result.blindPhase = blindState.phase;
        result.activeBlindStimulus = blindState.activeStimulus;
        result.pendingBlindStimulus = blindState.pendingStimulus;
        result.answeredBlindStimulus = blindState.answeredStimulus;
        result.blindStimulusOneHeard = blindState.stimulusOneAudibleFrames > 0
            && blindState.stimulusOneConfirmedSwitches > 0;
        result.blindStimulusTwoHeard = blindState.stimulusTwoAudibleFrames > 0
            && blindState.stimulusTwoConfirmedSwitches > 0;
        result.blindLowerAApprovalRequired = result.blindEligible
            && blindState.lowerAApprovalRequired;
        result.blindRequiredAAttenuationDb = result.blindEligible
            ? blindState.requiredAAttenuationDb : 0.0;
        if (result.presetSelectionStatus.isNotEmpty())
            result.auditionBuffered = false;
        result.blindReveal = blindState.phase == BlindPhase::revealed
            ? (blindState.revealedStimulusOneSide == 1
                ? "1 = B  /  2 = A" : "1 = A  /  2 = B")
            : juce::String {};
        return result;
    }

    void RuntimeV2Controller::publish (Snapshot next)
    {
        const juce::ScopedLock lock (stateLock);
        pendingApprovalKey.clear();
        publishLocked (std::move (next));
    }

    void RuntimeV2Controller::publishReady (
        Snapshot next, std::shared_ptr<const RuntimeSource> source)
    {
        const juce::ScopedLock lock (stateLock);
        pendingApprovalKey.clear();
        publishedSource = std::move (source);
        publishLocked (std::move (next));
        ready.store (true, std::memory_order_release);
    }

    void RuntimeV2Controller::publishApprovalRequired (
        Snapshot next, const juce::String& approvalKey)
    {
        const juce::ScopedLock lock (stateLock);
        pendingApprovalKey = approvalKey;
        publishedSource.reset();
        ready.store (false, std::memory_order_release);
        publishLocked (std::move (next));
    }

    void RuntimeV2Controller::publishLocked (Snapshot next)
    {
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
        }
        if (currentSnapshot.recoveryStatus.isNotEmpty())
            next.recoveryStatus = currentSnapshot.recoveryStatus;
        if (currentSnapshot.presetSelectionStatus.isNotEmpty())
        {
            next.presetSelectionStatus = currentSnapshot.presetSelectionStatus;
            next.presetSelectionAction = currentSnapshot.presetSelectionAction;
            next.presetSelectionTargetId = currentSnapshot.presetSelectionTargetId;
        }
        currentSnapshot = std::move (next);
    }


}
