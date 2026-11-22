#include "PluginProcessor.h"

namespace
{
// INV-LC28 / INV-LC10: fresh windows vote well within it (100 ms apart, two needed).
constexpr std::uint64_t confirmationWaitNanos = 1'000'000'000; // a notice without renewed evidence
constexpr std::uint64_t keptReadNanos = 500'000'000; // INV-LC28: a kept correction's read retries
}

void KirinHyphaProcessorBase::invalidateLiveComparePair() noexcept
{
    if (role != Role::Post) return;
    liveCompare.pairRevocationGeneration.store (liveCompare.authority.revoke(), std::memory_order_release);
    liveCompare.renderer.wholeMatch.suspend();
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
    auto estimate = hypha::live_compare::measureOffset (*mapping->ring(), liveCompare.renderer, liveCompare.offsetCenter);
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

// Message thread, INV-LC7: a wide, coarse search proposes where the fine search looks; it is no
// evidence itself. The same lock, session and ring checks as the fine search.
hypha::live_compare::WideOffset KirinHyphaProcessorBase::searchLiveCompareWideOffset (bool recentFirst)
{
    const juce::ScopedTryLock formatLock (handleLock);
    if (! formatLock.isLocked() || formatChangeHeld.load (std::memory_order_acquire)) return {};
    if (role != Role::Post || ! liveCompare.sessionActive.load (std::memory_order_acquire)) return {};
    const auto* mapping = liveCompare.ring.control();
    if (mapping == nullptr || mapping->ring() == nullptr
        || mapping->ring()->header.ownerClosed.load (std::memory_order_acquire) != 0) return {};
    // A settled position that fresh windows no longer confirm: a delay added while playing shows in
    // the latest seconds first, so those are searched before the whole history.
    if (recentFirst)
        for (const double seconds : { 2.0, 4.0 })
            if (const auto recent = hypha::live_compare::searchWideOffset (*mapping->ring(), liveCompare.renderer,
                    static_cast<std::int64_t> (preparedFormat.sampleRate * seconds)); recent.found) return recent;
    return hypha::live_compare::searchWideOffset (*mapping->ring(), liveCompare.renderer);
}

// INV-LC10: a status-line notice only. PRE keeps sounding at K; the clock rules alone choose POST.
bool KirinHyphaProcessorBase::noticeLiveCompareContentTiming (const hypha::live_compare::OffsetEstimate& estimate) noexcept
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
    liveCompare.holdEvidenceAt = hypha::live_compare::callbackWallNanos(); // before the status can see it
    if (liveCompare.blindStage != hypha::live_compare::BlindStage::idle && estimate.sampleRate > 0.0)
        liveCompare.blindContentNoticeMs = static_cast<double> (estimate.lagFrames) * 1000.0 / estimate.sampleRate;
    liveCompare.contentHold.hold (estimate.stopGeneration);
    return liveCompare.contentHold.held();
}

// Host notification, possibly off the message/audio thread. AU Reset is also used for seek:
// only an explicit, successfully read stopped transport ends a notice. Never infer stop from a
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

