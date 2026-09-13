#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceRuntimePendingPresets.h"

#include <algorithm>

namespace hypha::reference_audition
{
    bool RuntimeV2Controller::requestSelection (const juce::String& kind,
                                                const juce::String& id)
    {
        if (id.isEmpty())
            return false;
        {
            const juce::ScopedLock lock (stateLock);
            juce::String* selectedId = nullptr;
            if (kind == "preset") selectedId = &requestedSelection.presetId;
            else if (kind == "check") selectedId = &requestedSelection.checkId;
            else if (kind == "candidate") selectedId = &requestedSelection.candidateId;
            else if (kind == "cue") selectedId = &requestedSelection.cueId;
            else return false;
            if (*selectedId == id)
                return true;
            *selectedId = id;
            if (requestedConfiguration.identity.library)
            {
                if (kind == "preset") requestedSelection.checkId.clear();
                if (kind == "preset" || kind == "check") requestedSelection.candidateId.clear();
                if (kind != "cue") requestedSelection.cueId.clear();
            }
            ++requestedSelection.generation;
            requestedSelection.sampleRateApprovalKey.clear();
            pendingApprovalKey.clear();
            currentSnapshot.sampleRateApprovalRequired = false;
            revokeAuditionPublication();
        }
        if (blind.ongoing()) invalidateBlind();
        else selectA();
        notify();
        return true;
    }

    bool RuntimeV2Controller::selectPreset (const juce::String& id)
    {
        const auto presetId = runtimePresetOptionIdentity (id);
        RuntimeSelectionOption option;
        RuntimeIdentity identity;
        std::optional<PresetSelectionRequest> cancelled;
        std::optional<CandidatePreparationRequest> cancelledCandidate;
        std::int64_t manifestRevision = 0;
        std::uint64_t configurationGeneration = 0;
        bool restorePublishedSelection = false;
        {
            const juce::ScopedLock lock (stateLock);
            const auto found = std::find_if (
                currentSnapshot.presets.begin(), currentSnapshot.presets.end(),
                [&] (const auto& item) { return item.id == id; });
            if (found == currentSnapshot.presets.end()) return false;
            option = *found;
            if (option.requiresPreparation && pendingPresetSelectionRequest) return false;
            cancelledCandidate = pendingCandidatePreparationRequest;
            pendingCandidatePreparationRequest.reset();
            failedCandidatePreparationTarget.reset();
            currentSnapshot.candidatePreparationStatus.clear();
            currentSnapshot.candidatePreparationAction.clear();
            currentSnapshot.candidatePreparationTargetId.clear();
            candidatePreparationWaitingSinceMs = 0;
            candidatePreparationStatusExpiresAtMs = 0;
            if (! option.requiresPreparation)
            {
                cancelled = pendingPresetSelectionRequest;
                pendingPresetSelectionRequest.reset();
                pendingPresetSelectionTarget.reset();
                failedPresetSelectionTarget.reset();
                currentSnapshot.presetSelectionStatus.clear();
                currentSnapshot.presetSelectionAction.clear();
                currentSnapshot.presetSelectionTargetId.clear();
                presetSelectionWaitingSinceMs = 0;
                presetSelectionStatusExpiresAtMs = 0;
                restorePublishedSelection = presetId == currentSnapshot.presetId
                    && currentSnapshot.state == RuntimeState::ready
                    && publishedSource != nullptr;
                manifestRevision = -1;
            }
            else
            {
                identity = requestedConfiguration.identity;
                configurationGeneration = requestedConfiguration.generation;
                manifestRevision = currentSnapshot.manifestRevision;
            }
        }
        if (cancelledCandidate)
            candidatePreparationTransport.removeExchange (*cancelledCandidate);
        if (manifestRevision == -1)
        {
            if (cancelled) presetSelectionTransport.removeExchange (*cancelled);
            if (restorePublishedSelection)
            {
                ready.store (true, std::memory_order_release);
                return true;
            }
            return requestSelection ("preset", presetId);
        }
        RuntimeGlobalPresetCatalogEntry target;
        target.presetId = presetId;
        target.revisionId = option.revisionId;
        target.nameSnapshot = option.label;
        const auto request = presetSelectionTransport.writeRequest (
            identity, manifestRevision, target, juce::Time::currentTimeMillis());
        if (! request) return false;
        {
            const juce::ScopedLock lock (stateLock);
            if (requestedConfiguration.generation != configurationGeneration
                || requestedConfiguration.identity.runtimeInstanceId
                       != identity.runtimeInstanceId)
            {
                presetSelectionTransport.removeExchange (*request);
                return false;
            }
            pendingPresetSelectionRequest = request;
            pendingPresetSelectionTarget = target;
            failedPresetSelectionTarget.reset();
            currentSnapshot.presetSelectionStatus = "pending";
            currentSnapshot.presetSelectionAction.clear();
            currentSnapshot.presetSelectionTargetId = option.id;
            presetSelectionWaitingSinceMs = juce::Time::currentTimeMillis();
            presetSelectionStatusExpiresAtMs = 0;
            pendingApprovalKey.clear();
            currentSnapshot.sampleRateApprovalRequired = false;
            revokeAuditionPublication();
        }
        if (blind.ongoing()) invalidateBlind();
        else selectA();
        notify();
        return true;
    }

