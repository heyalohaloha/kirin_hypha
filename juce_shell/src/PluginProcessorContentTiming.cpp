#include "PluginProcessor.h"

// Message thread: keep the estimate and the exact timing context used to make it. The
// diagnostics are not PRE authority, serialized state, or a measurement/Record modification.
hypha::live_compare::OffsetEstimate KirinHyphaProcessorBase::measureLiveCompareOffset()
{
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire)) return {};
    const auto* mapping = liveCompare.ring.control();
    if (mapping == nullptr || mapping->ring() == nullptr) return {};
    const auto stop = liveCompare.contentHold.generation();
    const auto timeline = liveCompare.timelineGeneration.load (std::memory_order_acquire);
    auto estimate = hypha::live_compare::measureOffset (*mapping->ring(), liveCompare.renderer);
    estimate.stopGeneration = stop;
    estimate.timelineGeneration = timeline;
    if (stop != liveCompare.contentHold.generation()
        || timeline != liveCompare.timelineGeneration.load (std::memory_order_acquire)) estimate.determined = false;
    liveCompare.offsetFacts = estimate;
    return estimate;
}

bool KirinHyphaProcessorBase::holdLiveCompareForContentJump (const hypha::live_compare::OffsetEstimate& estimate) noexcept
{
    if (role != Role::Post || ! hypha::live_compare::offsetCanVote (estimate)
        || estimate.timelineGeneration != liveCompare.timelineGeneration.load (std::memory_order_acquire)
        || estimate.stopGeneration != liveCompare.contentHold.generation()) return false;
    liveCompare.contentJumpFacts = estimate;
    liveCompare.contentJumpLagFrames.store (estimate.lagFrames, std::memory_order_relaxed);
    liveCompare.contentHold.hold (estimate.stopGeneration);
    return liveCompare.contentHold.held();
}

// Host notification, possibly off the message/audio thread. AU Reset is also used for seek:
// only an explicit, successfully read stopped transport ends a hold. Never infer stop from a
// render timeout, a reset alone, a clock discontinuity, bypass, or a playing position change.
void KirinHyphaProcessorBase::kirinHostTimelineReset (bool transportKnown, bool playing)
{
    if (transportKnown && ! playing)
    {
        lastPlaying.store (false, std::memory_order_release);
        liveCompare.contentHold.stopped();
        if (role == Role::Post) liveCompare.playbackRun.fetch_add (1, std::memory_order_acq_rel);
        liveCompare.observationReason.store (hypha::live_compare::RecoveryReason::stopped, std::memory_order_release);
    }
    liveCompare.timelineGeneration.fetch_add (1, std::memory_order_acq_rel);
    liveCompare.sessionGeneration.fetch_add (1, std::memory_order_acq_rel);
    liveCompare.matched.store (false, std::memory_order_release);
    liveCompare.hostResetGeneration.fetch_add (1, std::memory_order_release);
}

// The processor owns the content warning across window closure and live Blind. A changed
// spectrum alone never votes. Two strong estimates returning to the original baseline clear
// the hold; timing/PCM validation still independently gates every subsequent PRE block.
void KirinHyphaProcessorBase::serviceLiveContentTiming()
{
    if (role != Role::Post) return;
    const auto state = liveCompareStatus();
    const auto run = liveComparePlaybackRun();
    if (!state.active || state.compensationOff || run != liveCompare.contentMonitorRun)
    {
        liveCompare.contentMonitor = {};
        liveCompare.contentStep = {};
        liveCompare.contentMonitorRun = run;
        liveCompare.contentMonitorNext = 0;
    }
    const auto now = hypha::live_compare::callbackWallNanos();
    if (!state.active || state.compensationOff || state.verdict != hypha::live_compare::Verdict::accepted
        || now < liveCompare.contentMonitorNext) return;
    liveCompare.contentMonitorNext = now + 2'000'000'000;
    const auto estimate = measureLiveCompareOffset();
    const auto step = liveCompare.contentMonitor.observe (estimate, state.contentHeld);
    if (step.settled) liveCompare.contentStep = step;
    if (step.jumped) holdLiveCompareForContentJump (estimate);
    if (step.recovered && estimate.timelineGeneration == liveCompare.timelineGeneration.load (std::memory_order_acquire))
        liveCompare.contentHold.clear (estimate.stopGeneration);
}

hypha::live_compare::OffsetMonitor::Step KirinHyphaProcessorBase::liveContentTimingObservation() const noexcept
{
    return liveCompare.contentStep; // message thread only, the same service-owned observation
}
