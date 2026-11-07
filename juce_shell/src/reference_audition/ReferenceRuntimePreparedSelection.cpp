#include "ReferenceRuntimeV2Controller.h"
#include "ReferencePinnedSource.h"
#include "ReferenceRuntimeV2PlaybackIdentity.h"
#include "ReferenceTestCommitObserver.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <system_error>
#include <utility>

namespace hypha::reference_audition
{
static_assert (std::atomic<AudioPages*>::is_always_lock_free);
namespace
{
// 取り消した裏の処理の終わりは待たない（作業スレッドと、stateLock を待つ画面を止めない）。破棄のときだけ待つ。
template <typename Result>
void retireBehindWorker (std::future<Result>& running, std::vector<std::future<Result>>& retired)
{
    if (running.valid()) retired.push_back (std::move (running));
    retired.erase (std::remove_if (retired.begin(), retired.end(), [] (const auto& search)
        { return search.wait_for (std::chrono::seconds (0)) != std::future_status::timeout; }), retired.end());
}
std::int64_t outputSample (std::int64_t sample, std::int64_t from, std::int64_t to)
{ return from > 0 ? static_cast<std::int64_t> (std::llround (static_cast<long double> (sample) * to / from)) : 0; }
double unavailable() noexcept { return std::numeric_limits<double>::quiet_NaN(); }
}

bool RuntimeV2Controller::beginSelectionPreparationLocked() noexcept
{
    pendingSourceFailureSinceMs = 0;
    pendingSourceFailureReason.clear();
    currentSnapshot.pendingSourceFailure.clear();
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

bool RuntimeV2Controller::holdSelectionForSourceRetry (const juce::String& failure, bool confirmed)
{
    const juce::ScopedLock lock (stateLock);
    if (!prepareBeforeReturn.load (std::memory_order_acquire)
        || !bSelected.load (std::memory_order_acquire) || !ready.load (std::memory_order_acquire)
        || requestedSelection.generation == currentSnapshot.selectionGeneration
        || blind.ongoing()) return false;
    if (failure.isEmpty())
    { pendingSourceFailureSinceMs = 0; pendingSourceFailureReason.clear(); currentSnapshot.pendingSourceFailure.clear(); }
    else if (confirmed || failure != "reference_source_revalidating")
    {
        if (pendingSourceFailureSinceMs == 0) pendingSourceFailureSinceMs = juce::Time::currentTimeMillis();
        pendingSourceFailureReason = failure;
    }
    if (pendingSourceFailureSinceMs != 0
        && (confirmed || juce::Time::currentTimeMillis() - pendingSourceFailureSinceMs >= 3000))
        currentSnapshot.pendingSourceFailure = pendingSourceFailureReason;
    if (workerSource) sourceRepository.verifySourceRevision (*workerSource);
    return true;
}

void RuntimeV2Controller::retireVersionSearch (std::future<RuntimeContentAlignment>& search)
{ retireBehindWorker (search, retiredVersionSearches); }

void RuntimeV2Controller::retireSourceVerification()
{
    retireBehindWorker (sourceVerification.check, retiredSourceVerifications);
    sourceVerification = {};
}

bool RuntimeV2Controller::verifyingSource (std::uint64_t generation) const
{
    return sourceVerification.check.valid() && sourceVerification.generation == generation
        && sourceVerification.check.wait_for (std::chrono::seconds (0)) == std::future_status::timeout;
}

juce::String RuntimeV2Controller::verifySourceBehindWorker (
    std::shared_ptr<const RuntimeSource>& source, const juce::String& key, std::uint64_t generation, bool freshAttempt)
{
    auto& running = sourceVerification;
    if (!running.check.valid())
    {
        try
        {
            running.check = std::async (stagedSearchLaunch, [repository = sourceRepository, source, freshAttempt]
                { return repository.verifySourceFile (*source, freshAttempt); });
        }
        catch (const std::system_error&) { return "reference_source_revalidating"; }  // 次の周期にやり直す
        running.key = key; running.generation = generation; running.source = source;
    }
    if (running.check.wait_for (std::chrono::seconds (0)) == std::future_status::timeout) return "reference_source_revalidating";
    juce::String failure;
    try { failure = running.check.get(); } catch (...) { failure = "reference_source_revalidating"; }
    // Carry the verified private copy across refreshes into playback preparation.
    if (failure.isEmpty()) source = running.source;
    running = {};
    return failure;
}

// 探すのは別の thread。作れなければ空を返す（作業スレッドで探さず、次の周期にやり直す）。
std::future<RuntimeContentAlignment> RuntimeV2Controller::searchAlignment (
    std::shared_ptr<const RuntimeACaptureAudio> capture, std::shared_ptr<const RuntimeSource> source,
    std::shared_ptr<const RuntimeDetailedMeasurement> measurement, std::optional<std::int64_t> expected)
{
    try
    {
        return std::async (stagedSearchLaunch, [capture = std::move (capture), source = std::move (source), measurement = std::move (measurement), expected]
            { return alignReferenceContent (*capture, *source, *measurement, true, expected); });
    }
    catch (const std::system_error&) { return {}; }
}

void RuntimeV2Controller::clearPreparedWorkspace()
{
    versionHoldSinceMs.store (0, std::memory_order_release);
    if (preparedWorkspace) retireVersionSearch (preparedWorkspace->versionPosition.search);
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

// この期限は位置合わせだけに使う。音源の読取り・検証と再試行の時間は含めない。
void RuntimeV2Controller::serviceVersionHold()
{
    const juce::ScopedLock lock (stateLock);
    const auto since = versionHoldSinceMs.load (std::memory_order_acquire);
    if (since == 0)
    { versionHoldSinceMs.store (juce::jmax (1u, juce::Time::getMillisecondCounter()), std::memory_order_release); return; }
    if (bSelected.load (std::memory_order_acquire) && juce::Time::getMillisecondCounter() - since < stagedVersionLimitMs) return;
    versionHoldSinceMs.store (0, std::memory_order_release);
    if (!bSelected.load (std::memory_order_acquire)) return;
    revokeAfterFadeLocked();
    selectA();
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
    if (!loaded.workspace || loaded.rejectionCode.isNotEmpty()
        || loaded.workspace->publicationHash != workspace->publicationHash)
    { clearPreparedWorkspace(); return false; }
    // Keep the audible source and the requested choice while the shared monitor retries.
    // Page output independently pauses only a confirmed unavailable source.
    if (audibleSource && audibleSource != prepared.source)
        sourceRepository.verifySourceRevision (*audibleSource);
    auto sourceFailure = sourceRepository.verifySourceRevision (*prepared.source);
    // Keep the control contract retryable. Only the pending notice takes the
    // confirmed reason; it must not revoke the source currently being auditioned.
    if (sourceFailure.isNotEmpty())
        sourceFailure = sourceFailureCode (revision_detail::service().watch (*prepared.source)->unavailable.load (std::memory_order_acquire));
    holdSelectionForSourceRetry (sourceFailure, sourceFailure.isNotEmpty());
    if (sourceFailure.isNotEmpty())
    { versionHoldSinceMs.store (0, std::memory_order_release); return true; }
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
    if (!versionComparison && !staging.readyAt (start, static_cast<int> (std::min<std::int64_t> (8192, end - start)))) return true;
    if (!prepared.returnStarted && versionComparison && !stagedVersionReady (prepared, staging)) return true;
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
    retireVersionSearch (completed.versionPosition.search);
    clearPreparedWorkspace();
    finishWorkspaceSelection (std::move (completed));
    return true;
}

// V は鳴っている Version を保ったまま、新しい Version の位置を A の直近で確かめ、その位置の音を読み終えてから替える。
// 止まった・A に戻ったなら保つ音が無いので待たない。
bool RuntimeV2Controller::stagedVersionReady (WorkspaceSelection& prepared, AudioPages& staging)
{
    auto& staged = prepared.versionPosition;
    if (!bSelected.load (std::memory_order_acquire) || !latestPlaying.load (std::memory_order_acquire)
        || !prepared.next.detailedMeasurement) return true;
    const auto continuity = aCapture.continuity();
    if (staged.capture != nullptr && staged.capture->continuity != continuity)
    { retireVersionSearch (staged.search); staged.capture.reset(); staged.match.reset(); }  // シークの後は窓がたまるまで待つ
    if (staged.search.valid())
    {
        if (staged.search.wait_for (std::chrono::seconds (0)) == std::future_status::timeout) return false;
        RuntimeContentAlignment found;
        try { found = staged.search.get(); } catch (...) { found.reason = "reference_alignment_source_unavailable"; }
        if (found.established) staged.match = std::make_shared<RuntimeContentAlignment> (std::move (found));
        else if (found.reason == "reference_alignment_busy" || found.reason == "reference_alignment_source_unavailable")
        {
            // No content comparison completed. Retry the same capture without
            // charging a miss or letting storage time expire the audible V.
            staged.capture.reset(); versionHoldSinceMs.store (0, std::memory_order_release);
            return false;
        }
        else if (++staged.misses >= 2) return true;  // この Version は A の今の所と合わない。A の待ちが理由を言う
    }
    if (staged.match == nullptr)
    {
        serviceVersionHold();
        if (!bSelected.load (std::memory_order_acquire)) return true;
        const auto capture = aCapture.currentAudio();
        if (capture == nullptr || capture->continuity != continuity || capture == staged.capture) return false;
        staged.capture = capture;
        staged.search = searchAlignment (capture, prepared.source, prepared.next.detailedMeasurement, std::nullopt);
        if (!staged.search.valid()) staged.capture.reset();
        return false;
    }
    const auto hostRate = prepared.next.hostSampleRateHz;
    const auto position = outputSample (staged.match->sourceStartSample, prepared.source->audio.sampleRateHz, hostRate)
        + latestHostPosition.load (std::memory_order_acquire) - staged.capture->startSample;
    const auto remaining = staging.lengthInSamples() - position;
    if (position < 0 || remaining <= 0) return true;  // 範囲の外。A の待ちが理由を言う
    staging.request (position); staging.service();
    return staging.readyAt (position, static_cast<int> (std::min<std::int64_t> (hostRate / 2, remaining)));
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
                pinned->authorityOwner(), pinned->invalidation(), pinned->initialInvalidation(), [pinned] { return pinned->readSucceeded(); });
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
    }
    // The new publication can already be selected while the old reader retires.
    if (prepared.staged) nextPages().close();
}
}
