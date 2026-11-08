#include "ReferenceRuntimeV2Controller.h"
#include "ReferencePinnedSource.h"
#include <algorithm>
#include <utility>

namespace hypha::reference_audition
{
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
void observeReferenceSwitchVerification (const RuntimeV2Controller*, std::uint64_t, const RuntimeSource&, bool);
#endif
std::shared_ptr<const RuntimeSource> RuntimeV2Controller::loadVerifiedSelectionSource (
    const RuntimeCandidate& candidate, const RequestedSelection& selection, Snapshot& next, std::int64_t nowMs)
{
        const auto sourceLoad = sourceRepository.load (candidate);
        if (! sourceLoad.accepted())
        {
            if (songsOnly.load (std::memory_order_acquire)
                && holdSelectionForSourceRetry (sourceLoad.rejectionCode, true)) return {};
            failClosedToA();
            next.state = RuntimeState::rejected;
            next.rejectionCode = sourceLoad.rejectionCode;
            publish (std::move (next));
            return {};
        }
        auto selectedSource = sourceCache.find (candidate.sourceArtifact.sha256);
        if (selectedSource == nullptr)
            if (const auto preloaded = songPreload.held.find (candidate.sourceArtifact.sha256); preloaded != songPreload.held.end())
                selectedSource = preloaded->second;
        // The retry reservation belongs to one explicit selection of one source receipt. A new
        // selection checks its file at once (through verifySourceFile) instead of serving an
        // earlier read failure through a new reservation; a republished manifest keeps it.
        const auto retryKey = candidate.sourceArtifact.sha256 + ":" + juce::String (selection.generation);
        const bool freshSelection = sourceRetryKey != retryKey;
        if (freshSelection)
        { sourceRetryKey = retryKey; sourceRetryFailure.clear(); sourceRetryAtMs = 0; sourceRetryCount = 0; }
        const bool previouslyVerified = selectedSource != nullptr && ! freshSelection;
        if (selectedSource == nullptr)
            selectedSource = sourceLoad.source;
        const bool rejectedPageSource = currentPages().sourceInvalidated() && workerSource
            && workerSourceGeneration == selection.generation
            && workerSource->absolutePath == selectedSource->absolutePath
            && workerSource->sourceFileSha256 == selectedSource->sourceFileSha256;
        // A reservation that has run out starts one fresh attempt. Polls before it only republish
        // the stored failure; polls after it wait for the attempt the worker holds, and the
        // worker's own failure reaches this clock as the verification reason.
        const bool freshAttempt = ! rejectedPageSource
            && (freshSelection || (sourceRetryFailure.isNotEmpty() && nowMs >= sourceRetryAtMs));
       #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        if (freshAttempt) observeReferenceSwitchVerification (this, selection.generation, *selectedSource, true);
       #endif
        const bool retryCopy = revision_detail::sourceCopyFailure (sourceRetryFailure);
        if (freshAttempt) { sourceRetryFailure.clear(); sourceRetryAtMs = 0; }
        if (sourceVerification.key != retryKey) retireSourceVerification();
        const auto sourceFailure = rejectedPageSource ? juce::String ("reference_source_changed")
            : nowMs < sourceRetryAtMs ? sourceRetryFailure : previouslyVerified && !sourceVerification.check.valid()
            ? sourceRepository.verifySourceRevision (*selectedSource)
            : verifySourceBehindWorker (selectedSource, retryKey, selection.generation, freshAttempt && !retryCopy);
        if (sourceFailure == "reference_source_revalidating")
        {
            // 共有の見張りと重なっただけなら、定期の見回りを待たず間隔を広げながら確かめ直す。
            sourceRecheckSoon = !sourceVerification.check.valid();
            const auto reason = sourceFailureCode (revision_detail::service().watch (*selectedSource)->unavailable.load (std::memory_order_acquire));
            if (!holdSelectionForSourceRetry (reason.isNotEmpty() ? reason : sourceFailure, reason.isNotEmpty())
                && !ready.load (std::memory_order_acquire))
            {
                next.state = RuntimeState::waiting; next.rejectionCode = sourceFailure;
                publish (std::move (next));
            }
            return {};
        }
        if (sourceFailure.isNotEmpty())
        {
            const bool keepsSelection = holdSelectionForSourceRetry (sourceFailure);
            if (nowMs >= sourceRetryAtMs)
            {
                sourceRetryFailure = sourceFailure;
                sourceRetryAtMs = nowMs + (keepsSelection && !revision_detail::sourceCopyFailure (sourceFailure) ? 1000
                    : std::min (60000, 5000 * (1 << std::min (sourceRetryCount++, 4))));
            }
            sourceCache.forget (candidate.sourceArtifact.sha256);
            if (sourceFailure == "reference_source_cache_full" && sourceCache.releaseUnused())
            { sourceRetryAtMs = nowMs; sourceRetryCount = 0; }
            if (keepsSelection) return {};
            failClosedToA();
            next.state = sourceFailure == "reference_source_read_failed" || revision_detail::sourceCopyFailure (sourceFailure)
                ? RuntimeState::waiting : RuntimeState::rejected;
            next.rejectionCode = sourceFailure;
            publish (std::move (next));
            return {};
        }
       #if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        observeReferenceSwitchVerification (this, selection.generation, *selectedSource, false);
       #endif
        holdSelectionForSourceRetry ({});
        sourceRetryFailure.clear(); sourceRetryAtMs = 0; sourceRetryCount = 0; sourceRecheckPolls = 1;
        sourceCache.remember (candidate.sourceArtifact.sha256, selectedSource);
        return selectedSource;
}
}
