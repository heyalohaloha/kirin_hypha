#include "PluginProcessor.h"
#include <cmath>

namespace
{
// A MATCH that cannot measure tries again for 10 s from the press, as a waiting press does. Then it
// ends with its reason, and LISTEN goes on at the levels it had, without another press.
constexpr std::uint64_t namedMatchLimitNanos = 10'000'000'000ull;
}

// Message thread. An explicit MATCH stays with its processor when the editor closes. A plan
// that would lower POST still needs the existing explicit approval after the editor reopens.
void KirinHyphaProcessorBase::requestLiveCompareMatch()
{
    const auto state = liveCompareStatus();
    if (!state.active || state.finishing || liveCompare.blindStage != hypha::live_compare::BlindStage::idle) return;
    liveCompare.namedMatchPending = true;
    liveCompare.namedMatchReady = false;
    liveCompare.namedMatchNext = 0;
    liveCompare.namedMatchSince = hypha::live_compare::callbackWallNanos();
    liveCompare.namedMatchWaiting = hypha::live_compare::MatchFailure::none;
    liveCompare.namedMatchAuthority = liveCompare.authority.ticket();
    requestServiceTimer();
}

void KirinHyphaProcessorBase::serviceLiveCompareMatch()
{
    using namespace hypha::live_compare;
    if (!liveCompare.namedMatchPending && !liveCompare.namedMatchReady) return;
    const auto state = liveCompareStatus();
    if (!state.active || state.finishing || !liveCompare.authority.permitted()
        || liveCompare.namedMatchAuthority != liveCompare.authority.ticket()
        || liveCompare.blindStage != BlindStage::idle)
    { liveCompare.namedMatchPending = liveCompare.namedMatchReady = false; return; }
    if (!liveCompare.namedMatchPending) return;
    const auto now = callbackWallNanos();
    if (now < liveCompare.namedMatchNext) return;
    liveCompare.namedMatchNext = now + 500'000'000;
    const auto measured = measureLiveCompare(true);
    liveCompare.namedMatchWaiting = measured.failure;
    const auto since = liveCompare.namedMatchSince;
    const bool expired = since != 0 && now >= since && now - since >= namedMatchLimitNanos;
    if (!measured.ok())
    {
        if (measured.failure == MatchFailure::outOfRange || measured.failure == MatchFailure::invalidPlan
            || measured.failure == MatchFailure::wholeSpanLost || expired)
        {
            liveCompare.namedMatchPlan = {};
            liveCompare.namedMatchPlan.failure = measured.failure;
            liveCompare.namedMatchApplied = liveCompare.namedMatchPending = false;
            liveCompare.namedMatchReady = true;
        }
        return; // retries end at the deadline; no stale window becomes an approval
    }
    auto plan = planMatch (measured, state.postTarget > 0 ? 20.0 * std::log10 (state.postTarget) : 0.0);
    liveCompare.namedMatchApplied = false;
    if (plan.failure == MatchFailure::none && !plan.needsApproval)
    {
        const auto application = applyLiveCompareMatch (plan, MatchChoice::basis);
        if (application.failure == MatchFailure::stale && !expired) return;
        plan.failure = application.failure;
        liveCompare.namedMatchApplied = static_cast<bool> (application);
    }
    liveCompare.namedMatchPlan = plan;
    liveCompare.namedMatchPending = false;
    liveCompare.namedMatchReady = true;
}

bool KirinHyphaProcessorBase::takeLiveCompareMatch (hypha::live_compare::MatchPlan& plan, bool& applied)
{
    serviceLiveCompareMatch();
    if (!liveCompare.namedMatchReady) return false;
    const auto state = liveCompareStatus();
    if (!liveCompare.namedMatchApplied && liveCompare.namedMatchPlan.generationBound
        && liveCompare.namedMatchPlan.generation != state.sessionGeneration)
    {
        liveCompare.namedMatchReady = false;
        if (!liveCompare.namedMatchApplied) liveCompare.namedMatchPending = true;
        return false; // a renewed timeline never inherits a pending POST-lowering plan
    }
    plan = liveCompare.namedMatchPlan;
    applied = liveCompare.namedMatchApplied;
    liveCompare.namedMatchReady = false;
    return true;
}

