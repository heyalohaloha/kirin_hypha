#include "ReferenceComparisonController.h"

namespace hypha::reference_audition
{
int ReferenceComparisonController::liveWindowBlocks (int slot) const
{
    return slot == 1 ? version.matchWindowBlocks() : check.matchWindowBlocks();
}

int ReferenceComparisonController::pendingLiveWindowBlocks() const
{
    int slot = 0;
    { const juce::ScopedLock lock (selectionLock); slot = pendingAudition.view.slot; }
    return liveWindowBlocks (slot);
}

bool ReferenceComparisonController::trackingNeedsService() const noexcept
{
    return version.trackingAudible() || check.trackingAudible();
}

TrackingAction ReferenceComparisonController::followAudition (const std::vector<KirinMeterHistoryEntry>& history,
                                                              double aSessionPeakDbtp)
{
    // 鳴っている役だけを動かす。同時に鳴るのは 1 役（共有の gate）。
    if (version.trackingAudible()) return version.followSelection (history, aSessionPeakDbtp);
    if (check.trackingAudible()) return check.followSelection (history, aSessionPeakDbtp);
    return TrackingAction::keep;
}
}
