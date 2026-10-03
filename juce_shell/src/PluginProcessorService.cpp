#include "PluginProcessor.h"
#include "reference_audition/ReferenceTrackingGain.h"

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
    serviceReferenceTracking();
    if (writesEnabled.load (std::memory_order_acquire) && ! localBlindProductSession.needsService()
        && ! heldFormat.held && ! liveCompareNeedsService() && ! referencePendingAuditionNeedsService()
        && ! referenceTrackingNeedsService())
        stopTimer();
    else
    {
        // Reuse the processor's existing service timer, at 4 Hz for idle clock preparation.
        // An explicit audition/return or another pending workflow keeps its normal 20 Hz.
        const bool idleTiming = writesEnabled.load (std::memory_order_acquire) && ! heldFormat.held
            && ! localBlindProductSession.needsService() && ! referencePendingAuditionNeedsService()
            && ! liveCompare.sessionActive.load (std::memory_order_acquire)
            && ! liveCompare.completion.pending() && liveCompare.blindScope == 0
            && ! liveCompare.authority.restoring();
        const int interval = idleTiming ? 250 : 50;
        if (getTimerInterval() != interval) startTimer (interval);
    }
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
    const auto level = referenceLiveALevel (false, referenceAuditionController->pendingLiveWindowBlocks());
    referenceAuditionController->servicePendingAudition (level.loudness, level.peak, live);
   #endif
}

bool KirinHyphaProcessorBase::referenceTrackingNeedsService() const
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    return referenceAuditionController && referenceAuditionController->trackingNeedsService();
   #else
    return false;
   #endif
}

// H3: B・V を聴いているあいだ、1 秒ごとに A の直近 10 秒で gain を求め直す（Audio Thread で 50 ms の ramp）。
void KirinHyphaProcessorBase::serviceReferenceTracking()
{
   #if ! KIRIN_HYPHA_PRE_DISPLAY
    const auto now = juce::Time::getMillisecondCounterHiRes();
    if (! referenceTrackingNeedsService())
    {
        referenceTrackingNextAtMs = now + hypha::reference_audition::trackingIntervalSeconds * 1000.0;
        return;
    }
    if (now < referenceTrackingNextAtMs) return;
    referenceTrackingNextAtMs = now + hypha::reference_audition::trackingIntervalSeconds * 1000.0;
    std::vector<KirinMeterHistoryEntry> history;
    if (! pollMeterHistory (KIRIN_METER_HISTORY_10_HZ, history, hypha::reference_audition::liveWindowBlocks,
                            hypha::reference_audition::liveWindowBlocks))
        return;
    referenceAuditionController->followAudition (history, referenceLiveALevel (true).peak);
   #endif
}