    bool RuntimeV2Controller::retryPresetSelection()
    {
        std::optional<PresetSelectionRequest> pending;
        std::optional<RuntimeGlobalPresetCatalogEntry> failed;
        {
            const juce::ScopedLock lock (stateLock);
            pending = pendingPresetSelectionRequest;
            failed = failedPresetSelectionTarget;
        }
        if (pending)
        {
            const auto rewritten = presetSelectionTransport.writeRequest (
                pending->identity,
                pending->manifestRevision,
                pending->selectedPreset,
                pending->requestedAtMs,
                pending->requestId);
            if (! rewritten) return false;
            const juce::ScopedLock lock (stateLock);
            if (! pendingPresetSelectionRequest
                || pendingPresetSelectionRequest->requestId != pending->requestId)
                return false;
            currentSnapshot.presetSelectionStatus = "pending";
            currentSnapshot.presetSelectionAction.clear();
            presetSelectionWaitingSinceMs = juce::Time::currentTimeMillis();
            notify();
            return true;
        }
        if (! failed) return false;
        const auto current = snapshot();
        for (const auto& option : current.presets)
            if (runtimePresetOptionIdentity (option.id) == failed->presetId
                && option.revisionId == failed->revisionId) return selectPreset (option.id);
        return false;
    }

    bool RuntimeV2Controller::selectCheck (const juce::String& id)
    {
        std::optional<CandidatePreparationRequest> cancelled;
        {
            const juce::ScopedLock lock (stateLock);
            if (id == currentSnapshot.checkId) return true;
            cancelled = pendingCandidatePreparationRequest;
            pendingCandidatePreparationRequest.reset();
            failedCandidatePreparationTarget.reset();
            currentSnapshot.candidatePreparationStatus.clear();
            currentSnapshot.candidatePreparationAction.clear();
            currentSnapshot.candidatePreparationTargetId.clear();
            candidatePreparationWaitingSinceMs = 0;
            candidatePreparationStatusExpiresAtMs = 0;
        }
        if (cancelled) candidatePreparationTransport.removeExchange (*cancelled);
        return requestSelection ("check", id);
    }

    bool RuntimeV2Controller::selectCue (const juce::String& id)
    {
        return requestSelection ("cue", id);
    }

    bool RuntimeV2Controller::approveSampleRateConversion()
    {
        const juce::ScopedLock lock (stateLock);
        if (pendingApprovalKey.isEmpty()
            || ! currentSnapshot.sampleRateApprovalRequired)
            return false;
        requestedSelection.sampleRateApprovalKey = pendingApprovalKey;
        ++requestedSelection.generation;
        notify();
        return true;
    }

