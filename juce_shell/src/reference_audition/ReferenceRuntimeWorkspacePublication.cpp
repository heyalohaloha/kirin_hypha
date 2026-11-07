#include "ReferenceRuntimeV2Controller.h"
#include "ReferencePinnedSource.h"
#include "ReferenceRuntimeV2PlaybackIdentity.h"
#include "ReferenceTestCommitObserver.h"
#include <cmath>
#include <limits>
#include <utility>

namespace hypha::reference_audition
{
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
void observeReferenceSwitchPages (const RuntimeV2Controller*, std::uint64_t, const RuntimeSource&, bool);
#endif
namespace
{
std::int64_t outputSample (std::int64_t sample, std::int64_t from, std::int64_t to)
{ return from > 0 ? static_cast<std::int64_t> (std::llround (static_cast<long double> (sample) * to / from)) : 0; }
double unavailable() noexcept { return std::numeric_limits<double>::quiet_NaN(); }
}

// audible：鳴っている B を替えるときの新しい MATCH。呼ぶ側が stateLock を持ち、PCM・位置・gain を一度に渡す。
void RuntimeV2Controller::finishWorkspaceSelection (WorkspaceSelection prepared, const PreparedNormalSelection* audible)
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
    if (prepared.staged && audible == nullptr)
    {
        activePages.store (&nextPages(), std::memory_order_release);
        activeSourceKey = sourceKey;
    }
    if (audible == nullptr && currentPages().sourceInvalidated() && prepared.staged)
    {
        failClosedToA(); next.state = RuntimeState::rejected;
        next.rejectionCode = "reference_source_changed"; publish (std::move (next)); return;
    }
    if (audible == nullptr && (sourceKey != activeSourceKey || ! currentPages().sourceOpen()))
    {
        revokeAuditionPublication();
        if (blind.ongoing())
            invalidateBlind();
        else
            selectA();
        KIRIN_REFERENCE_STAGE_FOR_TEST (this, "pages_close"); currentPages().close();
        KIRIN_REFERENCE_STAGE_FOR_TEST (this, "pages_open");
        const auto pinned = std::make_shared<revision_detail::PinnedSource> (selectedSource);
       #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        observeReferenceSwitchPages (this, selection.generation, *selectedSource, true);
       #endif
        const auto openFailure = currentPages().open (*selectedSource, configuration.sampleRate,
            configuration.channels, true, [this, pinned] (RuntimeFileRevision& revision) { return pinned->verified (sourceRepository, revision); },
            pinned->input(), [pinned] { return pinned->sameIdentity(); }, [pinned] { return pinned->wasRejected(); },
                pinned->authorityOwner(), pinned->invalidation(), pinned->initialInvalidation(), [pinned] { return pinned->readSucceeded(); });
       #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        observeReferenceSwitchPages (this, selection.generation, *selectedSource, false);
       #endif
        if (openFailure.isNotEmpty() || currentPages().sourceInvalidated())
        {
            next.state = openFailure == "reference_source_revalidating" ? RuntimeState::waiting : RuntimeState::rejected;
            next.rejectionCode = openFailure.isNotEmpty() ? openFailure : "reference_source_changed";
            publish (std::move (next));
            return;
        }
        activeSourceKey = sourceKey;
    }
    next.sourceStorageNotice = revision_detail::snapshotFor (*selectedSource)->storageNotice();
    KIRIN_REFERENCE_STAGE_FOR_TEST (this, "pages_open_done"); workerSource = selectedSource;
    workerSourceGeneration = selection.generation;

    const auto cueKey = runtimeCuePlaybackIdentity (*cue);
    const auto mappingKey = sourceKey + ":" + cueKey;
    if (audible != nullptr || activeMappingKey != mappingKey)
    {
        const juce::ScopedLock mappingLock (mappingWriteLock);
        if (audible != nullptr) normalSwitchWriting.store (true, std::memory_order_release);
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
        if (audible != nullptr)
        {
            activePages.store (&nextPages(), std::memory_order_release);
            bLinearGain.store (audible->linearGain, std::memory_order_release);
            normalSwitchSerial.fetch_add (1, std::memory_order_acq_rel);
            activeSourceKey = sourceKey;
        }
        mappingGeneration.fetch_add (1, std::memory_order_release);
        if (audible != nullptr) normalSwitchWriting.store (false, std::memory_order_release);
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
    serviceBlindPreparation (configuration, *candidate, *cue, selectedSource, next, &prepared.versionPosition);
    KIRIN_REFERENCE_STAGE_FOR_TEST (this, "adoption_write");
    if (!configuration.identity.library && prepared.adoptionKey != activePresetAdoptionKey
        && presetAdoptionTransport.write (configuration.identity, prepared.event.manifestRevision,
            prepared.sourceTemplateArtifact, prepared.event.presetArtifact, prepared.preparedAtMs))
        activePresetAdoptionKey = prepared.adoptionKey;
    {
        const juce::ScopedLock lock (stateLock);
        if (requestedConfiguration.generation != configuration.generation
            || requestedSelection.generation != selection.generation) return;
        versionHoldSinceMs.store (0, std::memory_order_release);
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
        if (audible != nullptr)
        {
            // 前の曲の記録は次に A が聴こえたときに閉じる（役の切替と同じ。A へは戻っていない）。
            requestAuditionReturnEvent (aAudibleConfirmations.load (std::memory_order_acquire));
            beginMatchLocked();
            applyPreparedFactsLocked (*audible);
            normalSelectionGeneration.fetch_add (1, std::memory_order_acq_rel);
            beginAuditionEventSession (bAudibleConfirmations.load (std::memory_order_acquire));
            songBankRetiringSinceMs = juce::jmax<std::int64_t> (1, juce::Time::currentTimeMillis());
        }
    }
    // The new publication can already be selected while the old reader retires.
    if (prepared.staged && audible == nullptr) nextPages().close();
    if (selectionReady && announcedSelectionGeneration != selection.generation)
    {
        announcedSelectionGeneration = selection.generation;
        selectionReady();
    }
}
}
