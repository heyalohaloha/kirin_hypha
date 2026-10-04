#include "ReferenceComparisonController.h"

// 見せているか・Blind（VERSION BLIND とローカル Blind）のあいだの観測と、Blind の枠（BlindSlot）。
// 2026-10-04：A の取り込み（capture）をやめたときに、取り込みの部品と一緒にあったものをここへ分けた。
namespace hypha::reference_audition
{
void ReferenceComparisonController::refreshObservation()
{
    const juce::ScopedLock lock (gateLock);
    const bool observing = presented && ! localBlindOwned;
    version.setContentObservationEnabled (observing);  // V の位置合わせ
    visual.setPresented (observing);
    // A を観測スレッドへ渡すのは、見せていて VERSION BLIND の外のときだけ（取り込みの部品の setPresented・pause と同じ）。
    aFeed.store (observing && ! trialActive() && ! aInputPaused);
}

bool ReferenceComparisonController::beginBlindGuard()
{
    clearPendingAudition();
    const juce::ScopedLock lock (gateLock);
    if (closing || ! blindSlot.reserve (BlindOwner::version)) return false;
    aInputPaused = true;
    aFeed.store (false);
    if (versionBlindGate && ! versionBlindGate (true))
    {
        blindSlot.release (BlindOwner::version);
        aInputPaused = false;
        refreshObservation();
        return false;
    }
    blindGuardOwned = true;
    return true;
}

void ReferenceComparisonController::endBlindGuard()
{
    const juce::ScopedLock lock (gateLock);
    if ((gateOwners & 2) != 0) return; // Keep exclusion until the RT normal-return receipt retires output.
    if (blindGuardOwned && versionBlindGate) versionBlindGate (false);
    blindGuardOwned = false;
    blindSlot.release (BlindOwner::version);
    aInputPaused = false;
    refreshObservation();
}

bool ReferenceComparisonController::reserveLocalBlind()
{
    if (heldA.held()) return false;  // 承認して A を下げているあいだは始めない（RETURN が先）
    clearPendingAudition();
    {
        const juce::ScopedLock lock (gateLock);
        if (closing || ! blindSlot.reserve (BlindOwner::local)) return false;
        localBlindOwned = true; localBlindEpoch = 0;
        visual.pauseAdmission();
        refreshObservation();
    }
    forgetHeldAudition(); // 仕様 A：ローカル Blind の後に、停止前の B／C／V へ自動で戻さない
    return true;
}

void ReferenceComparisonController::bindLocalBlind (std::uint64_t epoch)
{ const juce::ScopedLock lock (gateLock); if (localBlindOwned) localBlindEpoch = epoch; }

void ReferenceComparisonController::releaseLocalBlind (std::uint64_t epoch)
{
    const juce::ScopedLock lock (gateLock);
    if (! localBlindOwned || localBlindEpoch != epoch) return;
    localBlindOwned = false;
    blindSlot.release (BlindOwner::local);
    visual.resumeObservation();
    refreshObservation();
}
}
