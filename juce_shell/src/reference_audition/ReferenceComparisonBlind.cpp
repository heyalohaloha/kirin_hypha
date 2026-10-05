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
    // A を観測スレッドへ渡すのは、見せていて VERSION BLIND の外のときだけ。VERSION BLIND は始めてから終了を押すまで
    // （aInputPaused）。Blind の状態（trialActive）は見ない：終了の直後はまだ A へ戻す途中で、後から見直す呼び出しが無い。
    aFeed.store (observing && ! aInputPaused);
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
    blindSessionOpen.store (true, std::memory_order_release);
    return true;
}

// VERSION BLIND の終わり（END でも、ほかの終わり方でも）。試験は終わりなので A はすぐ観測へ戻す（V の画面・B と C の
// A の値・AUTO）。ほかの Blind との排他は、V がまだ出力を持っていれば（A へ戻す途中）返し終えたとき（admit）に解く：
// その前に解くと、V が鳴っているあいだにほかの Blind が始まる。何度呼んでも同じ。
void ReferenceComparisonController::finishVersionBlindSession()
{
    const juce::ScopedLock lock (gateLock);
    aInputPaused = false;
    if ((gateOwners & 2) == 0) releaseVersionBlindGuard();
    blindSessionOpen.store (blindGuardOwned, std::memory_order_release);
    refreshObservation();
}

// 中の Blind が END を通らずに終わった（始まる前の取り消し・DAW の状態の読み込み・形式の作り直し・ライセンスの失効）
// なら、持ち物を片付ける。定期の処理（servicePendingAudition）から呼ぶ。
void ReferenceComparisonController::reconcileVersionBlindSession()
{
    if (blindSessionOpen.load (std::memory_order_acquire) && ! trialActive()) finishVersionBlindSession();
}

// gateLock を持って呼ぶ。VERSION BLIND のほかの Blind との排他（Blind の枠と、同じ project・process の Hypha）を解く。
void ReferenceComparisonController::releaseVersionBlindGuard()
{
    if (blindGuardOwned && versionBlindGate) versionBlindGate (false);
    blindGuardOwned = false;
    blindSlot.release (BlindOwner::version);
    blindSessionOpen.store (aInputPaused, std::memory_order_release);
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
