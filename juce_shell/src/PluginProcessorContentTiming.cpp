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
    const auto revision = liveCompare.renderer.alignRevision();
    auto estimate = hypha::live_compare::measureOffset (*mapping->ring(), liveCompare.renderer);
    estimate.stopGeneration = stop;
    estimate.timelineGeneration = timeline;
    estimate.alignContext = context;
    estimate.alignRevision = revision;
    if (stop != liveCompare.contentHold.generation()
        || timeline != liveCompare.timelineGeneration.load (std::memory_order_acquire)
        || context != liveCompare.alignContext()
        || revision != liveCompare.renderer.alignRevision()
        || (estimate.windowReady && estimate.clockK != liveCompare.renderer.alignClockK())
        || mapping->ring()->header.ownerClosed.load (std::memory_order_acquire) != 0) { estimate.determined = false; estimate.windowReady = false; }
    liveCompare.offsetFacts = estimate;
    return estimate;
}

bool KirinHyphaProcessorBase::holdLiveCompareForContentJump (const hypha::live_compare::OffsetEstimate& estimate) noexcept
{
    const juce::ScopedTryLock formatLock (handleLock);
    if (! formatLock.isLocked() || formatChangeHeld.load (std::memory_order_acquire)) return false;
    if (role != Role::Post || estimate.verifySamples || ! hypha::live_compare::offsetCanVote (estimate)
        || estimate.alignContext != liveCompare.alignContext()
        || estimate.alignRevision != liveCompare.renderer.alignRevision()
        || (estimate.windowReady && estimate.clockK != liveCompare.renderer.alignClockK())
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
        liveCompare.offsetFacts = {};
        liveCompare.contentMonitorNext = 0;
        liveCompare.contentWindow = {};
    }
    const auto revision = liveCompare.renderer.alignRevision();
    if (revision != liveCompare.contentAlignRevision)
    {
        liveCompare.candidateConfirmStart = 0;
        liveCompare.candidateUnconfirmed.store (false, std::memory_order_release);
        liveCompare.contentAlignRevision = revision;
        liveCompare.contentMonitor = {};
        liveCompare.contentStep = {};
        liveCompare.contentMonitorNext = 0;
        liveCompare.contentWindow = {};
    }
    const auto state = liveCompareStatus();
    const auto run = liveComparePlaybackRun();
    if (!state.active || state.compensationOff || run != liveCompare.contentMonitorRun)
    {
        liveCompare.candidateConfirmStart = 0;
        liveCompare.candidateUnconfirmed.store (false, std::memory_order_release);
        liveCompare.contentMonitor = {};
        liveCompare.contentStep = {};
        liveCompare.contentMonitorRun = run;
        liveCompare.contentMonitorNext = 0;
        liveCompare.contentWindow = {};
    }
    const auto now = hypha::live_compare::callbackWallNanos();
    if (candidateConfirmationExpired (state, now)) liveCompare.renderer.requestAlign ({}, context, revision);
    if (!state.active || state.compensationOff || state.verdict != hypha::live_compare::Verdict::accepted
        || now < liveCompare.contentMonitorNext) return;
   #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
    if (hypha::live_compare::beforeContentEstimateForTest != nullptr)
        hypha::live_compare::beforeContentEstimateForTest();
   #endif
    const auto estimate = measureLiveCompareOffset();
    // The Audio Thread may have advanced K after the checks above but before acquisition.
    // Scope the votes to the estimate actually copied, never the earlier service snapshot.
    if (estimate.windowReady && (estimate.alignRevision != liveCompare.contentAlignRevision
        || estimate.alignContext != liveCompare.alignServiced))
    {
        liveCompare.contentAlignRevision = estimate.alignRevision;
        liveCompare.alignServiced = estimate.alignContext;
        liveCompare.contentMonitor = {};
        liveCompare.contentStep = {};
        liveCompare.contentWindow = {};
    }
    const bool fast = liveCompareAutoAlign() && (liveCompare.renderer.alignPending() || ! liveCompare.contentStep.settled);
    liveCompare.contentMonitorNext = now + (fast ? 100'000'000 : 2'000'000'000);
    if (estimate.windowReady)
    {
        if (estimate.window == liveCompare.contentWindow) return; // a copied window never votes twice
        liveCompare.contentWindow = estimate.window;
    }
    // A validated but ambiguous window cannot confirm any read position, including zero.
    // Keep the choice and explain how to obtain content evidence if the wait persists.
    if (estimate.windowReady && ! hypha::live_compare::offsetCanVote (estimate)
        && estimate.alignRevision == liveCompare.renderer.alignRevision())
    {
        liveCompare.renderer.reportAlignAmbiguous (estimate.alignRevision);
        liveCompare.candidateUnconfirmed.store (candidateCompareActive() && ! liveCompare.contentStep.settled,
                                                std::memory_order_release);
    }
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
// until the listener switches away from PRE. A known changed mapping instead keeps POST
// sounding until fresh evidence permits the new address; the selection and gain survive.
bool KirinHyphaProcessorBase::alignLiveCompare (const hypha::live_compare::OffsetMonitor::Step& step,
                                                const hypha::live_compare::OffsetEstimate& estimate) noexcept
{
    const juce::ScopedTryLock formatLock (handleLock);
    if (! formatLock.isLocked() || formatChangeHeld.load (std::memory_order_acquire)) return false;
    if (! liveCompareAutoAlign() || ! step.settled || ! hypha::live_compare::offsetCanVote (estimate)
        || estimate.alignContext != liveCompare.alignContext()
        || estimate.alignRevision != liveCompare.renderer.alignRevision()
        || (estimate.windowReady && estimate.clockK != liveCompare.renderer.alignClockK())
        || estimate.timelineGeneration != liveCompare.timelineGeneration.load (std::memory_order_acquire)
        || estimate.stopGeneration != liveCompare.contentHold.generation()) return false;
    auto value = hypha::live_compare::alignValue (step.lagFrames);
    value.verifySamples = step.verifySamples;
    const bool candidate = candidateCompareActive();
    if (value.frames == 0 && (step.lagFrames < -1 || step.lagFrames > 1))
    {
        if (! candidate)
        {
            if (step.lagFrames > 1) liveCompare.renderer.reportAlignLate (estimate.alignRevision);
            return false;
        }
        value = {}; // A/B: nothing earlier exists to read, so A sounds as it is and the footer says how late
    }
    // An estimate within its own agreement of the value in use moves nothing: no fade for that.
    const auto requested = liveCompare.renderer.alignRequested (estimate.alignContext);
    const auto current = requested.frames;
    const auto settled = value.frames;
    const auto agreement = std::floor (step.sampleRate * hypha::live_compare::offsetAgreementSeconds);
    if (liveCompare.renderer.alignPending() || value.verifySamples != requested.verifySamples || value.frames == 0 || current == 0
        || static_cast<double> (std::llabs (settled - current)) > agreement)
        liveCompare.renderer.requestAlign (value, estimate.alignContext, estimate.alignRevision);
    liveCompare.candidateUnconfirmed.store (false, std::memory_order_release);
    return candidate || value.frames != 0 || std::llabs (step.lagFrames) <= 1;
}

// A/B is a comparison the listener asked for, so A never waits on a check that cannot finish:
// quiet or ambiguous audio confirms no position, and B's first four settling seconds offer no
// window at all. After a short stretch of proven playback A sounds at its clock position.
// The footer says so once a real window fails, and later evidence can still move A.
bool KirinHyphaProcessorBase::candidateConfirmationExpired (const hypha::live_compare::Status& state,
                                                            std::uint64_t now) noexcept
{
    constexpr std::uint64_t waitNanos = 1'000'000'000; // fresh windows vote well within it
    if (! state.active || ! candidateCompareActive() || ! liveCompareAutoAlign()
        || ! liveCompare.renderer.alignPending())
    {
        liveCompare.candidateConfirmStart = 0;
        return false;
    }
    if (state.verdict != hypha::live_compare::Verdict::accepted) return false;
    if (liveCompare.candidateConfirmStart == 0) liveCompare.candidateConfirmStart = now;
    return now - liveCompare.candidateConfirmStart >= waitNanos;
}

bool KirinHyphaProcessorBase::candidateAlignUnconfirmed() const noexcept
{
    return liveCompare.candidateUnconfirmed.load (std::memory_order_acquire);
}

bool KirinHyphaProcessorBase::liveCompareAlignPending() const noexcept
{
    return liveCompareAutoAlign() && (liveCompare.renderer.alignPending() || ! liveCompare.contentStep.settled);
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
    return liveCompare.renderer.alignFrames (liveCompare.alignContext());
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