// The processor owns the content notice across window closure and live Blind. A changed
// spectrum alone never votes. Two strong estimates returning to the original baseline clear
// the notice; timing/PCM validation alone gates every PRE block.
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
        liveCompare.contentStep = {}; liveCompare.contentDoubt = false;
        liveCompare.offsetFacts = {};
        liveCompare.contentMonitorNext = 0;
        liveCompare.contentWindow = {};
        liveCompare.offsetCenter = 0;
        liveCompare.wideSearchNext = 0;
        preAlignEvent (hypha::live_compare::PreAlignEvent::contextChanged);
    }
    const auto revision = liveCompare.renderer.alignRevision();
    if (revision != liveCompare.contentAlignRevision)
    {
        liveCompare.candidateUnconfirmed.store (false, std::memory_order_release);
        preAlignEvent (hypha::live_compare::PreAlignEvent::clockChanged);
        liveCompare.contentAlignRevision = revision;
        liveCompare.offsetCenter = 0;
        liveCompare.wideSearchNext = 0;
        liveCompare.contentMonitor = {};
        liveCompare.contentStep = {}; liveCompare.contentDoubt = false;
        liveCompare.contentMonitorNext = 0;
        liveCompare.contentWindow = {};
    }
    const auto state = liveCompareStatus();
    const auto run = liveComparePlaybackRun();
    if (!state.active || state.compensationOff || run != liveCompare.contentMonitorRun)
    {
        liveCompare.candidateUnconfirmed.store (false, std::memory_order_release);
        auto ended = preAlignInput (hypha::live_compare::PreAlignEvent::transport);
        ended.active = state.active;
        liveCompare.preAlign.transition (ended);
        liveCompare.contentMonitor = {};
        liveCompare.contentStep = {}; liveCompare.contentDoubt = false;
        liveCompare.contentMonitorRun = run;
        liveCompare.contentMonitorNext = 0;
        liveCompare.contentWindow = {};
    }
    const auto now = hypha::live_compare::callbackWallNanos();
    // The one budget (RendererAlignment) ended without a confirmed position: PRE plays at K, and
    // LISTEN and BLIND say that the delay was not measured instead of staying silent (A/B already says
    // its A is unconfirmed). A correction confirmed before for this pair, format and PRE owner plays
    // instead, told as not re-measured (INV-LC28). A snapshot that cannot be read now is read again on
    // the next service, for at most keptReadNanos; one that PRE's new run cannot serve yet stays kept
    // and requested: PRE plays at K, told as unmeasured, until the run reaches it, then with it.
    // LISTEN / BLIND and A/B never use each other's; A/B uses one only for the same candidate under the
    // same host conditions. K moved by the kept amount: the host compensates that delay now, so it is
    // dropped (PRE at K). All of it is the owner's one transition (LiveComparePreAlign.h).
    auto tick = preAlignInput (hypha::live_compare::PreAlignEvent::tick);
    tick.revision = revision;
    tick.pending = state.active && liveCompareAutoAlign() && liveCompare.renderer.alignPending();
    if (tick.pending) liveCompare.renderer.alignState().arm(); // the wait for content counts from here, once for a budget
    tick.spent = liveCompare.renderer.alignState().budgetSpent();
    tick.releasedAt = liveCompare.renderer.alignState().releasedAt();
    tick.reach = liveCompare.renderer.alignReach();
   #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
    if (hypha::live_compare::keptViewMissForTest != nullptr && hypha::live_compare::keptViewMissForTest())
        tick.reach = std::numeric_limits<std::int64_t>::max();
   #endif
    tick.now = now; tick.readRetryNanos = keptReadNanos;
   #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
    if (hypha::live_compare::beforeAlignFallbackForTest) hypha::live_compare::beforeAlignFallbackForTest();
   #endif
    if (const auto fallback = liveCompare.preAlign.transition (tick); fallback.request)
    {
        const bool changed = tick.blind && fallback.value != liveCompare.renderer.alignRequested (context);
        if (changed) { liveCompare.blindHeard.restart(); liveCompare.blindHearing.beginChange(); }
        liveCompare.renderer.requestAlign (fallback.value, context, fallback.revision, fallback.resend);
        if (changed) liveCompare.blindHearing.endChange();
    }
    // BLIND: a correction settled during the trial plays once each source has been heard for half a
    // second of playback (PRE's read position moves while the listener is away from PRE).
    if (! liveCompare.preAlign.awaitsListener()) liveCompare.blindHeard.restart();
    else if (const auto receipt = liveCompare.blindHearing.read(); receipt.valid)
    {
        const auto command = liveCompare.blind.command();
        if (liveCompare.blind.valid (command) && command.word == receipt.command
            && liveCompare.blindHeard.observe (receipt.first, receipt.second,
                                               static_cast<std::uint64_t> (preparedFormat.sampleRate * 0.5)))
            preAlignEvent (hypha::live_compare::PreAlignEvent::heardBoth);
    }
    // INV-AB6: after an A/B return that ended at its deadline, B may still carry comparison audio.
    // Nothing is learnt from it; PRE plays at K or with a correction kept before, told as above.
    if (!state.active || state.compensationOff || ! liveContentPathProven (state)
        || now < liveCompare.contentMonitorNext || liveCompare.candidate.tail.load (std::memory_order_acquire) != 0) return;
   #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
    if (hypha::live_compare::beforeContentEstimateForTest != nullptr)
        hypha::live_compare::beforeContentEstimateForTest();
   #endif
    auto estimate = measureLiveCompareOffset();
    // A delay beyond the fine search (a linear-phase EQ, an uncompensated host) finds no peak there.
    // At most once a second, a wide search may move the fine search to a new center; only the fine
    // search's own strong peaks count, at the new center as anywhere.
    if (estimate.windowReady && ! hypha::live_compare::offsetCanVote (estimate) && now >= liveCompare.wideSearchNext)
    {
        liveCompare.wideSearchNext = now + 1'000'000'000;
        if (const auto wide = searchLiveCompareWideOffset (liveCompare.contentDoubt); wide.found && wide.lagFrames != liveCompare.offsetCenter)
        {
            liveCompare.offsetCenter = wide.lagFrames;
            estimate = measureLiveCompareOffset();
        }
    }
    // The Audio Thread may have advanced K after the checks above but before acquisition.
    // Scope the votes to the estimate actually copied, never the earlier service snapshot.
    if (estimate.windowReady && (estimate.alignRevision != liveCompare.contentAlignRevision
        || estimate.alignContext != liveCompare.alignServiced))
    {
        liveCompare.contentAlignRevision = estimate.alignRevision;
        liveCompare.alignServiced = estimate.alignContext;
        liveCompare.contentMonitor = {};
        liveCompare.contentStep = {}; liveCompare.contentDoubt = false;
        liveCompare.contentWindow = {};
    }
    const bool fast = state.contentHeld
        || (liveCompareAutoAlign() && (liveCompare.renderer.alignPending() || ! liveCompare.contentStep.settled || liveCompare.contentDoubt));
    liveCompare.contentMonitorNext = now + (fast ? 100'000'000 : 2'000'000'000);
    if (estimate.windowReady)
    {
        if (estimate.window == liveCompare.contentWindow) return; // a copied window never votes twice
        liveCompare.contentWindow = estimate.window;
    }
    // A validated but ambiguous window cannot confirm any read position, including zero. The
    // choice is kept; the bounded wait above lets PRE sound at its clock position instead. A
    // narrow (bass-heavy) window is only undetermined: it neither flags doubt nor breaks votes.
    // A/B still says that A sounds unconfirmed, as before.
    if (estimate.windowReady && ! hypha::live_compare::offsetCanVote (estimate)
        && estimate.alignRevision == liveCompare.renderer.alignRevision())
    {
        if (! estimate.narrow) liveCompare.renderer.reportAlignAmbiguous (estimate.alignRevision);
        liveCompare.candidateUnconfirmed.store (candidateCompareActive() && ! liveCompare.contentStep.settled,
                                                std::memory_order_release);
    }
    const auto step = liveCompare.contentMonitor.observe (estimate, state.contentHeld);
    // INV-LC7: a settled position that this fresh window did not confirm (a delay added between PRE
    // and POST while playing moves no clock) is checked at the preparing pace again until two windows
    // agree. PRE keeps playing where it is. A strong window elsewhere is told as unmeasured meanwhile.
    if (estimate.windowReady && liveCompare.contentStep.settled && liveCompareAutoAlign())
    {
        if (! step.settled && ! liveCompare.contentDoubt) liveCompare.contentMonitorNext = now + 100'000'000;
        liveCompare.contentDoubt = ! step.settled;
        const auto apart = std::llabs (estimate.lagFrames - liveCompare.contentStep.lagFrames);
        if (! step.settled && hypha::live_compare::offsetCanVote (estimate) && ! estimate.verifySamples
            && estimate.alignRevision == liveCompare.renderer.alignRevision()
            && static_cast<double> (apart) > std::floor (estimate.sampleRate * hypha::live_compare::offsetAgreementSeconds))
        {
            preAlignEvent (hypha::live_compare::PreAlignEvent::doubted);
            liveCompare.candidateUnconfirmed.store (candidateCompareActive(), std::memory_order_release);
        }
    }
    if (step.settled) liveCompare.contentStep = step;
    if (step.settled) preAlignEvent (hypha::live_compare::PreAlignEvent::measured);
    const bool aligned = step.settled && alignLiveCompare (step, estimate);
    // A strong estimate that PRE is late, early past the ring, or jumped from the run's baseline is
    // told, never sounded as POST: PRE keeps its clock position K (plus a correction confirmed
    // before) in LISTEN, BLIND and A/B. With alignment on, a settled window that cannot be aligned
    // is such an estimate; with it off, only a jump is news, the offset itself is shown as a fact.
    const bool notice = ! aligned && (step.jumped || (step.settled && liveCompareAutoAlign()));
    if (aligned) liveCompare.contentHold.clear (estimate.stopGeneration);
    if (step.recovered && ! notice
        && estimate.timelineGeneration == liveCompare.timelineGeneration.load (std::memory_order_acquire))
        liveCompare.contentHold.clear (estimate.stopGeneration);
    if (notice) noticeLiveCompareContentTiming (estimate); // renewed without a gap the 3 s timer would see
    // The notice stands only on confident evidence: two agreeing strong windows that still show
    // it renew it. Weak or ambiguous windows (a limiter, outboard, quiet audio) cannot keep it:
    // after one second of proven playback without a renewal the notice ends.
    if (state.contentHeld && step.settled && ! step.recovered && ! aligned) liveCompare.holdEvidenceAt = now;
    if (state.contentHeld && liveCompare.contentHold.held() && now >= liveCompare.holdEvidenceAt
        && now - liveCompare.holdEvidenceAt >= confirmationWaitNanos)
        liveCompare.contentHold.clear (liveCompare.contentHold.generation());
}

