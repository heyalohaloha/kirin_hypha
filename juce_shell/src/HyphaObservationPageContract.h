#pragma once
#include "HyphaObservatoryContract.h"
#include "HyphaAnalysisNavigation.h"

namespace hypha::observatory
{
struct PageCapabilities
{
    ObservationTarget target;
    bool targetSelectable;
    bool historyRange;
    bool loudnessScale;
    const char* help;
};

// One source for controls, displayed facts and capture metadata. Remember the user's target
// preference across fixed-target pages; visiting RUN/LIVE must never rewrite it.
constexpr PageCapabilities pageCapabilities (Role role, Domain domain,
    analysis_navigation::Page page, ObservationTarget preferred, bool attackPaired = false)
{
    using Page = analysis_navigation::Page;
    if (role == Role::pre)
        return { ObservationTarget::absolute, false, domain == Domain::time,
                 domain == Domain::time, "PRE input measurements" };
    if (domain == Domain::space || domain == Domain::reference)
        return { ObservationTarget::absolute, false, false, false,
                 "Local measurements; PRE subtraction does not apply" };
    if (domain == Domain::time && page == Page::perceptual)
        return { ObservationTarget::delta, false, false, false,
                 "SHARP: exact POST - PRE sharpness. A paired PRE is required" };
    if (domain == Domain::time && page == Page::absolute)
        return { ObservationTarget::absolute, false, false, false,
                 "LIVE: local POST absolute values, fixed scales, 100 ms observations" };
    if (domain == Domain::time && page == Page::run)
        return { ObservationTarget::absolute, false, true, false,
                 "RUN: absolute facts grouped by playback run within the selected history" };
    if (domain == Domain::time && page == Page::attack)
        return { attackPaired ? ObservationTarget::delta : ObservationTarget::absolute,
                 false, false, false, "ATTACK: automatic paired comparison or POST-only events" };
    return { preferred, true, domain == Domain::time, domain == Domain::time,
             "POST selects absolute values; delta selects POST minus PRE" };
}
static_assert (! pageCapabilities (Role::post, Domain::time,
    analysis_navigation::Page::attack, ObservationTarget::delta).loudnessScale);
static_assert (pageCapabilities (Role::post, Domain::time,
    analysis_navigation::Page::absolute, ObservationTarget::delta).target == ObservationTarget::absolute);
}
