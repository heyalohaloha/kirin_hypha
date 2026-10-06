#pragma once

#include "live_compare/LiveCompareRecovery.h"
#include <cstdint>

namespace hypha::live_compare_ui
{
enum class ActionFaultScope { duringAction, includingOpeningRefresh };
enum class ActionOutcome { success, refusal, stale };
enum class ActionCompletion { published, newFault, stale, ineligible };
enum class AutoReadiness { ready, stale, timing, match, limited, blocked };

// Message-thread action boundary, with no retained state. An opening refresh acknowledges old
// history before the action; only faults after that point supersede a successful result. AUTO
// additionally must not re-enable itself after an opening refresh just stopped it.
template <typename Action, typename Synchronize, typename Current, typename Publish>
ActionCompletion performAction (ActionFaultScope scope, Action action, Synchronize synchronize,
                                Current current, Publish publish)
{
    const bool openingFault = synchronize();
    const auto outcome = action();
    const bool closingFault = synchronize();
    if (outcome == ActionOutcome::stale) return ActionCompletion::stale;
    if (outcome == ActionOutcome::success)
    {
        if (closingFault || (scope == ActionFaultScope::includingOpeningRefresh && openingFault))
            return ActionCompletion::newFault; // never replace a newly published fault with readiness text
        if (! current()) return ActionCompletion::ineligible;
    }
    publish(); // a direct refusal remains the latest result, not old retained history
    return ActionCompletion::published;
}

template <typename Status>
bool currentNamedAction (const Status& state, std::uint64_t generation, bool matched) noexcept
{
    return state.active && ! state.finishing && state.sessionGeneration == generation
        && (! matched || state.matched); // retained reason alone never vetoes a legal reMATCH
}

template <typename Status>
AutoReadiness autoReadiness (const Status& state, std::uint64_t generation) noexcept
{
    if (! currentNamedAction (state, generation, false)) return AutoReadiness::stale;
    if (state.interrupted || state.contentHeld || state.compensationOff) return AutoReadiness::blocked;
    if (state.matchLimited) return AutoReadiness::limited;
    if (state.verdict != live_compare::Verdict::accepted
        || state.observation != live_compare::RecoveryReason::none) return AutoReadiness::timing;
    if (! state.matched || ! state.matchReady) return AutoReadiness::match;
    return AutoReadiness::ready;
}
template <typename Status>
bool currentAutoAction (const Status& state, std::uint64_t generation) noexcept
{ return autoReadiness (state, generation) == AutoReadiness::ready; }

inline const char* autoReadinessNotice (AutoReadiness readiness) noexcept
{
    switch (readiness)
    {
        case AutoReadiness::timing: return "AUTO not started: PRE timing pending";
        case AutoReadiness::match: return "AUTO not started: MATCH pending";
        case AutoReadiness::limited: return "AUTO needs a MATCH without TP LIMIT";
        case AutoReadiness::ready: case AutoReadiness::stale: case AutoReadiness::blocked: return "";
    }
    return "";
}
}
