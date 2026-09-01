#include "../src/HyphaObservatoryContract.h"

#include <array>
#include <cassert>
#include <cstring>

namespace observatory = hypha::observatory;

namespace
{
constexpr bool overlaps (observatory::Rect first, observatory::Rect second) noexcept
{
    if (! observatory::hasArea (first) || ! observatory::hasArea (second))
        return false;
    return first.x < observatory::right (second)
        && second.x < observatory::right (first)
        && first.y < observatory::bottom (second)
        && second.y < observatory::bottom (first);
}

void verifyLayout (observatory::Role role,
                   observatory::SizePreset preset,
                   observatory::GuidePresence guide)
{
    const auto layout = observatory::shellLayout (role, preset, guide);
    assert (layout.outer.width == preset.width);
    assert (layout.outer.height == preset.height);

    for (const auto rect : std::array {
             layout.header, layout.roleTitle, layout.domainNavigation,
             layout.connectionStatus, layout.body, layout.footer,
             layout.session, layout.actions })
    {
        assert (observatory::hasArea (rect));
        assert (observatory::fitsWithin (rect, preset.width, preset.height));
    }

    assert (! overlaps (layout.roleTitle, layout.domainNavigation));
    assert (! overlaps (layout.domainNavigation, layout.connectionStatus));
    assert (! overlaps (layout.header, layout.body));
    assert (! overlaps (layout.body, layout.footer));
    assert (! overlaps (layout.session, layout.actions));

    if (role == observatory::Role::post)
    {
        assert (observatory::hasArea (layout.observationTarget));
        assert (! overlaps (layout.observationTarget, layout.session));
    }
    else
    {
        assert (! observatory::hasArea (layout.observationTarget));
    }

    if (guide == observatory::GuidePresence::present)
    {
        assert (observatory::hasArea (layout.guideRail));
        assert (observatory::fitsWithin (layout.guideRail, preset.width, preset.height));
        assert (! overlaps (layout.header, layout.guideRail));
        assert (! overlaps (layout.guideRail, layout.body));
    }
    else
    {
        assert (layout.guideRail.width == 0);
        assert (layout.guideRail.height == 0);
    }
}
}

int main()
{
    static_assert (observatory::sizePresets.size() == 4);
    static_assert (observatory::captureRenderScale == 2.0f);
    assert (std::strcmp (observatory::sizePresets[0].label, "100%") == 0);
    assert (std::strcmp (observatory::sizePresets[3].label, "200%") == 0);

    for (const auto preset : observatory::sizePresets)
    {
        for (const auto role : { observatory::Role::pre, observatory::Role::post })
        {
            verifyLayout (role, preset, observatory::GuidePresence::absent);
            verifyLayout (role, preset, observatory::GuidePresence::present);

            const auto withoutGuide = observatory::shellLayout (
                role, preset, observatory::GuidePresence::absent);
            const auto withGuide = observatory::shellLayout (
                role, preset, observatory::GuidePresence::present);
            assert (withoutGuide.header.x == withGuide.header.x);
            assert (withoutGuide.header.y == withGuide.header.y);
            assert (withoutGuide.header.width == withGuide.header.width);
            assert (withoutGuide.footer.y == withGuide.footer.y);
            assert (withGuide.body.y > withoutGuide.body.y);
            assert (withGuide.body.height < withoutGuide.body.height);
        }
    }

    const auto compact = observatory::visibleContent (
        observatory::Role::post,
        observatory::Density::compact,
        observatory::GuidePresence::absent);
    assert (! compact.domainTabs);
    assert (compact.domainCycle);
    assert (! compact.supportingMetrics);
    assert (! compact.primaryVisual);
    assert (! compact.capture);
    assert (! compact.guideRail);

    const auto focused = observatory::visibleContent (
        observatory::Role::post,
        observatory::Density::focused,
        observatory::GuidePresence::absent);
    assert (! focused.domainTabs);
    assert (focused.domainCycle);
    assert (! focused.supportingMetrics);
    assert (! focused.primaryVisual);
    assert (! focused.axes);

    const auto standard = observatory::visibleContent (
        observatory::Role::post,
        observatory::Density::standard,
        observatory::GuidePresence::present);
    assert (standard.primaryVisual);
    assert (standard.axes);
    assert (! standard.capture);
    assert (standard.guideRail);

    const auto full = observatory::visibleContent (
        observatory::Role::post,
        observatory::Density::observatory,
        observatory::GuidePresence::present);
    assert (full.domainTabs);
    assert (! full.domainCycle);
    assert (full.supportingMetrics);
    assert (full.primaryVisual);
    assert (full.axes);
    assert (full.observationTarget);
    assert (full.reset);
    assert (full.capture);
    assert (full.guideRail);

    const auto pre = observatory::visibleContent (
        observatory::Role::pre,
        observatory::Density::observatory,
        observatory::GuidePresence::present);
    assert (! pre.observationTarget);
    assert (! pre.capture);
    assert (pre.guideRail);

    static_assert (observatory::targetAllowed (
        observatory::Role::pre, observatory::ObservationTarget::absolute));
    static_assert (! observatory::targetAllowed (
        observatory::Role::pre, observatory::ObservationTarget::delta));
    static_assert (observatory::targetAllowed (
        observatory::Role::post, observatory::ObservationTarget::delta));
    static_assert (! observatory::domainCapabilities (
        observatory::Role::pre).allows (observatory::Domain::frequency));
    static_assert (observatory::domainFromState (
        observatory::Role::pre,
        observatory::stateValue (observatory::Domain::frequency))
        == observatory::Domain::level);
    static_assert (observatory::timeRangeFromState (99u)
        == observatory::TimeRange::seconds30);

    for (const auto domain : {
             observatory::Domain::level,
             observatory::Domain::time,
             observatory::Domain::frequency,
             observatory::Domain::space })
    {
        for (const auto target : {
                 observatory::ObservationTarget::absolute,
                 observatory::ObservationTarget::delta })
        {
            const observatory::NavigationState before {
                domain, target, observatory::GuidePresence::absent
            };
            const auto after = observatory::receiveGuide (before);
            assert (after.domain == before.domain);
            assert (after.target == before.target);
            assert (after.guide == observatory::GuidePresence::present);
        }
    }
}
