#include "PluginProcessor.h"

void KirinHyphaProcessorBase::invalidateLiveComparePair() noexcept
{
    if (role != Role::Post) return;
    liveCompare.pairRevocationGeneration.store (liveCompare.authority.revoke(), std::memory_order_release);
    liveCompare.selection.end (hypha::live_compare::RecoveryReason::pairChanged);
    requestServiceTimer(); // cleanup is deferred; RT authority already rejects the old ring
}

// Message thread: keep the estimate and the exact timing context used to make it. The
// diagnostics are not PRE authority, serialized state, or a measurement/Record modification.
hypha::live_compare::OffsetEstimate KirinHyphaProcessorBase::measureLiveCompareOffset()
{
    const juce::ScopedTryLock formatLock (handleLock);
    if (! formatLock.isLocked() || formatChangeHeld.load (std::memory_order_acquire)) return {};
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire)) return {};
    const auto* mapping = liveCompare.ring.control();
    if (mapping == nullptr || mapping->ring() == nullptr
        || mapping->ring()->header.ownerClosed.load (std::memory_order_acquire) != 0) return {};
    const auto stop = liveCompare.contentHold.generation();
    const auto timeline = liveCompare.timelineGeneration.load (std::memory_order_acquire);
    const auto context = liveCompare.alignContext();
    auto estimate = hypha::live_compare::measureOffset (*mapping->ring(), liveCompare.renderer);
    estimate.stopGeneration = stop;
    estimate.timelineGeneration = timeline;
    estimate.alignContext = context;
    if (stop != liveCompare.contentHold.generation()
        || timeline != liveCompare.timelineGeneration.load (std::memory_order_acquire)
        || context != liveCompare.alignContext()
        || mapping->ring()->header.ownerClosed.load (std::memory_order_acquire) != 0) estimate.determined = false;
    liveCompare.offsetFacts = estimate;
    return estimate;
}

bool KirinHyphaProcessorBase::holdLiveCompareForContentJump (const hypha::live_compare::OffsetEstimate& estimate) noexcept
{
    const juce::ScopedTryLock formatLock (handleLock);
    if (! formatLock.isLocked() || formatChangeHeld.load (std::memory_order_acquire)) return false;
    if (role != Role::Post || ! hypha::live_compare::offsetCanVote (estimate)
        || estimate.alignContext != liveCompare.alignContext()
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
    const juce::ScopedTryLock formatLock (handleLock);
    if (! formatLock.isLocked() || formatChangeHeld.load (std::memory_order_acquire)) return;
    if (role != Role::Post) return;
    // A baseline belongs to one pair and format. Toggling automatic alignment establishes the
    // current offset again, so an earlier, already aligned change cannot become a fresh hold.
    const auto context = liveCompare.alignContext();
    const auto preference = liveCompare.alignPreference.load (std::memory_order_acquire);
    if (context != liveCompare.alignServiced || preference != liveCompare.alignPreferenceSeen)
    {
        liveCompare.alignServiced = context;
        liveCompare.alignPreferenceSeen = preference;
        liveCompare.contentMonitor = {};
        liveCompare.contentStep = {};
        liveCompare.contentMonitorNext = 0;
    }
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
    const bool aligned = step.settled && alignLiveCompare (step, estimate);
    if (aligned) liveCompare.contentHold.clear (estimate.stopGeneration);
    if (step.jumped && ! aligned) holdLiveCompareForContentJump (estimate);
    if (step.recovered && estimate.timelineGeneration == liveCompare.timelineGeneration.load (std::memory_order_acquire))
        liveCompare.contentHold.clear (estimate.stopGeneration);
}

// INV-LC28: PRE early by a settled amount is read that much later. Silence and an undetermined
// window never get here, so they change nothing. In a Blind trial the renderer keeps the address
// until the listener switches away from PRE; timing interruptions never count as that switch.
bool KirinHyphaProcessorBase::alignLiveCompare (const hypha::live_compare::OffsetMonitor::Step& step,
                                                const hypha::live_compare::OffsetEstimate& estimate) noexcept
{
    const juce::ScopedTryLock formatLock (handleLock);
    if (! formatLock.isLocked() || formatChangeHeld.load (std::memory_order_acquire)) return false;
    if (! liveCompareAutoAlign() || ! step.settled || ! hypha::live_compare::offsetCanVote (estimate)
        || estimate.alignContext != liveCompare.alignContext()
        || estimate.timelineGeneration != liveCompare.timelineGeneration.load (std::memory_order_acquire)
        || estimate.stopGeneration != liveCompare.contentHold.generation()) return false;
    const auto value = hypha::live_compare::alignValue (step.lagFrames);
    // An estimate within its own agreement of the value in use moves nothing: no fade for that.
    const auto current = liveCompare.renderer.alignRequested (estimate.alignContext).frames;
    const auto settled = value.frames;
    const auto agreement = std::floor (step.sampleRate * hypha::live_compare::offsetAgreementSeconds);
    if (value.frames == 0 || current == 0 || static_cast<double> (std::llabs (settled - current)) > agreement)
        liveCompare.renderer.requestAlign (value, estimate.alignContext);
    return value.frames != 0 || std::llabs (step.lagFrames) <= 1;
}

bool KirinHyphaProcessorBase::liveCompareAutoAlign() const noexcept
{
    return liveCompare.autoAlign.load (std::memory_order_acquire);
}

void KirinHyphaProcessorBase::setLiveCompareAutoAlign (bool enabled) noexcept
{
    if (liveCompare.autoAlign.exchange (enabled, std::memory_order_acq_rel) != enabled)
    {
        liveCompare.alignPreference.fetch_add (1, std::memory_order_release);
        updateHostDisplay (ChangeDetails {}.withNonParameterStateChanged (true));
    }
}

std::int64_t KirinHyphaProcessorBase::liveCompareAlignFrames() const noexcept
{
    return liveCompare.renderer.alignFrames();
}

double KirinHyphaProcessorBase::liveCompareAlignMs() const noexcept
{
    const juce::ScopedTryLock formatLock (handleLock);
    if (! formatLock.isLocked() || formatChangeHeld.load (std::memory_order_acquire)) return 0.0;
    return preparedFormat.sampleRate > 0.0
        ? static_cast<double> (liveCompareAlignFrames()) * 1000.0 / preparedFormat.sampleRate : 0.0;
}

hypha::live_compare::OffsetMonitor::Step KirinHyphaProcessorBase::liveContentTimingObservation() const noexcept
{
    return liveCompare.contentStep; // message thread only, the same service-owned observation
}
