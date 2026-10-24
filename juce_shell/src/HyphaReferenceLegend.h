#pragma once

#include "HyphaTextStyle.h"
#include <cmath>

namespace hypha::reference_ui
{
inline juce::StringArray referenceLegendParts (const juce::String& detail)
{
    auto parts = juce::StringArray::fromTokens (detail, "/", "");
    parts.trim();
    return parts;
}

inline int referenceTextWidth (const juce::Font& font, const juce::String& text)
{
    return juce::roundToInt (std::ceil (text_style::shownWidth (font, text))) + 2;
}

// Wrap whole source labels before reserving the plot. The text keeps its scope;
// colour alone never identifies a source.
inline int referenceLegendRows (const juce::Font& font, const juce::StringArray& parts, int width, int extra = 0)
{
    int rows = 1, used = 0;
    for (const auto& part : parts)
    {
        const auto needed = referenceTextWidth (font, part) + extra;
        if (used > 0 && used + 10 + needed > width) { ++rows; used = 0; }
        used += (used > 0 ? 10 : 0) + needed;
    }
    return rows;
}

struct ReferenceLegendCell
{
    juce::String text;
    juce::Rectangle<int> area;
    bool wrapped = false;
};

inline std::vector<ReferenceLegendCell> referenceLegendCells (const juce::Font& font,
    const juce::StringArray& parts, int width)
{
    std::vector<ReferenceLegendCell> cells;
    const auto row = juce::jmax (18, juce::roundToInt (std::ceil (font.getHeight() * 1.2f)));
    int x = 0, y = 0;
    for (const auto& part : parts)
    {
        const auto needed = referenceTextWidth (font, part);
        if (x > 0 && x + needed > width) { x = 0; y += row; }
        const bool wrap = needed > width;
        const auto height = wrap ? juce::jmax (row, 2 + juce::roundToInt (std::ceil (
            text_style::shownWrappedHeight (part, font, width)))) : row;
        cells.push_back ({part, {x, y, juce::jmin (needed, width), height}, wrap});
        if (wrap) { x = 0; y += height; }
        else x += needed + 10;
    }
    return cells;
}

inline int referenceLegendHeight (const std::vector<ReferenceLegendCell>& cells)
{
    int height = 0;
    for (const auto& cell : cells) height = juce::jmax (height, cell.area.getBottom());
    return height;
}

inline void paintReferenceLegend (juce::Graphics& g, const juce::String& detail, juce::Rectangle<int> area)
{
    const auto cells = referenceLegendCells (g.getCurrentFont(), referenceLegendParts (detail), area.getWidth());
    for (size_t index = 0; index < cells.size(); ++index)
    {
        const auto& cell = cells[index];
        g.setColour (index == 0 && cell.text.startsWith ("A") ? COL_FLORA_BR.withAlpha (0.92f)
                    : index == 1 ? COL_SPECTRUM_DELTA_BR.withAlpha (0.92f) : COL_TEXT_TERTIARY);
        const auto target = cell.area.translated (area.getX(), area.getY());
        if (cell.wrapped) text_style::drawLines (g, cell.text, target, juce::Justification::centredLeft, 2);
        else text_style::drawEllipsized (g, cell.text, target, juce::Justification::centredLeft);
    }
}

inline void paintReferenceHeader (juce::Graphics& g, juce::Rectangle<float>& bounds,
                                 const juce::String& heading, const juce::String& detail, presentation::Context context)
{
    const auto titleFont = labelFont (context, typography::TextRole::metricLabel, typography::Composition::visualization);
    const auto legendFont = labelFont (context, typography::TextRole::legend, typography::Composition::visualization);
    const auto titleWidth = referenceTextWidth (titleFont, heading);
    const auto width = juce::jmax (1, juce::roundToInt (bounds.getWidth()) - 18);
    const auto parts = referenceLegendParts (detail);
    int legendWidth = 0;
    for (const auto& part : parts) legendWidth += referenceTextWidth (legendFont, part) + 10;
    const bool shared = titleWidth + 12 + legendWidth <= width;
    const auto titleHeight = juce::jmax (24, 2 + juce::roundToInt (std::ceil (
        text_style::shownWrappedHeight (heading, titleFont, width))));
    const auto legendHeight = referenceLegendHeight (referenceLegendCells (legendFont, parts, width));
    auto header = bounds.removeFromTop (shared ? 28.0f : float (titleHeight + legendHeight + 2))
                       .reduced (9.0f, 1.0f).toNearestInt();
    auto title = shared ? header.removeFromLeft (titleWidth + 12) : header.removeFromTop (titleHeight);
    g.setColour (COL_NORMAL.withAlpha (0.92f)); g.setFont (labelFont (context, typography::TextRole::metricLabel, typography::Composition::visualization));
    if (titleWidth > width) text_style::drawLines (g, heading, title, juce::Justification::centredLeft, 2);
    else text_style::drawEllipsized (g, heading, title, juce::Justification::centredLeft);
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization));
    paintReferenceLegend (g, detail, header);
}
}
