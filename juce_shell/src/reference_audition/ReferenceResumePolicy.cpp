#include "ReferenceComparisonController.h"

namespace hypha::reference_audition
{
// H5: 停止・シーク後の自動復帰。利用者が B／C／V を選んだまま（normalOutputSlot）で、その音が A に戻って
// いれば、同じ音・同じ gain で戻す保留を立てる。A を押す・試聴が止められる・音が変わると戻さない。
// 選択を替えた役（switchSlot）は、新しい選択が公開されたら新しい MATCH で鳴らす（戻すのではない）。
bool ReferenceComparisonController::resumeWanted() const
{
    const int slot = normalOutputSlot.load (std::memory_order_acquire);
    if (slot < 1 || slot > 3) return false;
    const auto& target = slotController (slot);
    return ! target.hasOutputPath()
        && (target.hasHeldSelection() || switchSlot.load (std::memory_order_acquire) == slot);
}

bool ReferenceComparisonController::armResume()
{
    if (! resumeWanted() || trialActive()) return false;
    const int slot = normalOutputSlot.load (std::memory_order_acquire);
    auto& target = slotController (slot);
    PendingIntent next;
    if (switchSlot.load (std::memory_order_acquire) == slot)
    {
        const auto state = target.snapshot();
        std::uint64_t wanted = 0;
        { const juce::ScopedLock lock (selectionLock); wanted = switchGeneration; }
        if (state.selectionGeneration < wanted) return true;  // まだ新しい選択が公開されていない。timer は回し続ける
        if (state.state == RuntimeState::rejected) { dropResume(); return false; }
        if (state.playbackIdentity.isEmpty()) return true;     // 準備中
        switchSlot.store (0, std::memory_order_release);
        next.identity = state.playbackIdentity;
        next.switching = true;
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
    switchSlot.store (0, std::memory_order_release);
    version.forgetHeldSelection();
    check.forgetHeldSelection();
    reference.forgetHeldSelection();
}

void ReferenceComparisonController::continueAfterSwitch (int slot, bool continues)
{
    bool pendingHere = false;
    {
        const juce::ScopedLock lock (selectionLock);
        pendingHere = activePendingIntent.load (std::memory_order_acquire) != 0 && pendingAudition.view.slot == slot;
    }
    if (pendingHere) clearPendingAudition();
    auto& target = slotController (slot);
    target.forgetHeldSelection();
    if (switchSlot.load (std::memory_order_acquire) == slot) switchSlot.store (0, std::memory_order_release);
    if (! continues)
    {
        auto expected = slot;
        normalOutputSlot.compare_exchange_strong (expected, 0, std::memory_order_acq_rel);
        return;
    }
    if (! pendingHere && normalOutputSlot.load (std::memory_order_acquire) != slot) return;
    { const juce::ScopedLock lock (selectionLock); switchGeneration = target.requestedGeneration(); }
    switchSlot.store (slot, std::memory_order_release);
    normalOutputSlot.store (slot, std::memory_order_release);
}

// 仕様 A：鳴っている役はそのまま（利用者が選んで聴いている）。A に戻っている選択と、押した後の待ちを忘れる。
void ReferenceComparisonController::forgetHeldAudition()
{
    clearPendingAudition();
    switchSlot.store (0, std::memory_order_release);
    version.forgetHeldSelection();
    check.forgetHeldSelection();
    reference.forgetHeldSelection();
    const int slot = normalOutputSlot.load (std::memory_order_acquire);
    if (slot < 1 || slot > 3 || ! slotController (slot).outputSelected())
        normalOutputSlot.store (0, std::memory_order_release);
}
}
