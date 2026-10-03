#include "ReferenceComparisonController.h"

namespace hypha::reference_audition
{
int ReferenceComparisonController::liveWindowBlocks (int slot) const
{
    return slotController (slot).matchWindowBlocks();
}

bool ReferenceComparisonController::pendingAuditionNeedsLevel() const
{
    const juce::ScopedLock lock (selectionLock);
    return activePendingIntent.load (std::memory_order_acquire) != 0 && ! pendingAudition.resume;
}

int ReferenceComparisonController::pendingLiveWindowBlocks() const
{
    int slot = 0;
    { const juce::ScopedLock lock (selectionLock); slot = pendingAudition.view.slot; }
    return liveWindowBlocks (slot);
}

bool ReferenceComparisonController::trackingNeedsService() const noexcept
{
    return version.trackingAudible() || check.trackingAudible() || reference.trackingAudible();
}

TrackingAction ReferenceComparisonController::followAudition (const std::vector<KirinMeterHistoryEntry>& history,
                                                              double aSessionPeakDbtp)
{
    // 鳴っている役だけを動かす。同時に鳴るのは 1 役（共有の gate）。
    if (version.trackingAudible()) return version.followSelection (history, aSessionPeakDbtp);
    if (check.trackingAudible()) return check.followSelection (history, aSessionPeakDbtp);
    if (reference.trackingAudible()) return reference.followSelection (history, aSessionPeakDbtp);
    return TrackingAction::keep;
}

VersionIdentity ReferenceComparisonController::identifyVersions()
{
    const auto timeline = visual.snapshot();
    if (timeline == nullptr || timeline->aFingerprint == nullptr) return {};
    return version.identifyVersions (*timeline->aFingerprint, timeline->aFingerprintEndTick);
}

RematchResult ReferenceComparisonController::rematch (int slot, double aLoudness, double aSessionPeakDbtp)
{
    // 固定で合わせるのは C だけ（B・V は追従する）。鳴っている役だけを決め直す。
    if (slot != 2 || normalOutputSlot.load (std::memory_order_acquire) != 2) return RematchResult::notPlaying;
    return check.rematch (aLoudness, aSessionPeakDbtp);
}
}
