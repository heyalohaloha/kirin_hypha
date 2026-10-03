#include "ReferenceComparisonController.h"

namespace hypha::reference_audition
{
// H5: 停止・シーク後の自動復帰。利用者が B／C を選んだまま（normalOutputSlot）で、その音が A に戻って
// いれば、同じ音・同じ gain で戻す保留を立てる。A を押す・試聴が止められる・音が変わると戻さない。
bool ReferenceComparisonController::resumeWanted() const
{
    const int slot = normalOutputSlot.load (std::memory_order_acquire);
    if (slot != 1 && slot != 2) return false;
    const auto& target = slot == 1 ? version : check;
    return ! target.hasOutputPath() && target.hasHeldSelection();
}

bool ReferenceComparisonController::armResume()
{
    if (! resumeWanted() || trialActive() || hasActiveWorkflow() || capture.access->busy()) return false;
    const int slot = normalOutputSlot.load (std::memory_order_acquire);
    PendingIntent next;
    next.identity = (slot == 1 ? version : check).heldPlaybackIdentity();
    if (next.identity.isEmpty()) return false;
    next.safetyEpoch = pendingSafetyEpoch.load (std::memory_order_acquire);
    next.resume = true;
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
    version.forgetHeldSelection();
    check.forgetHeldSelection();
}
}
