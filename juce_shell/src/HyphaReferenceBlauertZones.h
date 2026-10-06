#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "HyphaReferenceAComparison.h"
#include "HyphaReferenceHelp.h"
#include "HyphaReferenceHelpText.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"
#include "reference_audition/ReferenceBlauertBands.h"

#include <algorithm>
#include <cmath>

// 2026-10-04：Blauert の帯（ReferenceBlauertBands.h）を Reference
// のスペクトルの図（B の Balance、C の SPECTRUM、V の Check のタブ）に薄く敷き、図の右上に
// 「1k vs 300-400·3-4k / A 1.2 dB LOWER」（300-400 Hz・3-4 kHz に対する 1 kHz の高さを、A を主語に。日本語は
// 「300-400·3-4kに対する1k / Aが1.2 dB低い」。HyphaReferenceAComparison.h）を出す。横軸はどの図も minimumHz〜maximumHz の対数。
// 3–4 kHz まで描かない図（LOW FREQUENCY）には出さない。差が出せないあいだは数字を出さない（R-26）。
namespace hypha::reference_ui
{
inline bool blauertShown (double minimumHz, double maximumHz) noexcept
{
    return minimumHz > 0.0 && minimumHz <= reference_audition::blauertZones.front().lowHz
        && maximumHz >= reference_audition::blauertZones.back().highHz;
}

// 曲線と目盛りの数字より先に描く（線の下に敷く。下の 14 px は目盛りの数字の段）。
inline void paintBlauertZones (juce::Graphics& g, juce::Rectangle<float> chart, double minimumHz, double maximumHz)
{
    if (! blauertShown (minimumHz, maximumHz) || chart.getWidth() < 80.0f || chart.getHeight() < 40.0f) return;
    const auto x = [&chart, minimumHz, maximumHz] (double hz)
    { return chart.getX() + static_cast<float> (std::log (hz / minimumHz) / std::log (maximumHz / minimumHz)) * chart.getWidth(); };
    g.setColour (COL_MUTED.withAlpha (0.08f));
    for (const auto& zone : reference_audition::blauertZones)
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (x (zone.lowHz), chart.getY(),
                                                                std::max (x (zone.highHz), x (zone.lowHz) + 2.0f), chart.getBottom() - 14.0f));
}

// A と比べる側の 1k の張り出し（blauertContrastDb）。帯域の並びが違えば値を出さない。
inline AVersus blauertVersus (const std::vector<double>& aCentersHz, const std::vector<float>& aLevelsDb,
                              const std::vector<double>& otherCentersHz, const std::vector<float>& otherLevelsDb)
{
    if (! reference_audition::blauertBandsAligned (aCentersHz, otherCentersHz)) return {};
    return { reference_audition::blauertContrastDb (aCentersHz, aLevelsDb),
             reference_audition::blauertContrastDb (otherCentersHz, otherLevelsDb) };
}

// 「1k vs 300-400·3-4k / A 1.2 dB LOWER」。どちらかが出せなければ空。
inline juce::String blauertReadout (char role, AVersus contrasts)
{
    const auto comparison = compareA (contrasts, 1, " dB", AWords::level, role);
    if (! comparison.shown()) return {};
    return juce::String::fromUTF8 (u8"1k vs 300-400·3-4k") + " / " + comparison.text;
}

inline void paintBlauertReadout (juce::Graphics& g, juce::Rectangle<float> chart, char role, AVersus contrasts,
                                 double minimumHz, double maximumHz, const presentation::Context& context)
{
    const auto text = blauertReadout (role, contrasts);
    if (text.isEmpty() || ! blauertShown (minimumHz, maximumHz) || chart.getWidth() < 160.0f || chart.getHeight() < 40.0f) return;
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::visualization));
    g.setColour (COL_TEXT_SECONDARY.withAlpha (0.92f));
    const auto line = chart.withHeight (14.0f).reduced (4.0f, 0.0f);
    const auto textWidth = std::min (line.getWidth(), std::ceil (text_style::shownWidth (g.getCurrentFont(), text)) + 4.0f);
    help::note (line.withLeft (line.getRight() - textWidth), help_text::blauert);
    text_style::drawEllipsized (g, text, line.toNearestInt(), juce::Justification::centredRight);
}
}
