#pragma once

#include <array>

namespace hypha::analysis_navigation
{
enum class Page
{
    meters,
    run,
    attack,
    spectrum,
    perceptual,
    absolute
};

constexpr bool isAnalysis (Page page) noexcept
{
    return page == Page::attack || page == Page::spectrum
        || page == Page::perceptual || page == Page::absolute;
}

// TIME has four factual readings. FREQ remains a first-level domain because frequency is an
// independent observation axis, while these four readings all explain change through time.
constexpr std::array<Page, 5> timePages {
    Page::meters, Page::run, Page::attack, Page::perceptual, Page::absolute
};

constexpr bool isTimePage (Page page) noexcept
{
    return page == Page::meters || page == Page::run || page == Page::attack
        || page == Page::perceptual || page == Page::absolute;
}

constexpr Page nextTimePage (Page page) noexcept
{
    return page == Page::meters ? Page::run
         : page == Page::run ? Page::attack
         : page == Page::attack ? Page::perceptual
         : page == Page::perceptual ? Page::absolute : Page::meters;
}

constexpr const char* timePageLabel (Page page) noexcept
{
    return page == Page::meters ? "HISTORY"
         : page == Page::run ? "RUN"
         : page == Page::attack ? "ATTACK"
         : page == Page::perceptual ? "SHARP"
         : page == Page::absolute ? "LIVE" : "";
}

// ATTACK, FREQ, SHARP, and LIVE are views inside one owned Analysis slot. HISTORY and RUN are
// read-only projections of the existing meter history and do not own that slot.
constexpr bool releasesSlot (Page previous, Page next) noexcept
{
    return isAnalysis (previous) && ! isAnalysis (next);
}

static_assert (timePages.size() == 5u);
static_assert (nextTimePage (Page::absolute) == Page::meters);
static_assert (! isTimePage (Page::spectrum));
}