// INV-LC28: PRE early by a settled amount is read that much later. Silence and an undetermined
// window never get here, so they change nothing. In a Blind trial the renderer keeps the address
// until the listener switches away from PRE. A known changed mapping instead keeps POST
// sounding, for at most one second, until fresh evidence permits the new address.
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
    // Below the jump threshold a late PRE is no latency proof (INV-LC10): it sounds as it is.
    const bool slightlyLate = step.lagFrames > 1
        && step.lagFrames < std::ceil (step.sampleRate * hypha::live_compare::offsetJumpSeconds);
    // Nothing earlier exists to read for a late PRE, nor past the ring for a far early one: PRE
    // sounds at its clock position K in LISTEN, BLIND and A/B, and the caller tells the listener.
    const bool unreadable = value.frames == 0 && (step.lagFrames < -1 || step.lagFrames > 1);
    if (unreadable) value = {};
    // An estimate within its own agreement of the value in use moves nothing: no fade for that.
    const auto requested = liveCompare.renderer.alignRequested (estimate.alignContext);
    const auto current = requested.frames;
    const auto settled = value.frames;
    const auto agreement = std::floor (step.sampleRate * hypha::live_compare::offsetAgreementSeconds);
    if (liveCompare.renderer.alignPending() || value.verifySamples != requested.verifySamples || value.frames == 0 || current == 0
        || static_cast<double> (std::llabs (settled - current)) > agreement)
    {
        // Confirmed: published for the revision it was measured at, and kept (in memory only) with
        // the K it was measured against, never whatever K has become since the checks above.
        auto confirmed = preAlignInput (hypha::live_compare::PreAlignEvent::estimate);
        confirmed.requested = requested;
        confirmed.pending = liveCompare.renderer.alignPending();
        confirmed.spent = liveCompare.renderer.alignState().budgetSpent();
        confirmed.context = estimate.alignContext;
        confirmed.value = value;
        confirmed.measuredRevision = estimate.alignRevision;
        confirmed.measuredK = estimate.windowReady ? estimate.clockK : confirmed.k;
        if (const auto publish = liveCompare.preAlign.transition (confirmed); publish.request)
        {
            const bool changed = confirmed.blind && publish.value != requested;
            if (changed) { liveCompare.blindHeard.restart(); liveCompare.blindHearing.beginChange(); }
            liveCompare.renderer.requestAlign (publish.value, estimate.alignContext, publish.revision);
            if (changed) liveCompare.blindHearing.endChange();
        }
    }
    liveCompare.candidateUnconfirmed.store (false, std::memory_order_release);
    return ! unreadable || slightlyLate;
}

