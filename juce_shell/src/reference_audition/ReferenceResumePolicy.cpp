#include "ReferenceComparisonController.h"

namespace hypha::reference_audition
{
// 停止・シーク後の自動復帰。利用者が B／C／V を選んだまま（normalOutputSlot）で、その音が A に戻って
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
        if (state.state == RuntimeState::rejected
            || state.rejectionCode == "reference_selection_unavailable" || state.rejectionCode == "reference_version_unselected")
        { dropResume(); return false; }                        // 待っても鳴らない（選んだものが消えた）
        if (state.playbackIdentity.isEmpty()) return true;     // 準備中
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
    if (next.switching && rememberedPress.slot == slot && rememberedPress.generation == switchGeneration)
    { next.pressLoudness = rememberedPress.loudness; next.pressPeak = rememberedPress.peak; }  // 曲の外で押した時の A
    rememberedPress = {};
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

void ReferenceComparisonController::reconcileOutputMark()
{
    const int marked = normalOutputSlot.load (std::memory_order_acquire);
    for (int slot = 1; slot <= 3; ++slot)
        if (slotController (slot).takeHeldWithdrawn() && marked == slot && activePendingIntent.load (std::memory_order_acquire) == 0
            && switchSlot.load (std::memory_order_acquire) != slot)
        {
            const auto reason = slotController (slot).snapshot().rejectionCode;
            const juce::ScopedLock lock (selectionLock);
            pendingAudition = {};
            pendingAudition.view = { slot, PendingAuditionView::Stage::sourceChanged, reason };
        }
    if (marked < 1 || marked > 3 || switchSlot.load (std::memory_order_acquire) == marked
        || activePendingIntent.load (std::memory_order_acquire) != 0) return;
    const auto& target = slotController (marked);
    if (target.outputSelected() || target.hasOutputPath() || target.hasHeldSelection()) return;
    auto expected = marked;
    normalOutputSlot.compare_exchange_strong (expected, 0, std::memory_order_acq_rel);
}

void ReferenceComparisonController::continueAfterSwitch (int slot, bool continues)
{
    reconcileOutputMark();  // 控えも出力も無い役の古い印で、選択の替えを「鳴っている役の替え」と取り違えない
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

// 替えている途中で、鳴っている音へ選び直した。替える途中なら鳴っている役は公開より先に A へ戻るので、
// 選択が公開されてもまだ鳴っているなら、それは今の音で、替える物は無い。
void ReferenceComparisonController::settleSwitchBack()
{
    auto slot = switchSlot.load (std::memory_order_acquire);
    if (slot < 1 || slot > 3 || ! slotController (slot).outputSelected()) return;
    std::uint64_t wanted = 0;
    { const juce::ScopedLock lock (selectionLock); wanted = switchGeneration; }
    if (slotController (slot).snapshot().selectionGeneration >= wanted)
        switchSlot.compare_exchange_strong (slot, 0, std::memory_order_acq_rel);
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
