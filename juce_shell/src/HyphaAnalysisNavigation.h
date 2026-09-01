#pragma once

#include <array>

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

// TIME has four factual readings. FREQ remains a first-level domain because frequency is an
// independent observation axis, while these four readings all explain change through time.
constexpr std::array<Page, 4> timePages {
    Page::meters, Page::attack, Page::perceptual, Page::absolute
};

constexpr bool isTimePage (Page page) noexcept
{
    return page == Page::meters || page == Page::attack
        || page == Page::perceptual || page == Page::absolute;
}

constexpr Page nextTimePage (Page page) noexcept
{
    return page == Page::meters ? Page::attack
         : page == Page::attack ? Page::perceptual
         : page == Page::perceptual ? Page::absolute : Page::meters;
}

constexpr const char* timePageLabel (Page page) noexcept
{
    return page == Page::meters ? "HISTORY"
         : page == Page::attack ? "ATTACK"
         : page == Page::perceptual ? "SHARP"
         : page == Page::absolute ? "LIVE" : "";
}

// ATTACK, FREQ, SHARP, and LIVE are views inside one owned Analysis slot. Only an explicit return
// to METERS releases that slot; changing the analyzer must never give a waiting instance a race.
constexpr bool releasesSlot (Page previous, Page next) noexcept
{
    return isAnalysis (previous) && next == Page::meters;
}

static_assert (timePages.size() == 4u);
static_assert (nextTimePage (Page::absolute) == Page::meters);
static_assert (! isTimePage (Page::spectrum));
}
