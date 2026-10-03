#include "ReferenceComparisonController.h"

namespace hypha::reference_audition
{
// H5: 停止・シーク後の自動復帰。利用者が B／C を選んだまま（normalOutputSlot）で、その音が A に戻って
// いれば、同じ音・同じ gain で戻す保留を立てる。A を押す・試聴が止められる・音が変わると戻さない。
bool ReferenceComparisonController::resumeWanted() const
{
    const int slot = normalOutputSlot.load (std::memory_order_acquire);
    if (slot < 1 || slot > 3) return false;
    const auto& target = slotController (slot);
    return ! target.hasOutputPath()
        && (target.hasHeldSelection() || (slot == 3 && songSwitchPending.load (std::memory_order_acquire)));
}

bool ReferenceComparisonController::armResume()
{
    if (! resumeWanted() || trialActive() || hasActiveWorkflow() || capture.access->busy()) return false;
    const int slot = normalOutputSlot.load (std::memory_order_acquire);
    auto& target = slotController (slot);
    PendingIntent next;
    if (slot == 3 && songSwitchPending.load (std::memory_order_acquire))
    {
        // H8: B のまま曲を替えた。新しい曲が公開されたら、新しい MATCH で鳴らす（戻すのではない）。
        const auto state = target.snapshot();
        juce::String chosen;
        { const juce::ScopedLock lock (selectionLock); chosen = songId; }
        if (state.state == RuntimeState::rejected) { dropResume(); return false; }
        if (state.playbackIdentity.isEmpty() || state.presetId + "/" + state.checkId + "/" + state.candidateId != chosen)
            return true;  // まだ準備中。timer は回し続ける
        songSwitchPending.store (false, std::memory_order_release);
        next.identity = state.playbackIdentity;
    }
    else
    {
        next.identity = target.heldPlaybackIdentity();
        if (next.identity.isEmpty()) return false;
        next.resume = true;
    }
    next.safetyEpoch = pendingSafetyEpoch.load (std::memory_order_acquire);
    next.view = { slot, PendingAuditionView::Stage::play };
    const juce::ScopedLock lock (selectionLock);
    if (activePendingIntent.load (std::memory_order_acquire) != 0) return true;
    next.intentId = ++pendingSequence;
    pendingAudition = std::move (next);
    version.setQueuedContentObservationEnabled (slot == 1);
    activePendingIntent.store (pendingSequence, std::memory_order_release);
    return true;
}

void ReferenceComparisonController::dropResume()
{
    normalOutputSlot.store (0, std::memory_order_release);
    songSwitchPending.store (false, std::memory_order_release);
    version.forgetHeldSelection();
    check.forgetHeldSelection();
    reference.forgetHeldSelection();
}
}
