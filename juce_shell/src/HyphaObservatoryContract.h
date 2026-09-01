#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

// Pure C++ contract for the Hypha Observatory parent shell.
//
// This layer owns responsive anatomy and navigation facts only. It deliberately has no JUCE,
// measurement, pairing, filesystem, or transport dependency. PRE and POST therefore consume the
// same geometry without making Guide delivery or optional Analysis part of the audio boundary.
namespace hypha::observatory
{
enum class Role
{
    pre,
    post,
};

enum class Domain
{
    level,
    time,
    frequency,
    space,
};

enum class ObservationTarget
{
    absolute,
    delta,
};

enum class TimeRange
{
    seconds30,
    minutes2,
    minutes10,
    hours2,
    hours24,
};

enum class GuidePresence
{
    absent,
    present,
};

enum class ConnectionState
{
    source,
    unpaired,
    waiting,
    paired,
};

enum class Density
{
    compact,
    focused,
    standard,
    observatory,
};

struct SizePreset
{
    int width = 0;
    int height = 0;
    Density density = Density::compact;
    const char* label = "";
};

constexpr std::array<SizePreset, 5> sizePresets {{
    { 300, 200, Density::compact, "100%" },
    { 375, 250, Density::focused, "125%" },
    { 450, 300, Density::standard, "150%" },
    { 600, 400, Density::observatory, "200%" },
    { 900, 600, Density::observatory, "300%" },
}};

constexpr float captureRenderScale = 2.0f;

struct Rect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

constexpr int right (Rect rect) noexcept
{
    return rect.x + rect.width;
}

constexpr int bottom (Rect rect) noexcept
{
    return rect.y + rect.height;
}

constexpr bool hasArea (Rect rect) noexcept
{
    return rect.width > 0 && rect.height > 0;
}

constexpr bool fitsWithin (Rect rect, int width, int height) noexcept
{
    return rect.x >= 0 && rect.y >= 0
        && right (rect) <= width && bottom (rect) <= height;
}

constexpr int shellMargin (Density density) noexcept
{
    switch (density)
    {
        case Density::compact:     return 4;
        case Density::focused:     return 6;
        case Density::standard:    return 8;
        case Density::observatory: return 10;
    }
    return 4;
}

constexpr int headerHeight (Density density) noexcept
{
    switch (density)
    {
        case Density::compact:     return 30;
        case Density::focused:     return 36;
        case Density::standard:    return 42;
        case Density::observatory: return 50;
    }
    return 30;
}

constexpr int footerHeight (Density density) noexcept
{
    switch (density)
    {
        case Density::compact:     return 24;
        case Density::focused:     return 28;
        case Density::standard:    return 32;
        case Density::observatory: return 40;
    }
    return 24;
}

constexpr int guideRailHeight (Density density, GuidePresence presence) noexcept
{
    if (presence == GuidePresence::absent)
        return 0;

    switch (density)
    {
        case Density::compact:     return 18;
        case Density::focused:     return 20;
        case Density::standard:    return 22;
        case Density::observatory: return 24;
    }
    return 18;
}

struct ShellLayout
{
    Rect outer;
    Rect header;
    Rect roleTitle;
    Rect domainNavigation;
    Rect connectionStatus;
    Rect guideRail;
    Rect body;
    Rect footer;
    Rect observationTarget;
    Rect session;
    Rect actions;
};

constexpr ShellLayout shellLayout (Role role,
                                   SizePreset preset,
                                   GuidePresence guide) noexcept
{
    const int margin = shellMargin (preset.density);
    const int gap = preset.density == Density::compact ? 2 : 4;
    const int headerH = headerHeight (preset.density);
    const int footerH = footerHeight (preset.density);
    const int railH = guideRailHeight (preset.density, guide);
    const Rect header { margin, margin, preset.width - 2 * margin, headerH };
    const int titleWidth = preset.density == Density::compact ? 74
                         : preset.density == Density::focused ? 92
                         : preset.density == Density::standard ? 108 : 128;
    const int statusWidth = preset.density == Density::compact ? 64 : 104;
    const Rect roleTitle { header.x, header.y, titleWidth, header.height };
    const Rect connectionStatus {
        right (header) - statusWidth, header.y, statusWidth, header.height
    };
    const Rect domainNavigation {
        right (roleTitle) + gap,
        header.y,
        connectionStatus.x - gap - (right (roleTitle) + gap),
        header.height
    };

    const int contextTop = bottom (header) + gap;
    const Rect guideRail = guide == GuidePresence::present
        ? Rect { margin, contextTop, preset.width - 2 * margin, railH }
        : Rect { margin, contextTop, 0, 0 };
    const int bodyTop = guide == GuidePresence::present
        ? bottom (guideRail) + gap : contextTop;
    const Rect footer {
        margin,
        preset.height - margin - footerH,
        preset.width - 2 * margin,
        footerH
    };
    const Rect body {
        margin,
        bodyTop,
        preset.width - 2 * margin,
        footer.y - gap - bodyTop
    };

    const int targetWidth = role == Role::post
        ? (preset.density == Density::compact ? 58
           : preset.density == Density::observatory ? 150 : 76) : 0;
    const int actionWidth = preset.density == Density::compact ? 54
                          : preset.density == Density::focused ? 92
                          : preset.density == Density::standard ? 120 : 180;
    const Rect observationTarget {
        footer.x, footer.y, targetWidth, footer.height
    };
    const Rect actions {
        right (footer) - actionWidth, footer.y, actionWidth, footer.height
    };
    const int sessionX = targetWidth > 0 ? right (observationTarget) + gap : footer.x;
    const Rect session {
        sessionX,
        footer.y,
        actions.x - gap - sessionX,
        footer.height
    };

    return {
        { 0, 0, preset.width, preset.height },
        header,
        roleTitle,
        domainNavigation,
        connectionStatus,
        guideRail,
        body,
        footer,
        observationTarget,
        session,
        actions,
    };
}

struct VisibleContent
{
    bool domainTabs = false;
    bool domainCycle = false;
    bool supportingMetrics = false;
    bool primaryVisual = false;
    bool axes = false;
    bool observationTarget = false;
    bool reset = true;
    bool capture = false;
    bool guideRail = false;
};

constexpr VisibleContent visibleContent (Role role,
                                         Density density,
                                         GuidePresence guide) noexcept
{
    const bool compactMeter = density == Density::compact || density == Density::focused;
    const bool singleDomainControl = compactMeter;
    const bool visual = density == Density::standard
                     || density == Density::observatory;
    const bool full = density == Density::observatory;
    return {
        ! singleDomainControl,
        singleDomainControl,
        ! compactMeter,
        visual,
        visual,
        role == Role::post,
        true,
        role == Role::post && full,
        guide == GuidePresence::present,
    };
}

constexpr bool targetAllowed (Role role, ObservationTarget target) noexcept
{
    return role == Role::post || target == ObservationTarget::absolute;
}

// SPACE currently has only factual POST meaning. Correlation and field subtraction are not
// defined product facts, so selecting SPACE temporarily projects POST without destroying the
// user's preferred target for LEVEL, TIME, and FREQ.
constexpr bool targetAllowed (Role role, Domain domain, ObservationTarget target) noexcept
{
    return targetAllowed (role, target)
        && (domain != Domain::space || target == ObservationTarget::absolute);
}

constexpr ObservationTarget effectiveTarget (Role role,
                                               Domain domain,
                                               ObservationTarget preferred) noexcept
{
    return targetAllowed (role, domain, preferred)
        ? preferred : ObservationTarget::absolute;
}

struct DomainCapabilities
{
    bool level = true;
    bool time = true;
    bool frequency = false;
    bool space = true;

