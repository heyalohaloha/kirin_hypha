#include "ReferenceRuntimeV2Controller.h"

#include <algorithm>

namespace hypha::reference_audition
{
    bool RuntimeV2Controller::selectCandidate (const juce::String& id)
    {
        RuntimeSelectionOption option;
        RuntimeIdentity identity;
        CandidatePreparationTarget target;
        std::optional<CandidatePreparationRequest> cancelled;
        std::int64_t manifestRevision = 0;
        std::uint64_t configurationGeneration = 0;
        bool restorePublishedSelection = false;
        {
            const juce::ScopedLock lock (stateLock);
            const auto found = std::find_if (
                currentSnapshot.candidates.begin(), currentSnapshot.candidates.end(),
                [&] (const auto& item) { return item.id == id; });
            if (found == currentSnapshot.candidates.end()) return false;
            if (pendingPresetSelectionRequest) return false;
            option = *found;
            currentSnapshot.presetSelectionStatus.clear();
            currentSnapshot.presetSelectionAction.clear();
            currentSnapshot.presetSelectionTargetId.clear();
            presetSelectionStatusExpiresAtMs = 0;
            if (! option.requiresPreparation)
            {
                cancelled = pendingCandidatePreparationRequest;
                pendingCandidatePreparationRequest.reset();
                failedCandidatePreparationTarget.reset();
                currentSnapshot.candidatePreparationStatus.clear();
                currentSnapshot.candidatePreparationAction.clear();
                currentSnapshot.candidatePreparationTargetId.clear();
                candidatePreparationWaitingSinceMs = 0;
                candidatePreparationStatusExpiresAtMs = 0;
                restorePublishedSelection = id == currentSnapshot.candidateId
                    && currentSnapshot.state == RuntimeState::ready
                    && publishedSource != nullptr;
                manifestRevision = -1;
            }
            else
            {
                if (pendingCandidatePreparationRequest)
                    return pendingCandidatePreparationRequest->target.candidateId == id;
                identity = requestedConfiguration.identity;
                configurationGeneration = requestedConfiguration.generation;
                manifestRevision = currentSnapshot.manifestRevision;
                target = { currentSnapshot.presetId, option.revisionId,
                           currentSnapshot.checkId, id };
            }
        }
        if (manifestRevision == -1)
        {
            if (cancelled) candidatePreparationTransport.removeExchange (*cancelled);
            if (restorePublishedSelection)
            {
                ready.store (true, std::memory_order_release);
                return true;
            }
            return requestSelection ("candidate", id);
        }
        const auto request = candidatePreparationTransport.writeRequest (
            identity, manifestRevision, target, juce::Time::currentTimeMillis());
        if (! request) return false;
        {
            const juce::ScopedLock lock (stateLock);
            if (requestedConfiguration.generation != configurationGeneration
                || requestedConfiguration.identity.runtimeInstanceId
                       != identity.runtimeInstanceId)
            {
                candidatePreparationTransport.removeExchange (*request);
                return false;
            }
            pendingCandidatePreparationRequest = request;
            failedCandidatePreparationTarget.reset();
            currentSnapshot.candidatePreparationStatus = "pending";
            currentSnapshot.candidatePreparationAction.clear();
            currentSnapshot.candidatePreparationTargetId = id;
            candidatePreparationWaitingSinceMs = juce::Time::currentTimeMillis();
            candidatePreparationStatusExpiresAtMs = 0;
            pendingApprovalKey.clear();
            currentSnapshot.sampleRateApprovalRequired = false;
            revokeAuditionPublication();
        }
        if (blind.ongoing()) invalidateBlind();
        else selectA();
        notify();
        return true;
    }