    bool RuntimeV2Controller::requestRecovery()
    {
        {
            const juce::ScopedLock lock (stateLock);
            if (requestedConfiguration.identity.library) return requestLibraryRecovery();
        }
        RecoveryAuthority authority;
        RecoveryContext context;
        RecoveryDestination destination = RecoveryDestination::reference;
        {
            const juce::ScopedLock lock (stateLock);
            if (pendingRecoveryRequest.has_value()) return true;
            authority.runtimeInstanceId = requestedConfiguration.identity.runtimeInstanceId;
            authority.hostProcessId = requestedConfiguration.identity.hostProcessId;
            authority.workId = requestedConfiguration.identity.workId;
            context = { currentSnapshot.presetId, currentSnapshot.checkId,
                        currentSnapshot.candidateId };
            if (currentSnapshot.presetSelectionTargetId.isNotEmpty())
                context.presetId = runtimePresetOptionIdentity (currentSnapshot.presetSelectionTargetId);
            if (currentSnapshot.candidatePreparationTargetId.isNotEmpty())
                context.candidateId = currentSnapshot.candidatePreparationTargetId;
            if (currentSnapshot.presetSelectionAction == "choose_source")
                destination = RecoveryDestination::candidateSource;
            else if (currentSnapshot.presetSelectionAction == "measure_source")
                destination = RecoveryDestination::candidateMeasurement;
            if (currentSnapshot.candidatePreparationAction == "choose_source")
                destination = RecoveryDestination::candidateSource;
            else if (currentSnapshot.candidatePreparationAction == "measure_source")
                destination = RecoveryDestination::candidateMeasurement;
            if (currentSnapshot.rejectionCode.contains ("source"))
                destination = RecoveryDestination::candidateSource;
            else if (! currentSnapshot.measurementAvailable
                     && ! currentSnapshot.viewBindings.empty()
                     && currentSnapshot.candidateId.isNotEmpty())
                destination = RecoveryDestination::candidateMeasurement;
        }
        const auto request = recoveryTransport.writeRequest (
            authority, destination, context, juce::Time::currentTimeMillis());
        if (! request) return false;
        {
            const juce::ScopedLock lock (stateLock);
            pendingRecoveryRequest = request;
            currentSnapshot.recoveryStatus = "pending";
            recoveryStatusExpiresAtMs = 0;
            if (currentSnapshot.presetSelectionAction.isNotEmpty()
                && currentSnapshot.presetSelectionAction != "retry")
            {
                failedPresetSelectionTarget.reset();
                currentSnapshot.presetSelectionStatus.clear();
                currentSnapshot.presetSelectionAction.clear();
                currentSnapshot.presetSelectionTargetId.clear();
                presetSelectionStatusExpiresAtMs = 0;
            }
            if (currentSnapshot.candidatePreparationAction.isNotEmpty()
                && currentSnapshot.candidatePreparationAction != "retry")
            {
                failedCandidatePreparationTarget.reset();
                currentSnapshot.candidatePreparationStatus.clear();
                currentSnapshot.candidatePreparationAction.clear();
                currentSnapshot.candidatePreparationTargetId.clear();
                candidatePreparationStatusExpiresAtMs = 0;
            }
        }
        notify();
        return true;
    }

    void RuntimeV2Controller::servicePresetSelectionAcknowledgement()
    {
        constexpr std::int64_t acknowledgementTimeoutMs = 15'000;
        constexpr std::int64_t preparedDisplayMs = 3'000;
        const auto now = juce::Time::currentTimeMillis();
        std::optional<PresetSelectionRequest> request;
        {
            const juce::ScopedLock lock (stateLock);
            if (presetSelectionStatusExpiresAtMs > 0
                && now >= presetSelectionStatusExpiresAtMs)
            {
                currentSnapshot.presetSelectionStatus.clear();
                currentSnapshot.presetSelectionAction.clear();
                currentSnapshot.presetSelectionTargetId.clear();
                presetSelectionStatusExpiresAtMs = 0;
            }
            request = pendingPresetSelectionRequest;
        }
        if (! request) return;
        const auto acknowledgement = presetSelectionTransport.loadAcknowledgement (*request);
        if (! acknowledgement)
        {
            const juce::ScopedLock lock (stateLock);
            if (pendingPresetSelectionRequest
                && pendingPresetSelectionRequest->requestId == request->requestId
                && presetSelectionWaitingSinceMs > 0
                && now - presetSelectionWaitingSinceMs >= acknowledgementTimeoutMs)
            {
                currentSnapshot.presetSelectionStatus = "timed_out";
                currentSnapshot.presetSelectionAction = "retry";
            }
            return;
        }
        {
            const juce::ScopedLock lock (stateLock);
            if (! pendingPresetSelectionRequest
                || pendingPresetSelectionRequest->requestId != acknowledgement->requestId)
                return;
            if (acknowledgement->outcome == PresetSelectionOutcome::prepared
                && acknowledgement->preparedPreset)
            {
                requestedSelection.presetId = acknowledgement->preparedPreset->presetId;
                requestedSelection.checkId.clear();
                requestedSelection.candidateId.clear();
                requestedSelection.cueId.clear();
                requestedSelection.sampleRateApprovalKey.clear();
                ++requestedSelection.generation;
                currentSnapshot.presetSelectionStatus = "prepared";
                currentSnapshot.presetSelectionAction.clear();
                currentSnapshot.presetSelectionTargetId
                    = acknowledgement->preparedPreset->presetId;
                failedPresetSelectionTarget.reset();
                presetSelectionStatusExpiresAtMs = now + preparedDisplayMs;
            }
            else if (acknowledgement->recovery)
            {
                failedPresetSelectionTarget = pendingPresetSelectionTarget;
                currentSnapshot.presetSelectionStatus
                    = acknowledgement->recovery->reason;
                currentSnapshot.presetSelectionAction
                    = acknowledgement->recovery->action;
                presetSelectionStatusExpiresAtMs = 0;
            }
            pendingPresetSelectionRequest.reset();
            pendingPresetSelectionTarget.reset();
            presetSelectionWaitingSinceMs = 0;
        }
        presetSelectionTransport.removeExchange (*request);
        notify();
    }
}
