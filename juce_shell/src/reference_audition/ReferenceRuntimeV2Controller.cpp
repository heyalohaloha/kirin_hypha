#include "ReferenceRuntimeV2Controller.h"

#include <cmath>
#include <limits>
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

        const RuntimePreset* findPreset (const RuntimeWorkspace& workspace,
                                         const juce::String& id)
        {
            for (const auto& preset : workspace.presets)
                if (preset.sourcePresetArtifact.presetId == id)
                    return &preset;
            return nullptr;
        }

        const RuntimeCheck* findCheck (const RuntimePreset& preset, const juce::String& id)
        {
            for (const auto& check : preset.checks)
                if (check.checkId == id)
                    return &check;
            return nullptr;
        }

        const RuntimeCandidate* findCandidate (const RuntimeCheck& check, const juce::String& id)
        {
            for (const auto& candidate : check.candidates)
                if (candidate.candidateId == id)
                    return &candidate;
            return nullptr;
        }

        const RuntimeCue* findCue (const RuntimeCandidate& candidate, const juce::String& id)
        {
            for (const auto& cue : candidate.cues)
                if (cue.cueId == id)
                    return &cue;
            return nullptr;
        }

        std::int64_t outputSample (std::int64_t sourceSample, std::int64_t sourceRate,
                                   std::int64_t outputRate)
        {
            if (sourceRate <= 0 || outputRate <= 0)
                return 0;
            return static_cast<std::int64_t> (std::llround (
                static_cast<long double> (sourceSample) * outputRate / sourceRate));
        }

        double unavailable() noexcept
        {
            return std::numeric_limits<double>::quiet_NaN();
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
        }
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
        result.auditionBuffered = blindState.eligible || (
            ready.load (std::memory_order_acquire) && result.transportPositionValid
            && pages.readyAt (mappedSourcePosition (latestHostPosition.load()), 1));
        result.blindEligible = blindState.eligible;
        result.blindPhase = blindState.phase;
        result.activeBlindStimulus = blindState.activeStimulus;
        result.pendingBlindStimulus = blindState.pendingStimulus;
        result.answeredBlindStimulus = blindState.answeredStimulus;
        result.blindStimulusOneHeard = blindState.stimulusOneAudibleFrames > 0
            && blindState.stimulusOneConfirmedSwitches > 0;
        result.blindStimulusTwoHeard = blindState.stimulusTwoAudibleFrames > 0
            && blindState.stimulusTwoConfirmedSwitches > 0;
        result.blindLowerAApprovalRequired = blindState.lowerAApprovalRequired;
        result.blindRequiredAAttenuationDb = blindState.requiredAAttenuationDb;
        result.blindReveal = blindState.phase == BlindPhase::revealed
            ? (blindState.revealedStimulusOneSide == 1
                ? "1 = B  /  2 = A" : "1 = A  /  2 = B")
            : juce::String {};
        return result;
    }

    void RuntimeV2Controller::publish (Snapshot next)
    {
        const juce::ScopedLock lock (stateLock);
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
        currentSnapshot = std::move (next);
    }

    void RuntimeV2Controller::refreshWorkspace (const Configuration& configuration,
                                                std::int64_t nowMs)
    {
        if (! configuration.identity.valid())
            return;
        writeCapability (root, configuration.identity, nowMs);
        activeABinding = aBindingRepository.load (configuration.identity, nowMs);
        aCapture.service (activeABinding,
                          static_cast<std::int64_t> (std::llround (
                              configuration.sampleRate)),
                          configuration.channels, nowMs);
        const auto loaded = repository.refresh (configuration.identity.workId, workspace);
        if (loaded.workspace == nullptr)
        {
            failClosedToA();
            Snapshot next;
            next.state = loaded.state == RuntimeWorkspaceLoadState::missing
                ? RuntimeState::waiting : RuntimeState::rejected;
            next.rejectionCode = loaded.rejectionCode;
            publish (std::move (next));
            return;
        }
        workspace = loaded.workspace;
        RequestedSelection selection;
        {
            const juce::ScopedLock lock (stateLock);
            selection = requestedSelection;
        }
        appliedSelectionGeneration = selection.generation;
        const RuntimePreset* preset = findPreset (*workspace, selection.presetId);
        if (preset == nullptr)
            preset = findPreset (*workspace, workspace->manifest.activePresetId);
        if (preset == nullptr && ! workspace->presets.empty())
            preset = &workspace->presets.front();
        if (preset == nullptr || preset->checks.empty())
        {
            failClosedToA();
            Snapshot next;
            next.state = RuntimeState::waiting;
            next.rejectionCode = "reference_checks_empty";
            for (const auto& item : workspace->presets)
                next.presets.push_back ({ item.sourcePresetArtifact.presetId, item.name });
            publish (std::move (next));
            return;
        }
        const RuntimeCheck* check = findCheck (*preset, selection.checkId);
        if (check == nullptr) check = &preset->checks.front();
        const RuntimeCandidate* candidate = findCandidate (*check, selection.candidateId);
        if (candidate == nullptr) candidate = &check->candidates.front();
        const RuntimeCue* cue = findCue (*candidate, selection.cueId);
        if (cue == nullptr) cue = findCue (*candidate, candidate->defaultCueId);
        if (cue == nullptr) cue = &candidate->cues.front();

        Snapshot next;
        next.state = RuntimeState::verifying;
        next.presetId = preset->sourcePresetArtifact.presetId;
        next.checkId = check->checkId;
        next.candidateId = candidate->candidateId;
        next.cueId = cue->cueId;
        next.presetName = preset->name;
        next.checkLabel = check->label;
        next.candidateName = candidate->displayName;
        next.cueLabel = cue->label;
        next.title = candidate->displayName;
        next.sourceKind = candidate->sourceKind;
        next.comparisonMode = check->comparisonMode;
        next.presentationLayout = RuntimeV2PresentationRepository::text (
            presentationRepository.load (configuration.identity.workId));
        next.viewBindings = check->viewBindings;
        next.hostSampleRateHz = static_cast<std::int64_t> (std::llround (configuration.sampleRate));
        next.aBindingAvailable = activeABinding.has_value();
        next.aRecordingId = activeABinding ? activeABinding->recordingId : juce::String {};
        next.aCaptureAvailable = aCapture.currentReceipt().has_value();
        for (const auto& item : workspace->presets)
            next.presets.push_back ({ item.sourcePresetArtifact.presetId, item.name });
        for (const auto& item : preset->checks)
            next.checks.push_back ({ item.checkId, item.label });
        for (const auto& item : check->candidates)
            next.candidates.push_back ({ item.candidateId, item.displayName });
        for (const auto& item : candidate->cues)
            next.cues.push_back ({ item.cueId, item.label });

        const auto sourceLoad = sourceRepository.load (*candidate);
        if (! sourceLoad.accepted())
        {
            failClosedToA();
            next.state = RuntimeState::rejected;
            next.rejectionCode = sourceLoad.rejectionCode;
            publish (std::move (next));
            return;
        }
        auto selectedSource = sourceLoad.source;
        const bool sourceArtifactChanged = activeSource == nullptr
            || activeSourceArtifactSha256 != candidate->sourceArtifact.sha256;
        const auto sourceFailure = sourceArtifactChanged
            ? sourceRepository.verifySourceFile (*selectedSource)
            : sourceRepository.verifySourceRevision (*activeSource);
        if (sourceFailure.isNotEmpty())
        {
            failClosedToA();
            next.state = RuntimeState::rejected;
            next.rejectionCode = sourceFailure;
            publish (std::move (next));
            return;
        }
        if (! sourceArtifactChanged)
            selectedSource = activeSource;
        next.sourceSampleRateHz = selectedSource->audio.sampleRateHz;
        const auto approvalKey = candidate->sourceArtifact.sha256 + ":"
            + juce::String (selectedSource->audio.sampleRateHz) + ":"
            + juce::String (next.hostSampleRateHz);
        const bool rateDiffers = selectedSource->audio.sampleRateHz != next.hostSampleRateHz;
        const bool rateApproved = ! rateDiffers || selection.sampleRateApprovalKey == approvalKey;
        if (! rateApproved)
        {
            failClosedToA();
            pendingApprovalKey = approvalKey;
            next.state = RuntimeState::waiting;
            next.sampleRateApprovalRequired = true;
            next.rejectionCode = "reference_sample_rate_approval_required";
            publish (std::move (next));
            return;
        }
        pendingApprovalKey.clear();

        const auto sourceKey = candidate->sourceArtifact.sha256 + ":"
            + juce::String (next.hostSampleRateHz) + ":"
            + (rateDiffers ? "converted" : "native");
        if (sourceKey != activeSourceKey || ! pages.sourceOpen())
        {
            selectA();
            ready.store (false, std::memory_order_release);
            pages.close();
            const auto openFailure = pages.open (*selectedSource, configuration.sampleRate,
                                                 configuration.channels, rateApproved);
            if (openFailure.isNotEmpty())
            {
                next.state = RuntimeState::rejected;
                next.rejectionCode = openFailure;
                publish (std::move (next));
                return;
            }
            activeSourceKey = sourceKey;
        }
        activeSource = selectedSource;
        activeSourceArtifactSha256 = candidate->sourceArtifact.sha256;

        const auto measurement = measurementRepository.load (*activeSource);
        if (measurement.accepted())
        {
            next.detailedMeasurement = measurement.measurement;
            next.measurementAvailable = true;
        }
        const auto alignment = alignmentRepository.load (*activeSource);
        next.alignmentPrepared = alignment.accepted();
        for (const auto& binding : check->profileBindings)
        {
            const auto profile = profileRepository.load (binding.profileArtifact);
            if (profile.accepted()) next.profiles.push_back (profile.profile);
        }

        const auto hostRate = next.hostSampleRateHz;
        const auto mappedCueStart = outputSample (
            cue->startSample, cue->sampleRateHz, hostRate);
        const auto mappedCueEnd = outputSample (
            cue->endSample, cue->sampleRateHz, hostRate);
        const auto mappingKey = activeSourceKey + ":" + cue->cueId + ":"
            + juce::String (cue->startSample) + ":" + juce::String (cue->endSample)
            + ":" + (cue->loopEnabled ? "loop" : "once");
        if (activeMappingKey != mappingKey)
        {
            mappingGeneration.fetch_add (1, std::memory_order_acq_rel);
            cueStart.store (mappedCueStart, std::memory_order_relaxed);
            cueEnd.store (mappedCueEnd, std::memory_order_relaxed);
            cueLoops.store (cue->loopEnabled, std::memory_order_relaxed);
            sampleLocked.store (candidate->sourceKind == "work_version"
                                && candidate->sourceWorkId == configuration.identity.workId,
                                std::memory_order_relaxed);
            bHostAnchor.store (latestHostPosition.load (std::memory_order_acquire),
                               std::memory_order_relaxed);
            bSourceAnchor.store (mappedCueStart, std::memory_order_relaxed);
            mappingGeneration.fetch_add (1, std::memory_order_release);
            activeMappingKey = mappingKey;
            activeContentMappingKey.clear();
        }
        next.sourceIntegratedLoudness = activeSource->measurementSummary
            && activeSource->measurementSummary->loudnessLufsI
            ? *activeSource->measurementSummary->loudnessLufsI : unavailable();
        next.sourceMaximumTruePeakDbtp = activeSource->measurementSummary
            && activeSource->measurementSummary->maximumTruePeakDbtp
            ? *activeSource->measurementSummary->maximumTruePeakDbtp : unavailable();
        const auto capturedA = aCapture.currentAudio();
        const bool blindIdentityMatches = activeABinding
            && activeABinding->workId == configuration.identity.workId
            && activeABinding->recordingId == candidate->sourceRecordingId
            && candidate->sourceKind == "work_version"
            && candidate->sourceWorkId == configuration.identity.workId
            && candidate->sourceVersionId.isNotEmpty();
        const auto nextBlindContextKey = blindIdentityMatches
            ? activeABinding->bindingId + ":" + candidate->sourceArtifact.sha256 + ":"
                + cue->cueId + ":" + juce::String (next.hostSampleRateHz)
            : juce::String {};
        if (nextBlindContextKey != blindContextKey)
        {
            if (blind.ongoing())
                invalidateBlind();
            blind.clear();
            blindPreparationKey.clear();
            blindContextKey = nextBlindContextKey;
        }
        const auto nextBlindKey = blindIdentityMatches && capturedA != nullptr
            ? capturedA->cuePcmSha256 + ":" + candidate->sourceArtifact.sha256 + ":"
                + cue->cueId + ":" + juce::String (next.hostSampleRateHz)
            : juce::String {};
        if (nextBlindKey.isEmpty() && blindPreparationKey.isNotEmpty()
            && ! blind.ongoing())
        {
            blind.clear();
            blindPreparationKey.clear();
        }
        if (blindIdentityMatches && capturedA != nullptr
            && nextBlindKey != blindPreparationKey)
        {
            if (blind.ongoing())
                invalidateBlind();
            blind.prepare (capturedA, *candidate, *cue, activeSource, rateApproved);
            blindPreparationKey = nextBlindKey;
        }
        const auto blindState = blind.snapshot();
        const auto nextContentMappingKey = blindState.eligible
            ? nextBlindKey + ":" + juce::String (blindState.aStartSample) + ":"
                + juce::String (blindState.bStartSample)
            : juce::String {};
        if (nextContentMappingKey.isNotEmpty()
            && nextContentMappingKey != activeContentMappingKey)
        {
            mappingGeneration.fetch_add (1, std::memory_order_acq_rel);
            cueStart.store (mappedCueStart, std::memory_order_relaxed);
            cueEnd.store (mappedCueEnd, std::memory_order_relaxed);
            cueLoops.store (cue->loopEnabled, std::memory_order_relaxed);
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
            cueLoops.store (cue->loopEnabled, std::memory_order_relaxed);
            sampleLocked.store (candidate->sourceKind == "work_version"
                                && candidate->sourceWorkId == configuration.identity.workId,
                                std::memory_order_relaxed);
            bHostAnchor.store (latestHostPosition.load (std::memory_order_acquire),
                               std::memory_order_relaxed);
            bSourceAnchor.store (mappedCueStart, std::memory_order_relaxed);
            mappingGeneration.fetch_add (1, std::memory_order_release);
            activeContentMappingKey.clear();
        }
        pages.request (mappedSourcePosition (latestHostPosition.load()));
        pages.service();
        ready.store (true, std::memory_order_release);
        next.state = RuntimeState::ready;
        next.blindEligible = blindState.eligible;
        next.blindLowerAApprovalRequired = blindState.lowerAApprovalRequired;
        next.blindRequiredAAttenuationDb = blindState.requiredAAttenuationDb;
        {
            const juce::ScopedLock lock (stateLock);
            if (requestedSelection.generation == selection.generation)
            {
                requestedSelection.presetId = next.presetId;
                requestedSelection.checkId = next.checkId;
                requestedSelection.candidateId = next.candidateId;
                requestedSelection.cueId = next.cueId;
            }
            activeEventContext = {
                configuration.identity,
                workspace->manifest.revision,
                preset->sourcePresetArtifact,
                next.presetName,
                next.checkId,
                next.checkLabel,
                next.candidateId,
                next.candidateName,
                next.cueId,
                next.cueLabel,
                next.comparisonMode,
            };
            activeEventCandidate = *candidate;
            activeEventCue = *cue;
            activeEventSource = activeSource;
        }
        publish (std::move (next));
    }

}
