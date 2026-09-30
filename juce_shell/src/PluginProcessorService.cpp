#include "PluginProcessor.h"

void KirinHyphaProcessorBase::timerCallback()
{
    // B-126 + Logic stopped-state fix: non-RT enable poll on the message thread.
    if (! writesEnabled.load (std::memory_order_acquire)
        && enablePending.load (std::memory_order_acquire))
    {
        const int ticks = enableDelayTicks.load (std::memory_order_acquire);
        if (ticks > 0)
            enableDelayTicks.store (ticks - 1, std::memory_order_release);
        else
            enableWritesNow();
    }
    applyHeldFormatIfRecordReleased();
    serviceLocalBlindProductSession();
    serviceLiveCompare();
    serviceReferencePendingAudition();
    if (writesEnabled.load (std::memory_order_acquire) && ! localBlindProductSession.needsService()
        && ! heldFormat.held && ! liveCompareNeedsService() && ! referencePendingAuditionNeedsService())
        stopTimer();
}

bool KirinHyphaProcessorBase::referencePendingAuditionNeedsService() const
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return referenceAuditionController && referenceAuditionController->pendingAuditionNeedsService();
   #else
    return false;
   #endif
}

void KirinHyphaProcessorBase::serviceReferencePendingAudition()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    if (!referencePendingAuditionNeedsService()) return;
    if (!licenseIsOs()) { referenceAuditionController->suspendAudition(); return; }
    const bool live = heartbeatLive();
    const auto level = referenceLiveALevel();
    referenceAuditionController->servicePendingAudition (level.loudness, level.peak, live);
   #endif
}
