#include "ReferenceRuntimeV2Controller.h"
#include "ReferencePinnedSource.h"
#include "ReferenceRuntimeV2PlaybackIdentity.h"
#include "ReferenceTestCommitObserver.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace hypha::reference_audition
{
static_assert (std::atomic<AudioPages*>::is_always_lock_free);
namespace
{
std::int64_t outputSample (std::int64_t sample, std::int64_t from, std::int64_t to)
{ return from > 0 ? static_cast<std::int64_t> (std::llround (static_cast<long double> (sample) * to / from)) : 0; }
double unavailable() noexcept { return std::numeric_limits<double>::quiet_NaN(); }
}

bool RuntimeV2Controller::beginSelectionPreparationLocked() noexcept
{
    if (prepareBeforeReturn.load (std::memory_order_acquire)
        && bSelected.load (std::memory_order_acquire) && ready.load (std::memory_order_acquire)
        && latestPlaying.load (std::memory_order_acquire) && !blind.ongoing())
    {
        normalSelectionGeneration.fetch_add (1, std::memory_order_acq_rel);
        return true;
    }
    revokeAfterFadeLocked();
    return false;
}

void RuntimeV2Controller::clearPreparedWorkspace()
{
    preparedWorkspace.reset();
    const juce::ScopedLock lock (stateLock);
    preparedReturn = {};
}

bool RuntimeV2Controller::awaitingPreparedLevel() const
{
    const juce::ScopedLock lock (stateLock);
    return preparedReturn.pending && !preparedReturn.available && preparedReturn.level != PreparedLevel::none
        && preparedReturn.generation == requestedSelection.generation && bSelected.load (std::memory_order_acquire);
}

int RuntimeV2Controller::preparedMatchWindowBlocks() const
{
    const juce::ScopedLock lock (stateLock);
    return preparedReturn.pending && preparedReturn.generation == requestedSelection.generation
        ? preparedReturn.windowBlocks : currentSnapshot.cueWindowBlocks;
}

void RuntimeV2Controller::supplyPreparedLevel (double loudness, double peak)
{
    const juce::ScopedLock lock (stateLock);
    if (preparedReturn.pending && preparedReturn.generation == requestedSelection.generation)
    {
        preparedReturn.available = preparedReturn.level == PreparedLevel::none
            || (preparedReturn.level == PreparedLevel::loudness && std::isfinite (loudness))
            || (preparedReturn.level == PreparedLevel::peak && std::isfinite (peak));
        if (preparedReturn.available) notify();
    }
}

bool RuntimeV2Controller::servicePreparedWorkspace (const Configuration& configuration)
{
    if (!preparedWorkspace) return false;
    auto& prepared = *preparedWorkspace;
    std::shared_ptr<const RuntimeSource> audibleSource;
    {
        const juce::ScopedLock lock (stateLock);
        if (configuration.generation != prepared.configuration.generation
            || requestedSelection.generation != prepared.selection.generation)
        { clearPreparedWorkspace(); return false; }
        if (bSelected.load (std::memory_order_acquire)) audibleSource = publishedSource;
    }
    // A withdrawn publication cannot gain authority by finishing an earlier preparation.
    const auto loaded = configuration.identity.library ? repository.refreshLibrary (workspace)
        : repository.refresh (configuration.identity.workId, workspace);
    // A hash still running keeps the preparation. A read that already failed (revalidate with a
    // reason) cannot retry from here, so the preparation is dropped and refreshWorkspace takes the
    // same selection to its retry reservation; a playing source in that state returns to A.
    const auto waiting = [] (const SourceRevisionResult& r)
    { return r.state == SourceRevisionState::revalidate && r.reason.isEmpty(); };
    const auto preparedRevision = sourceRepository.checkSourceRevision (*prepared.source);
    if (waiting (preparedRevision)) return true;
    if (!loaded.workspace || loaded.rejectionCode.isNotEmpty()
        || loaded.workspace->publicationHash != workspace->publicationHash
        || preparedRevision.state != SourceRevisionState::matched)
    { clearPreparedWorkspace(); return false; }
    if (audibleSource && audibleSource != prepared.source)
    {
        const auto audibleRevision = sourceRepository.checkSourceRevision (*audibleSource);
        if (waiting (audibleRevision)) return true;
        if (audibleRevision.state != SourceRevisionState::matched) failClosedToA();
    }
    auto& staging = nextPages();
    const auto start = outputSample (prepared.cue.startSample, prepared.cue.sampleRateHz, prepared.next.hostSampleRateHz);
    const auto end = outputSample (prepared.cue.endSample, prepared.cue.sampleRateHz, prepared.next.hostSampleRateHz);
    staging.service();
    if (staging.sourceInvalidated())
    {
        auto rejected = prepared.next;
        rejected.state = RuntimeState::rejected; rejected.rejectionCode = "reference_source_changed";
        clearPreparedWorkspace(); failClosedToA(); publish (std::move (rejected)); return true;
    }
    if (!staging.readyAt (start, static_cast<int> (std::min<std::int64_t> (8192, end - start)))) return true;
    if (!prepared.returnStarted)
    {
        const juce::ScopedLock lock (stateLock);
        // A new Cue can require a longer A window. Keep the old C throughout
        // that preparation too; the actual new MATCH still runs after return.
        if (preparedReturn.level != PreparedLevel::none && !preparedReturn.available
            && bSelected.load (std::memory_order_acquire)) return true;
        if (requestedSelection.generation != prepared.selection.generation)
        { clearPreparedWorkspace(); return false; }
        revokeAfterFadeLocked();
        selectA();
        prepared.returnStarted = true;
        KIRIN_REFERENCE_COMMIT_FOR_TEST();
    }
    if (deferredRevokeWaiting()) return true;
    auto completed = std::move (prepared);
    clearPreparedWorkspace();
    finishWorkspaceSelection (std::move (completed));
    return true;
}

void RuntimeV2Controller::finishWorkspaceSelection (WorkspaceSelection prepared)
{
    KIRIN_REFERENCE_STAGE_FOR_TEST (this, "finish_selection_enter");
    const auto& configuration = prepared.configuration;
    const auto& selection = prepared.selection;
    auto& next = prepared.next;
    const auto* candidate = &prepared.candidate;
    const auto* cue = &prepared.cue;
    const auto selectedSource = prepared.source;
    const auto publicationKey = next.playbackIdentity;
    const auto mappedCueStart = outputSample (cue->startSample, cue->sampleRateHz, next.hostSampleRateHz);
    const auto mappedCueEnd = outputSample (cue->endSample, cue->sampleRateHz, next.hostSampleRateHz);
    const auto& sourceKey = prepared.sourceKey;
    if (prepared.staged)
    {
        activePages.store (&nextPages(), std::memory_order_release);
        activeSourceKey = sourceKey;
    }
    if (currentPages().sourceInvalidated() && prepared.staged)
    {
        failClosedToA(); next.state = RuntimeState::rejected;
        next.rejectionCode = "reference_source_changed"; publish (std::move (next)); return;
    }
    if (sourceKey != activeSourceKey || ! currentPages().sourceOpen())
    {
        revokeAuditionPublication();
        if (blind.ongoing())
            invalidateBlind();
        else
            selectA();
        KIRIN_REFERENCE_STAGE_FOR_TEST (this, "pages_close"); currentPages().close();
        KIRIN_REFERENCE_STAGE_FOR_TEST (this, "pages_open");
        const auto pinned = std::make_shared<revision_detail::PinnedSource> (selectedSource);
        const auto openFailure = currentPages().open (*selectedSource, configuration.sampleRate,
            configuration.channels, true, [this, pinned] (RuntimeFileRevision& revision) { return pinned->verified (sourceRepository, revision); },
            pinned->input(), [pinned] { return pinned->sameIdentity(); }, [pinned] { return pinned->wasRejected(); },
                pinned->authorityOwner(), pinned->invalidation(), pinned->initialInvalidation());
        if (openFailure.isNotEmpty() || currentPages().sourceInvalidated())
        {
            next.state = RuntimeState::rejected;
            next.rejectionCode = openFailure.isNotEmpty() ? openFailure : "reference_source_changed";
            publish (std::move (next));
            return;
        }
        activeSourceKey = sourceKey;
    }
    KIRIN_REFERENCE_STAGE_FOR_TEST (this, "pages_open_done"); workerSource = selectedSource;
    workerSourceGeneration = selection.generation;

    const auto cueKey = runtimeCuePlaybackIdentity (*cue);
    const auto mappingKey = activeSourceKey + ":" + cueKey;
    if (activeMappingKey != mappingKey)
    {
        const juce::ScopedLock mappingLock (mappingWriteLock);
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
    // Cue の頭から鳴らし直す役（DAW の位置に合わせない曲）も、周回して頭へ戻るので頭のページを読んでおく。
    currentPages().setPinnedCue (mappedCueStart, mappedCueEnd, cue->loopEnabled || restartsAtCueStart());
    // MATCH compares A with what is played: the opened file's channels on this track's channels.
    next.sourceLoudnessOffsetLu = currentPages().playbackLoudnessOffsetLu();
    next.sourceIntegratedLoudness = selectedSource->measurementSummary
        && selectedSource->measurementSummary->loudnessLufsI
        ? *selectedSource->measurementSummary->loudnessLufsI : unavailable();
    next.sourceMaximumTruePeakDbtp = selectedSource->measurementSummary
        && selectedSource->measurementSummary->maximumTruePeakDbtp
        ? *selectedSource->measurementSummary->maximumTruePeakDbtp : unavailable();
    KIRIN_REFERENCE_STAGE_FOR_TEST (this, "blind_preparation");
    serviceBlindPreparation (configuration, *candidate, *cue, selectedSource, next);
    KIRIN_REFERENCE_STAGE_FOR_TEST (this, "adoption_write");
    if (!configuration.identity.library && prepared.adoptionKey != activePresetAdoptionKey
        && presetAdoptionTransport.write (configuration.identity, prepared.event.manifestRevision,
            prepared.sourceTemplateArtifact, prepared.event.presetArtifact, prepared.preparedAtMs))
        activePresetAdoptionKey = prepared.adoptionKey;
    {
        const juce::ScopedLock lock (stateLock);
        if (requestedConfiguration.generation != configuration.generation
            || requestedSelection.generation != selection.generation) return;
        requestedSelection.presetId = next.presetId;
        requestedSelection.checkId = next.checkId;
        requestedSelection.candidateId = next.candidateId;
        requestedSelection.cueId = prepared.songEntry ? juce::String {} : next.cueId;
        activeEventContext = prepared.event;
        activeEventCandidate = *candidate; activeEventCue = *cue; activeEventSource = selectedSource;
        appliedSelectionGeneration.store (selection.generation, std::memory_order_release);
        KIRIN_REFERENCE_STAGE_FOR_TEST (this, "publish_ready"); publishReady (std::move (next), selectedSource, *cue);
        KIRIN_REFERENCE_STAGE_FOR_TEST (this, "publish_ready_done");
        activePublishedSelectionKey = publicationKey;
    }
    // The new publication can already be selected while the old reader retires.
    if (prepared.staged) nextPages().close();
}
}