// LISTEN, BLIND and A/B are comparisons the listener asked for, so PRE (A) never waits on a
// content check that cannot finish: quiet or ambiguous audio, a limiter or outboard between PRE
// and POST confirm no position, and B's first four settling seconds offer no window at all.
// After one second of the Audio Thread's one budget (clock-proven playback that waits for content,
// together with any hold for PRE's run) PRE sounds at its clock position K, as with alignment off;
// every block still needs the clock and PCM proof. Later strong evidence that PRE is early still
// moves the read position through the usual fades. A confident late PRE (two strong windows,
// 0.25 ms or more) is never a wait: it sounds at K and the status line says so (INV-LC10).
// What every event tells the owner of PRE's alignment state: the present context, host conditions
// and the Audio Thread's clock mapping.
hypha::live_compare::PreAlignInput KirinHyphaProcessorBase::preAlignInput (hypha::live_compare::PreAlignEvent event) const noexcept
{
    hypha::live_compare::PreAlignInput in;
    in.event = event;
    in.context = liveCompare.alignContext();
    in.host = { candidateCompareActive(), liveCompare.alignPeer.load (std::memory_order_acquire),
                liveCompare.alignConditions.load (std::memory_order_acquire),
                liveCompare.compensationRevision.load (std::memory_order_acquire) };
    in.revision = liveCompare.renderer.alignRevision();
    in.k = liveCompare.renderer.alignClockK(); in.clock = &liveCompare.renderer.alignState().readiness;
    in.active = true;
    in.blind = liveCompare.blind.valid (liveCompare.blind.command());
    in.agreement = static_cast<std::int64_t> (std::floor (preparedFormat.sampleRate * hypha::live_compare::offsetAgreementSeconds));
    return in;
}

