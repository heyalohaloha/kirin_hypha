#pragma once

#include "HyphaTextStyle.h"
#include <cmath>

namespace hypha::reference_ui
{
// Match the plotted ink without adding an overlay or a second row. Text retains its
// observation scope (LIVE, CAPTURE, CUE, TRACK); colour alone never identifies a source.
inline void paintReferenceLegend (juce::Graphics& g, const juce::String& detail,
                                  juce::Rectangle<int> area)
{
    auto parts = juce::StringArray::fromTokens (detail, "/", "");
    parts.trim();
    const bool sourceLegend = parts.size() >= 2 && parts[0].startsWith ("A")
        && (parts[1].startsWith ("B") || parts[1].startsWith ("C") || parts[1].startsWith ("V"));
    if (!sourceLegend)
    {
        g.setColour (COL_TEXT_TERTIARY.withAlpha (0.92f));
        text_style::drawEllipsized (g, detail, area, juce::Justification::centredRight);
        return;
    }
    constexpr int gap = 10;
    int width = gap * (parts.size() - 1);
    for (const auto& part : parts)
        width += juce::roundToInt (std::ceil (text_style::shownWidth (g.getCurrentFont(), part)));
    const bool fits = width <= area.getWidth();
    auto row = area.removeFromRight (juce::jmin (width, area.getWidth()));
    for (int index = 0; index < parts.size(); ++index)
    {
        const auto desired = juce::roundToInt (std::ceil (text_style::shownWidth (g.getCurrentFont(), parts[index])));
        const auto remaining = parts.size() - index;
        const auto available = fits ? desired : juce::jmax (0, (row.getWidth() - gap * (remaining - 1)) / remaining);
        auto cell = row.removeFromLeft (juce::jmin (desired, available));
        g.setColour (index == 0 ? COL_SPECTRUM_DELTA_BR.withAlpha (0.92f)
                    : index == 1 ? COL_FLORA.withAlpha (0.86f) : COL_TEXT_TERTIARY);
        text_style::drawEllipsized (g, parts[index], cell, juce::Justification::centredLeft);
        row.removeFromLeft (gap);
    }
}
}
