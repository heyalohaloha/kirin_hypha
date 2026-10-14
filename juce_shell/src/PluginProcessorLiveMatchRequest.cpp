#include "PluginProcessor.h"
#include <cmath>

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
    const auto measured = measureLiveCompare();
    liveCompare.namedMatchWaiting = measured.failure;
    if (!measured.ok())
    {
        if (measured.failure == MatchFailure::outOfRange || measured.failure == MatchFailure::invalidPlan)
        {
            liveCompare.namedMatchPlan = {};
            liveCompare.namedMatchPlan.failure = measured.failure;
            liveCompare.namedMatchApplied = liveCompare.namedMatchPending = false;
            liveCompare.namedMatchReady = true;
        }
        return; // bounded retries, no stale window becomes an approval
    }
    auto plan = planMatch (measured, state.postTarget > 0 ? 20.0 * std::log10 (state.postTarget) : 0.0);
    liveCompare.namedMatchApplied = false;
    if (plan.failure == MatchFailure::none && !plan.needsApproval)
    {
        const auto application = applyLiveCompareMatch (plan, MatchChoice::basis);
        if (application.failure == MatchFailure::stale) return;
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