void KirinHyphaProcessorBase::preAlignEvent (hypha::live_compare::PreAlignEvent event) noexcept
{ liveCompare.preAlign.transition (preAlignInput (event)); }

// BLIND never tells which source is PRE: its content checks and their timers follow PRE's path
// (proven and not held), which is the same whichever source is selected; the selected source's own
// verdict differs once a correction is applied. LISTEN and A/B follow the playback verdict.
bool KirinHyphaProcessorBase::liveContentPathProven (const hypha::live_compare::Status& state) const noexcept
{
    return liveCompare.blindStage != hypha::live_compare::BlindStage::idle
        ? liveCompare.renderer.alignState().pathProven() : state.verdict == hypha::live_compare::Verdict::accepted;
}

bool KirinHyphaProcessorBase::candidateAlignUnconfirmed() const noexcept
{
    return liveCompare.candidateUnconfirmed.load (std::memory_order_acquire);
}

// PRE waits for content only while the renderer holds it back. A/B keeps saying it checks A
// until a window settles; LISTEN and BLIND, already sounding at K, claim no check.
bool KirinHyphaProcessorBase::liveCompareAlignPending() const noexcept
{
    return liveCompareAutoAlign() && (liveCompare.renderer.alignPending()
        || (candidateCompareActive() && ! liveCompare.contentStep.settled));
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
