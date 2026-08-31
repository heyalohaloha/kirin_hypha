#pragma once

namespace hypha::analysis_navigation
{
enum class Page
{
    meters,
    attack,
    spectrum,
    perceptual,
    absolute
};

constexpr bool isAnalysis (Page page) noexcept
{
    return page != Page::meters;
}

// ATTACK, FREQ, SHARP, and LIVE are views inside one owned Analysis slot. Only an explicit return
// to METERS releases that slot; changing the analyzer must never give a waiting instance a race.
constexpr bool releasesSlot (Page previous, Page next) noexcept
{
    return isAnalysis (previous) && next == Page::meters;
}
}
