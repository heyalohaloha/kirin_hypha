#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "HyphaAttackUiContract.h"

// DRUM BAND (B-1097). One octave band of every hit, chosen on the header's second row. ALL is
// the DRUM that measures the whole signal; a band filters each hit to that octave on PRE and on
// POST (kirin_measure attack_perception::band) and pairs the two at the PRE onset. Everything
// here is geometry and the fixed band table: nothing depends on a measured value.
namespace hypha::attack_band
{
constexpr std::size_t bandCount = 8;
constexpr std::size_t choiceCount = bandCount + 1; // ALL, then the eight bands

// IEC 61260 base-two octave bands with their ISO 266 nominal labels: centres 62.5 Hz * 2^(i-1),
// edges half an octave either side. The engine's AttackBand uses the same centres; every hit it
// reports carries the band's time resolution (one period of the centre).
struct Band
{
    const char* label;
    float centreHz;
    constexpr float lowHz() const noexcept { return centreHz * 0.70710678f; }
    constexpr float highHz() const noexcept { return centreHz * 1.41421356f; }
    constexpr float periodMs() const noexcept { return 1'000.0f / centreHz; }
};

constexpr std::array<Band, bandCount> bands {{
    { "63", 62.5f }, { "125", 125.0f }, { "250", 250.0f }, { "500", 500.0f },
    { "1k", 1'000.0f }, { "2k", 2'000.0f }, { "4k", 4'000.0f }, { "8k", 8'000.0f } }};

// `band` is the engine's index: 0 = ALL, 1..8 = bands[0..7].
constexpr const Band* bandFor (std::uint8_t band) noexcept
{
    return band >= 1 && band <= bandCount ? &bands[band - 1] : nullptr;
}

constexpr const char* labelFor (std::uint8_t band) noexcept
{
    return band == 0 ? "ALL" : bandFor (band) != nullptr ? bandFor (band)->label : "";
}

// Chips: "BAND" and the nine choices on the header's second row, left of the legend, at 125%
// and above. 100% has no header and keeps the chosen band (INV-S38).
constexpr int chipWidthFor (observatory::Density density) noexcept
{
    switch (density)
    {
        case observatory::Density::compact:     return 0;
        case observatory::Density::focused:     return 30;
        case observatory::Density::standard:    return 34;
        case observatory::Density::observatory: return 40;
        case observatory::Density::inspection:  return 46;
    }
    return 0;
}

constexpr int chipCaptionWidthFor (observatory::Density density) noexcept
{
    switch (density)
    {
        case observatory::Density::compact:     return 0;
        case observatory::Density::focused:     return 36;
        case observatory::Density::standard:    return 40;
        case observatory::Density::observatory: return 46;
        case observatory::Density::inspection:  return 54;
    }
    return 0;
}

// The caption and the chips together. Empty where the header has no second row, or is too
// narrow for all nine (a capture layout, never an editor size).
constexpr attack_ui::Box chipRow (const attack_ui::Layout& layout,
                                  const presentation::Context& context) noexcept
{
    const auto title = attack_ui::titleRowHeight (context);
    const auto width = chipCaptionWidthFor (context.density)
                     + static_cast<int> (choiceCount) * chipWidthFor (context.density);
    if (layout.arrangement == attack_ui::Arrangement::glance || width <= 0
        || layout.header.height <= title || width > layout.header.width - 40)
        return {};
    return { layout.header.x, layout.header.y + title, width, layout.header.height - title };
}

constexpr attack_ui::Box chipCaption (const attack_ui::Layout& layout,
                                      const presentation::Context& context) noexcept
{
    const auto row = chipRow (layout, context);
    if (row.empty())
        return {};
    return { row.x, row.y, chipCaptionWidthFor (context.density), row.height };
}

constexpr attack_ui::Box chipCell (const attack_ui::Layout& layout,
                                   const presentation::Context& context,
                                   std::size_t choice) noexcept
{
    const auto row = chipRow (layout, context);
    if (row.empty() || choice >= choiceCount)
        return {};
    const auto width = chipWidthFor (context.density);
    return { row.x + chipCaptionWidthFor (context.density) + static_cast<int> (choice) * width,
             row.y, width, row.height };
}

// Panes: at 200% and 300% the HISTORY row shows the selected hit in the band, its head
// magnified (where a few ms of DELAY are visible) and its tail (where the ring-out is). Below
// that, HISTORY keeps the six-second envelope and the lanes alone carry the band.
constexpr int paneMinimumHeight = attack_ui::bandPaneMinimumHeight;
constexpr int paneGap = 8;
constexpr float headFromMs = -5.0f;
constexpr float headToMs = 40.0f;
constexpr float tailFromMs = 0.0f;
constexpr float tailToMs = 300.0f;
// The envelope windows the engine delivers: HEAD over [-20, +40) ms, TAIL over [0, 300) ms.
constexpr float headPointsFromMs = -20.0f;
constexpr float headPointsToMs = 40.0f;

constexpr bool panesShown (const attack_ui::Layout& layout) noexcept
{
    return layout.arrangement == attack_ui::Arrangement::lanes
        && attack_ui::historyPlot (layout).height >= paneMinimumHeight;
}

constexpr attack_ui::Box headPane (const attack_ui::Layout& layout) noexcept
{
    if (! panesShown (layout))
        return {};
    const auto plot = attack_ui::historyPlot (layout);
    return { plot.x, plot.y, (plot.width - paneGap) * 2 / 5, plot.height };
}

constexpr attack_ui::Box tailPane (const attack_ui::Layout& layout) noexcept
{
    if (! panesShown (layout))
        return {};
    const auto plot = attack_ui::historyPlot (layout);
    const auto head = headPane (layout);
    return { head.right() + paneGap, plot.y, plot.right() - head.right() - paneGap, plot.height };
}

// The five editor bodies: chips from 125%, panes at 200% and 300% only.
static_assert (chipRow (attack_ui::layoutFor (292, 120, presentation::forEditor (300, 200)),
                        presentation::forEditor (300, 200)).empty());
static_assert (! chipRow (attack_ui::layoutFor (363, 158, presentation::forEditor (375, 250)),
                          presentation::forEditor (375, 250)).empty());
static_assert (chipCell (attack_ui::layoutFor (363, 158, presentation::forEditor (375, 250)),
                         presentation::forEditor (375, 250), choiceCount - 1).right() <= 363);
static_assert (! chipRow (attack_ui::layoutFor (872, 412, presentation::forEditor (900, 600)),
                          presentation::forEditor (900, 600)).empty());
static_assert (! panesShown (attack_ui::layoutFor (434, 164, presentation::forEditor (450, 300))));
static_assert (panesShown (attack_ui::layoutFor (580, 248, presentation::forEditor (600, 400))));
static_assert (panesShown (attack_ui::layoutFor (872, 412, presentation::forEditor (900, 600))));
static_assert (tailPane (attack_ui::layoutFor (872, 412, presentation::forEditor (900, 600))).right()
               == attack_ui::historyPlot (attack_ui::layoutFor (872, 412,
                                                                presentation::forEditor (900, 600))).right());
static_assert (headPane (attack_ui::layoutFor (580, 248, presentation::forEditor (600, 400))).right()
               + paneGap
               == tailPane (attack_ui::layoutFor (580, 248, presentation::forEditor (600, 400))).x);
}