    bool RuntimeV2Controller::retryCandidatePreparation()
    {
        std::optional<CandidatePreparationRequest> pending;
        std::optional<CandidatePreparationTarget> failed;
        {
            const juce::ScopedLock lock (stateLock);
            pending = pendingCandidatePreparationRequest;
            failed = failedCandidatePreparationTarget;
        }
        if (pending)
        {
            const auto rewritten = candidatePreparationTransport.writeRequest (
                pending->identity, pending->manifestRevision, pending->target,
                pending->requestedAtMs, pending->requestId);
            if (! rewritten) return false;
            const juce::ScopedLock lock (stateLock);
            if (! pendingCandidatePreparationRequest
                || pendingCandidatePreparationRequest->requestId != pending->requestId) return false;
            currentSnapshot.candidatePreparationStatus = "pending";
            currentSnapshot.candidatePreparationAction.clear();
            candidatePreparationWaitingSinceMs = juce::Time::currentTimeMillis();
            notify();
            return true;
        }
        if (! failed) return false;
        const auto current = snapshot();
        if (current.presetId != failed->presetId || current.checkId != failed->checkId)
            return false;
        for (const auto& option : current.candidates)
            if (option.id == failed->candidateId && option.revisionId == failed->presetRevisionId
                && option.requiresPreparation) return selectCandidate (option.id);
        return false;
    }

    void RuntimeV2Controller::serviceCandidatePreparationAcknowledgement()
    {
        constexpr std::int64_t acknowledgementTimeoutMs = 15'000;
        constexpr std::int64_t preparedDisplayMs = 3'000;
        const auto now = juce::Time::currentTimeMillis();
        std::optional<CandidatePreparationRequest> request;
        {
            const juce::ScopedLock lock (stateLock);
            if (candidatePreparationStatusExpiresAtMs > 0
                && now >= candidatePreparationStatusExpiresAtMs)
            {
                currentSnapshot.candidatePreparationStatus.clear();
                currentSnapshot.candidatePreparationAction.clear();
                currentSnapshot.candidatePreparationTargetId.clear();
                candidatePreparationStatusExpiresAtMs = 0;
            }
            request = pendingCandidatePreparationRequest;
        }
        if (! request) return;
        const auto acknowledgement = candidatePreparationTransport.loadAcknowledgement (*request);
        if (! acknowledgement)
        {
            const juce::ScopedLock lock (stateLock);
            if (pendingCandidatePreparationRequest
                && pendingCandidatePreparationRequest->requestId == request->requestId
                && candidatePreparationWaitingSinceMs > 0
                && now - candidatePreparationWaitingSinceMs >= acknowledgementTimeoutMs)
            {
                currentSnapshot.candidatePreparationStatus = "timed_out";
                currentSnapshot.candidatePreparationAction = "retry";
            }
            return;
        }
        {
            const juce::ScopedLock lock (stateLock);
            if (! pendingCandidatePreparationRequest
                || pendingCandidatePreparationRequest->requestId != acknowledgement->requestId) return;
            if (acknowledgement->outcome == PresetSelectionOutcome::prepared
                && acknowledgement->preparedCandidate)
            {
                const auto& prepared = *acknowledgement->preparedCandidate;
                requestedSelection.presetId = prepared.presetId;
                requestedSelection.checkId = prepared.checkId;
                requestedSelection.candidateId = prepared.candidateId;
                requestedSelection.cueId.clear();
                requestedSelection.sampleRateApprovalKey.clear();
                ++requestedSelection.generation;
                currentSnapshot.candidatePreparationStatus = "prepared";
                currentSnapshot.candidatePreparationAction.clear();
                currentSnapshot.candidatePreparationTargetId = prepared.candidateId;
                failedCandidatePreparationTarget.reset();
                candidatePreparationStatusExpiresAtMs = now + preparedDisplayMs;
            }
            else if (acknowledgement->recovery)
            {
                failedCandidatePreparationTarget = request->target;
                currentSnapshot.candidatePreparationStatus = acknowledgement->recovery->reason;
                currentSnapshot.candidatePreparationAction = acknowledgement->recovery->action;
                candidatePreparationStatusExpiresAtMs = 0;
            }
            pendingCandidatePreparationRequest.reset();
            candidatePreparationWaitingSinceMs = 0;
        }
        candidatePreparationTransport.removeExchange (*request);
        notify();
    }
}
