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
    if (writesEnabled.load (std::memory_order_acquire) && ! localBlindProductSession.needsService()
        && ! heldFormat.held && ! liveCompareNeedsService())
        stopTimer();
}
