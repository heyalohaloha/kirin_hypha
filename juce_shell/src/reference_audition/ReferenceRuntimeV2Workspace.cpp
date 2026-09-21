#include "ReferenceRuntimeV2Controller.h"
#include "ReferenceLibraryLegacyChoice.h"
#include "ReferenceRuntimeV2PlaybackIdentity.h"
#include "ReferenceRuntimePresetOptions.h"

#include <cmath>
#include <limits>

namespace hypha::reference_audition
{
    namespace
    {
        const RuntimePreset* findPreset (const RuntimeWorkspace& workspace,
                                         const juce::String& id)
        {
            for (const auto& preset : workspace.presets)
                if (preset.sourcePresetArtifact.presetId == id)
                    return &preset;
            return nullptr;
        }

        void appendPresetOptions (Snapshot& snapshot, const RuntimeWorkspace& workspace)
        {
            appendRuntimePresetOptions (snapshot, workspace);
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
                if (candidate.prepared && (id.isEmpty() || candidate.candidateId == id))
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

    void RuntimeV2Controller::refreshWorkspace (const Configuration& configuration,
                                                std::int64_t nowMs)
    {
        if (! configuration.identity.valid())
            return;
        if (! configuration.identity.library) writeCapability (root, configuration.identity, nowMs);
        libraryOnline.store (configuration.identity.library && repository.libraryOnline (nowMs),
                             std::memory_order_release);
        activeABinding = aBindingRepository.load (configuration.identity, nowMs);
        RequestedSelection selection;
        { const juce::ScopedLock lock (stateLock); selection = requestedSelection; }
        aCapture.service (activeABinding,
                          static_cast<std::int64_t> (std::llround (
                              configuration.sampleRate)),
                          configuration.channels, nowMs,
                          versionComparison && configuration.identity.library
                              ? configuration.identity.runtimeInstanceId : juce::String {});
        const auto loaded = configuration.identity.library ? repository.refreshLibrary (workspace)
            : repository.refresh (configuration.identity.workId, workspace);
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
        if (versionComparison && workspace->independentVersions)
        {
            const ReferenceChoice original { selection.presetId, selection.checkId, selection.candidateId, selection.cueId };
            const auto key = workspace->publicationHash + ":" + original.target() + ":" + juce::String (selection.generation);
            if (legacyVersionLookupKey != key)
            {
                legacyVersionLookupKey = key;
                const auto id = migrateLegacyVersionChoice (root, *workspace, original);
                if (id.isNotEmpty())
                {
                    const juce::ScopedLock lock (stateLock);
                    if (requestedSelection.generation == selection.generation)
                    {
                        legacyVersionChoice = original.target();
                        selection.presetId = id; selection.checkId = id; selection.candidateId = id; selection.cueId = id;
                        requestedSelection = selection;
                    }
                }
            }
        }
        libraryReceived.store (workspace->library, std::memory_order_release);
        appliedSelectionGeneration = selection.generation;
        const auto missingSelection = [&] {
            failClosedToA();
            Snapshot next;
            next.state = RuntimeState::waiting;
            next.rejectionCode = "reference_selection_unavailable";
            next.presetId = selection.presetId; next.checkId = selection.checkId;
            next.candidateId = selection.candidateId; next.cueId = selection.cueId;
            next.manifestRevision = workspace->manifest.revision;
            appendPresetOptions (next, *workspace);
            publish (std::move (next));
        };
        const RuntimePreset* preset = findPreset (*workspace, selection.presetId);
        if (workspace->library && selection.presetId.isNotEmpty() && preset == nullptr)
        { missingSelection(); return; }
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
            if (preset != nullptr) { next.presetId = preset->sourcePresetArtifact.presetId; next.presetName = preset->name; }
            next.manifestRevision = workspace->manifest.revision;
            appendPresetOptions (next, *workspace);
            publish (std::move (next));
            return;
        }
        const RuntimeCheck* check = findCheck (*preset, selection.checkId);
        if (workspace->library && selection.checkId.isNotEmpty() && check == nullptr)
        { missingSelection(); return; }
        if (check == nullptr) check = &preset->checks.front();
        if (check->candidates.empty())
        {
            failClosedToA();
            Snapshot next;
            next.state = RuntimeState::waiting;
            next.rejectionCode = "reference_candidates_empty";
            next.presetId = preset->sourcePresetArtifact.presetId;
            next.presetName = preset->name;
            next.checkId = check->checkId;
            next.checkLabel = check->label;
            next.manifestRevision = workspace->manifest.revision;
            next.hostSampleRateHz = static_cast<std::int64_t> (
                std::llround (configuration.sampleRate));
            appendPresetOptions (next, *workspace);
            for (const auto& item : preset->checks)
                next.checks.push_back ({ item.checkId, item.label, {}, false });
            publish (std::move (next));
            return;
        }
        const RuntimeCandidate* candidate = findCandidate (*check, selection.candidateId);
        if (candidate == nullptr) candidate = findCandidate (*check, {});
        if (workspace->library)
        {
            candidate = &check->candidates.front();
            for (const auto& item : check->candidates)
                if (item.candidateId == selection.candidateId) candidate = &item;
            if (selection.candidateId.isNotEmpty() && candidate->candidateId != selection.candidateId)
            { missingSelection(); return; }
        }
        if (candidate == nullptr || ! candidate->prepared || candidate->cues.empty())
        {
            failClosedToA();
            Snapshot next;
            next.state = RuntimeState::waiting;
            next.rejectionCode = candidate != nullptr && ! candidate->prepared
                ? "reference_source_unavailable" : "reference_cues_empty";
            next.presetId = preset->sourcePresetArtifact.presetId;
            next.presetName = preset->name;
            next.checkId = check->checkId;
            next.checkLabel = check->label;
            if (candidate != nullptr) { next.candidateId = candidate->candidateId; next.candidateName = candidate->displayName; }
            next.manifestRevision = workspace->manifest.revision;
            next.hostSampleRateHz = static_cast<std::int64_t> (
                std::llround (configuration.sampleRate));
            appendPresetOptions (next, *workspace);
            for (const auto& item : preset->checks)
                next.checks.push_back ({ item.checkId, item.label, {}, false });
            for (const auto& item : check->candidates)
                next.candidates.push_back ({ item.candidateId, item.displayName,
                    preset->sourcePresetArtifact.revisionId, ! item.prepared });
            publish (std::move (next));
            return;
        }
        const RuntimeCue* cue = findCue (*candidate, selection.cueId);
        if (workspace->library && selection.cueId.isNotEmpty() && cue == nullptr)
        { missingSelection(); return; }
        if (cue == nullptr) cue = findCue (*candidate, candidate->defaultCueId);
        if (cue == nullptr) cue = &candidate->cues.front();

        if (selection.workflowCondition)
        {
            const auto& expected = *selection.workflowCondition;
            const bool exact = expected.comparison == "a_c"
                && preset->sourcePresetArtifact.presetId == expected.presetId
                && preset->sourcePresetArtifact.revisionId == expected.presetRevisionId
                && preset->sourcePresetArtifact.sha256 == expected.presetSha256
                && check->checkId == expected.checkId
                && candidate->candidateId == expected.candidateId
                && candidate->sourceKind == expected.sourceKind
                && candidate->sourceIdentityKey == expected.sourceIdentityKey
                && cue->cueId == expected.cueId && cue->label == expected.cueLabel
                && cue->startSample == expected.cueStart && cue->endSample == expected.cueEnd
                && cue->sampleRateHz == expected.cueRate && cue->loopEnabled == expected.cueLoops;
            if (! exact)
            {
                failClosedToA();
                Snapshot rejected;
                rejected.state = RuntimeState::rejected;
                rejected.rejectionCode = "reference_workflow_condition_changed";
                rejected.presetId = selection.presetId;
                rejected.checkId = selection.checkId;
                rejected.candidateId = selection.candidateId;
                rejected.cueId = selection.cueId;
                rejected.manifestRevision = workspace->manifest.revision;
                appendPresetOptions (rejected, *workspace);
                publish (std::move (rejected));
                return;
            }
        }

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
        next.workflowToken = selection.workflowCondition ? selection.workflowToken : juce::String {};
        next.sourceKind = candidate->sourceKind;
        next.comparisonMode = check->comparisonMode;
        next.presentationLayout = RuntimeV2PresentationRepository::text (
            presentationRepository.load (configuration.identity.workId));
        next.viewBindings = check->viewBindings;
        next.hostSampleRateHz = static_cast<std::int64_t> (std::llround (configuration.sampleRate));
        next.aBindingAvailable = activeABinding.has_value();
        next.aRecordingId = activeABinding ? activeABinding->recordingId : juce::String {};
        next.aCaptureAvailable = aCapture.currentReceipt().has_value();
        next.manifestRevision = workspace->manifest.revision;
        appendPresetOptions (next, *workspace);
        for (const auto& item : preset->checks)
            next.checks.push_back ({ item.checkId, item.label, {}, false });
        for (const auto& item : check->candidates)
            next.candidates.push_back ({ item.candidateId, item.displayName,
                preset->sourcePresetArtifact.revisionId, ! item.prepared });
        for (const auto& item : candidate->cues)
            next.cues.push_back ({ item.cueId, item.label, {}, false });

        const auto sourceLoad = sourceRepository.load (*candidate);
        if (! sourceLoad.accepted())
        {
            failClosedToA();
            next.state = RuntimeState::rejected;
            next.rejectionCode = sourceLoad.rejectionCode;
            publish (std::move (next));
            return;
        }
        auto selectedSource = sourceCache.find (candidate->sourceArtifact.sha256);
        const bool previouslyVerified = selectedSource != nullptr;
        if (! previouslyVerified)
            selectedSource = sourceLoad.source;
        const auto sourceFailure = previouslyVerified
            ? sourceRepository.verifySourceRevision (*selectedSource)
            : sourceRepository.verifySourceFile (*selectedSource);
        if (sourceFailure.isNotEmpty())
        {
            sourceCache.forget (candidate->sourceArtifact.sha256);
            failClosedToA();
            next.state = RuntimeState::rejected;
            next.rejectionCode = sourceFailure;
            publish (std::move (next));
            return;
        }
        if (! previouslyVerified)
            sourceCache.remember (candidate->sourceArtifact.sha256, selectedSource);
        const auto selectedCueLabel = cue->label;
        RuntimeCue wholeVersionCue;
        if (versionComparison && candidate->sourceKind == "work_version")
        {
            wholeVersionCue = *cue;
            wholeVersionCue.startSample = 0;
            wholeVersionCue.endSample = selectedSource->audio.totalSampleFrames;
            wholeVersionCue.sampleRateHz = selectedSource->audio.sampleRateHz;
            wholeVersionCue.loopEnabled = false;
            wholeVersionCue.label = "Full song";
            cue = &wholeVersionCue;
            next.cueLabel = wholeVersionCue.label;
            next.comparisonMode = "loudness_match";
        }
        const auto mediaKey = workspace->library ? runtimeSourceAudioIdentity (*selectedSource)
            : candidate->sourceArtifact.sha256;
        const auto publicationKey = runtimeSelectionPlaybackIdentity (
            *preset, *check, *candidate, *cue, workspace->library ? mediaKey : juce::String {});
        if (publicationKey != activePublishedSelectionKey)
        {
            revokeAuditionPublication();
            if (blind.ongoing())
                invalidateBlind();
            else
                selectA();
        }
        next.sourceSampleRateHz = selectedSource->audio.sampleRateHz;
        const auto approvalKey = mediaKey + ":"
            + juce::String (selectedSource->audio.sampleRateHz) + ":"
            + juce::String (next.hostSampleRateHz);
        const bool rateDiffers = selectedSource->audio.sampleRateHz != next.hostSampleRateHz;
        const bool rateApproved = ! rateDiffers || selection.sampleRateApprovalKey == approvalKey;
        if (! rateApproved)
        {
            failClosedToA();
            next.state = RuntimeState::waiting;
            next.sampleRateApprovalRequired = true;
            next.rejectionCode = "reference_sample_rate_approval_required";
            publishApprovalRequired (std::move (next), approvalKey);
            return;
        }

        const auto sourceKey = mediaKey + ":"
            + juce::String (next.hostSampleRateHz) + ":"
            + (rateDiffers ? "converted" : "native");
        if (sourceKey != activeSourceKey || ! pages.sourceOpen())
        {
            revokeAuditionPublication();
            if (blind.ongoing())
                invalidateBlind();
            else
                selectA();
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
        workerSource = selectedSource;
        const auto measurement = measurementRepository.load (*selectedSource);
        if (measurement.accepted())
        {
            next.detailedMeasurement = measurement.measurement;
            next.measurementAvailable = true;
        }
        const auto alignment = alignmentRepository.load (*selectedSource);
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
        const auto cueKey = runtimeCuePlaybackIdentity (*cue);
        const auto mappingKey = activeSourceKey + ":" + cueKey;
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
        pages.setPinnedCue (mappedCueStart, mappedCueEnd, cue->loopEnabled);
        next.sourceIntegratedLoudness = selectedSource->measurementSummary
            && selectedSource->measurementSummary->loudnessLufsI
            ? *selectedSource->measurementSummary->loudnessLufsI : unavailable();
        next.sourceMaximumTruePeakDbtp = selectedSource->measurementSummary
            && selectedSource->measurementSummary->maximumTruePeakDbtp
            ? *selectedSource->measurementSummary->maximumTruePeakDbtp : unavailable();
        serviceBlindPreparation (configuration, *candidate, *cue, selectedSource, next);
        const auto adoptionKey = configuration.identity.runtimeInstanceId + ":"
            + juce::String (configuration.identity.hostProcessId) + ":"
            + configuration.identity.workId + ":"
            + juce::String (workspace->manifest.revision) + ":"
            + preset->sourceTemplateArtifact.sha256 + ":"
            + preset->sourcePresetArtifact.sha256;
        if (! configuration.identity.library && adoptionKey != activePresetAdoptionKey
            && presetAdoptionTransport.write (
                configuration.identity,
                workspace->manifest.revision,
                preset->sourceTemplateArtifact,
                preset->sourcePresetArtifact,
                nowMs))
            activePresetAdoptionKey = adoptionKey;
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
                selectedCueLabel,
                next.comparisonMode,
                preset->versionEntry,
            };
            activeEventCandidate = *candidate;
            activeEventCue = *cue;
            activeEventSource = selectedSource;
        }
        publishReady (std::move (next), selectedSource);
        activePublishedSelectionKey = publicationKey;
    }

}
