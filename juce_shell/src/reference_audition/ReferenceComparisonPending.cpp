#include "ReferenceComparisonController.h"
#include <cmath>

namespace hypha::reference_audition
{
namespace
{
using Stage = PendingAuditionView::Stage;
}

void ReferenceComparisonController::clearPendingAudition()
{
    activePendingIntent.store (0, std::memory_order_release);
    const juce::ScopedLock lock (selectionLock);
    version.setQueuedContentObservationEnabled (false);
    pendingAudition = {};
}

bool ReferenceComparisonController::pendingAuditionNeedsService() const
{
    return activePendingIntent.load (std::memory_order_acquire) != 0;
}

void ReferenceComparisonController::appendPendingAudition (
    Snapshot& state, const VisualBinding& b, const VisualBinding& c) const
{
    // snapshot() already holds selectionLock. A prepared identity need not be audible yet.
    state.pendingAudition = pendingAudition.view;
    const auto& versionState = *state.versionSelection;
    const auto chosen = versionState.presetId + "/" + versionState.checkId + "/" + versionState.candidateId;
    state.versionArmable = b.source != nullptr && state.selectedVersionId.isNotEmpty()
        && state.selectedVersionId == chosen && versionState.playbackIdentity.isNotEmpty()
        && versionState.blindPhase == BlindPhase::inactive;
    state.checkArmable = c.source != nullptr && !c.hidden && state.checkSelection->playbackIdentity.isNotEmpty();
}

bool ReferenceComparisonController::requestAudition (int slot, double loudness, double peak)
{
    if (slot != 1 && slot != 2) return false;
    const auto safety = pendingSafetyEpoch.load (std::memory_order_acquire);
    const auto state = snapshot();
    if (state.transportPlaying) return slot == 1 ? selectB (loudness, peak) : selectC (loudness, peak);
    if (trialActive() || hasActiveWorkflow() || capture.access->busy()
        || !(slot == 1 ? state.versionArmable : state.checkArmable)) return false;
    { const juce::ScopedLock lock (gateLock); if (localBlindOwned || blindGuardOwned || captureOwned) return false; }
    const auto identity = (slot == 1 ? *state.versionSelection : *state.checkSelection).playbackIdentity;
    if (identity.isEmpty()) return false;
    selectA();
    PendingIntent next;
    next.identity = identity; next.safetyEpoch = safety;
    next.view = { slot, Stage::play };
    { const juce::ScopedLock lock (selectionLock);
      next.intentId = ++pendingSequence; pendingAudition = std::move (next);
      version.setQueuedContentObservationEnabled (slot == 1);
      activePendingIntent.store (pendingSequence, std::memory_order_release); }
    return true;
}

void ReferenceComparisonController::servicePendingAudition (double loudness, double peak, bool callbackLive)
{
    PendingIntent intent;
    { const juce::ScopedLock lock (selectionLock); intent = pendingAudition; }
    if (!intent.view.waiting() || activePendingIntent.load (std::memory_order_acquire) != intent.intentId) return;
    const auto publish = [&] (Stage stage, bool sawPlayback = false)
    {
        const juce::ScopedLock lock (selectionLock);
        if (pendingAudition.intentId == intent.intentId)
        {
            pendingAudition.view.stage = stage; pendingAudition.sawPlayback |= sawPlayback;
            if (!pendingAudition.view.waiting())
            {
                auto expected = intent.intentId; activePendingIntent.compare_exchange_strong (expected, 0);
                version.setQueuedContentObservationEnabled (false);
            }
        }
    };
    auto& target = intent.view.slot == 1 ? version : check;
    const auto state = target.snapshot();
    const auto inputSafety = pendingInputSafety.load (std::memory_order_acquire);
    if (intent.safetyEpoch != pendingSafetyEpoch.load (std::memory_order_acquire)
        || (intent.sawPlayback && (!state.transportPlaying || !callbackLive))
        || (state.transportPlaying && inputSafety == 0)
        || trialActive() || hasActiveWorkflow())
    { publish (Stage::safetyChanged); return; }
    const auto binding = target.visualBinding();
    if (state.state == RuntimeState::rejected || state.state == RuntimeState::disconnected
        || (state.playbackIdentity.isNotEmpty() && state.playbackIdentity != intent.identity))
    { publish (Stage::sourceChanged); return; }
    if (state.sampleRateApprovalRequired) { publish (Stage::approval, state.transportPlaying); return; }
    if (!state.transportPlaying) { publish (Stage::play); return; }
    if (!callbackLive || inputSafety != 1
        || !state.transportPositionValid || state.state != RuntimeState::ready
        || !state.auditionBuffered || !binding.source)
    { publish (Stage::checking, callbackLive); return; }
    if (intent.view.slot == 2 && ((state.comparisonMode == "loudness_match" && !std::isfinite (loudness))
        || (state.comparisonMode == "peak_match" && !std::isfinite (peak))))
    { publish (Stage::level, true); return; }
    RuntimeV2SourceRepository verifier (juce::File {});
    if (verifier.verifySourceRevision (*binding.source).isNotEmpty())
    { publish (Stage::sourceChanged); return; }
    // Claim once on the control thread, then use the runtime generation across gain preparation.
    // A, a source change or reconfiguration invalidates that generation before publication.
    // Bind preparation to the same condition even if a publication changes after this snapshot.
    // Manual and queued selection share the same no-fallback MATCH policy.
    const auto generation = target.normalSelectionTicket();
    auto expected = intent.intentId;
    if (!activePendingIntent.compare_exchange_strong (expected, 0, std::memory_order_acq_rel)) return;
    if (!target.selectB (loudness, peak, generation, intent.identity))
    {
        const auto failure = target.snapshot().matchFailure;
        publish (failure == MatchFailure::ceilingExceeded ? Stage::ceilingExceeded
            : failure == MatchFailure::sourceLevelUnavailable ? Stage::sourceLevelUnavailable
            : Stage::startFailed, true);
        return;
    }
    normalOutputSlot.store (intent.view.slot, std::memory_order_release);
    publish (Stage::none, true);
}
}