    constexpr bool allows (Domain domain) const noexcept
    {
        switch (domain)
        {
            case Domain::level:     return level;
            case Domain::time:      return time;
            case Domain::frequency: return frequency;
            case Domain::space:     return space;
        }
        return false;
    }
};

constexpr DomainCapabilities domainCapabilities (Role role) noexcept
{
    return { true, true, role == Role::post, true };
}

constexpr Domain sanitizeDomain (Role role, Domain requested) noexcept
{
    return domainCapabilities (role).allows (requested) ? requested : Domain::level;
}

constexpr Domain nextDomain (Role role, Domain current) noexcept
{
    auto candidate = current;
    for (int attempts = 0; attempts < 4; ++attempts)
    {
        candidate = candidate == Domain::level ? Domain::time
                  : candidate == Domain::time ? Domain::frequency
                  : candidate == Domain::frequency ? Domain::space : Domain::level;
        if (domainCapabilities (role).allows (candidate))
            return candidate;
    }
    return Domain::level;
}

constexpr std::uint8_t stateValue (Domain value) noexcept
{
    return static_cast<std::uint8_t> (value);
}

constexpr std::uint8_t stateValue (ObservationTarget value) noexcept
{
    return static_cast<std::uint8_t> (value);
}

constexpr std::uint8_t stateValue (TimeRange value) noexcept
{
    return static_cast<std::uint8_t> (value);
}

constexpr Domain domainFromState (Role role, std::uint8_t value) noexcept
{
    const auto decoded = value <= stateValue (Domain::space)
        ? static_cast<Domain> (value) : Domain::level;
    return sanitizeDomain (role, decoded);
}

constexpr ObservationTarget targetFromState (Role role, std::uint8_t value) noexcept
{
    const auto decoded = value <= stateValue (ObservationTarget::delta)
        ? static_cast<ObservationTarget> (value) : ObservationTarget::absolute;
    return targetAllowed (role, decoded) ? decoded : ObservationTarget::absolute;
}

constexpr TimeRange timeRangeFromState (std::uint8_t value) noexcept
{
    return value <= stateValue (TimeRange::hours24)
        ? static_cast<TimeRange> (value) : TimeRange::seconds30;
}

struct NavigationState
{
    Domain domain = Domain::level;
    ObservationTarget target = ObservationTarget::absolute;
    GuidePresence guide = GuidePresence::absent;
};

// Receiving a Guide publishes context only. It cannot select a domain or change POST/Delta.
constexpr NavigationState receiveGuide (NavigationState current) noexcept
{
    current.guide = GuidePresence::present;
    return current;
}

static_assert (sizePresets[0].width == 300 && sizePresets[0].height == 200);
static_assert (sizePresets[1].width == 375 && sizePresets[1].height == 250);
static_assert (sizePresets[2].width == 450 && sizePresets[2].height == 300);
static_assert (sizePresets[3].width == 600 && sizePresets[3].height == 400);
static_assert (sizePresets[4].width == 900 && sizePresets[4].height == 600);
static_assert (! hasArea (shellLayout (Role::post, sizePresets[0],
                                       GuidePresence::absent).guideRail));
static_assert (hasArea (shellLayout (Role::post, sizePresets[0],
                                    GuidePresence::present).guideRail));
static_assert (! targetAllowed (Role::pre, ObservationTarget::delta));
static_assert (targetAllowed (Role::post, ObservationTarget::delta));
static_assert (! targetAllowed (Role::post, Domain::space, ObservationTarget::delta));
static_assert (effectiveTarget (Role::post, Domain::space, ObservationTarget::delta)
               == ObservationTarget::absolute);
static_assert (! domainCapabilities (Role::pre).allows (Domain::frequency));
static_assert (domainCapabilities (Role::post).allows (Domain::frequency));
static_assert (nextDomain (Role::pre, Domain::time) == Domain::space);
}