void KirinHyphaProcessorBase::setWholeMatchPreferred(bool enabled) noexcept
{
    // Saved with the project: the host hears of a real change, as it does for AUTO ALIGN.
    if (!liveCompare.sessionActive.load(std::memory_order_acquire)
        && liveCompare.wholeMatchPreferred.exchange(enabled, std::memory_order_acq_rel) != enabled)
        updateHostDisplay(ChangeDetails {}.withNonParameterStateChanged(true));
}

void KirinHyphaProcessorBase::serviceLiveMatchSpan()
{
    if (role != Role::Post) return;
    auto& span = liveCompare.renderer.wholeMatch;
    if (!liveCompare.sessionActive.load(std::memory_order_acquire) || !liveCompare.authority.permitted())
    { if (!span.enabled()) span.clear(); return; }
    if (!candidateCompareActive()) span.service(&kirin_hypha_live_match_span_cut);
}

hypha::live_compare::MatchResult KirinHyphaProcessorBase::measureWholeLiveMatch(const hypha::live_compare::Ring& ring, std::uint32_t rate)
{
    using namespace hypha::live_compare;
    MatchResult result;
    auto& span = liveCompare.renderer.wholeMatch;
    const auto target = span.snapshot();
    const auto owner = gainIdentity(ring, liveCompare.authority.ticket());
    const auto view = liveCompare.renderer.historyView();
   #ifdef KIRIN_HYPHA_TEST_WALL_CLOCK
    if (duringLiveMatchReadLeaseForTest) duringLiveMatchReadLeaseForTest(true);
   #endif
    const bool drained = span.serviceThrough(target,
        std::numeric_limits<std::uint64_t>::max(), &kirin_hypha_live_match_span_cut);

    if (span.lost()) { result.failure = MatchFailure::wholeSpanLost; return result; }
    if (!drained || (view.compensationProof & 1u) != 0
        || liveCompare.compensationState.load(std::memory_order_acquire) != view.compensationProof
        || !wholeProofCurrent(ring, view) || !owner.same(gainIdentity(ring, liveCompare.authority.ticket())))
        return result;
    if (!span.enough()) { result.failure = MatchFailure::tooShort; return result; }
    KirinLiveMatchSpanFacts facts {};
    if (!span.measure(facts))
    { result.failure = span.lost() ? MatchFailure::wholeSpanLost : MatchFailure::notEnoughSignal; return result; }
    const auto current = liveCompare.renderer.historyView();
    if (!wholeProofCurrent(ring, current) || current.proof != view.proof
        || current.preRun != view.preRun || current.preClock != view.preClock
        || current.compensationProof != view.compensationProof
        || liveCompare.compensationState.load(std::memory_order_acquire) != view.compensationProof
        || !owner.same(gainIdentity(ring, liveCompare.authority.ticket())))
    { result.failure = MatchFailure::stale; return result; }
    result.measuredDb = facts.post_lufs - facts.pre_lufs;
    result.prePeakDbtp = facts.pre_peak; result.postPeakDbtp = facts.post_peak;
    result.ceilingDbtp = std::max({-1.0, facts.pre_peak, facts.post_peak});
    result.analysisUnits = span.frames();
    result.seconds = static_cast<double>(span.frames()) / rate;
    result.wholeOwner = owner; result.wholeOwnerBound = true; result.wholePreClock = view.preClock;
    result.compensationProof = view.compensationProof; result.compensationProofBound = true;
    result.proof = view.proof; result.preRun = view.preRun; result.proofBound = true;
    result.failure = std::abs(result.measuredDb) <= maximumMatchDb ? MatchFailure::none : MatchFailure::outOfRange;
    return result;
}
