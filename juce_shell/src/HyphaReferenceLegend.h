#pragma once

#include "HyphaTextStyle.h"
#include <cmath>

namespace hypha::reference_ui
{
// 目標の範囲（B は Reference のセットの曲の帯、C は Cue の p10〜p90 の帯）。凡例には帯の見本を添える。
inline constexpr const char* targetRangeLegend = "TARGET RANGE";
inline constexpr int legendSwatchWidth = 18;  // 見本 14 と文字のあいだ 4

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
// colour alone never identifies a source. `firstExtra` is room beside the first label.
inline int referenceLegendRows (const juce::Font& font, const juce::StringArray& parts, int width, int extra = 0, int firstExtra = 0)
{
    int rows = 1, used = 0;
    for (int index = 0; index < parts.size(); ++index)
    {
        const auto needed = referenceTextWidth (font, parts[index]) + extra + (index == 0 ? firstExtra : 0);
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

inline int referenceLegendCellWidth (const juce::Font& font, const juce::String& part, bool first, int firstExtra)
{
    return referenceTextWidth (font, part) + (first ? firstExtra : 0) + (part == targetRangeLegend ? legendSwatchWidth : 0);
}

inline std::vector<ReferenceLegendCell> referenceLegendCells (const juce::Font& font,
    const juce::StringArray& parts, int width, int firstExtra = 0)
{
    std::vector<ReferenceLegendCell> cells;
    const auto row = juce::jmax (18, juce::roundToInt (std::ceil (font.getHeight() * 1.2f)));
    int x = 0, y = 0;
    for (const auto& part : parts)
    {
        const auto needed = referenceLegendCellWidth (font, part, cells.empty(), firstExtra);
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

inline void paintReferenceLegend (juce::Graphics& g, const juce::String& detail, juce::Rectangle<int> area, int firstExtra = 0)
{
    const auto cells = referenceLegendCells (g.getCurrentFont(), referenceLegendParts (detail), area.getWidth(), firstExtra);
    for (size_t index = 0; index < cells.size(); ++index)
    {
        const auto& cell = cells[index];
        g.setColour (index == 0 && cell.text.startsWith ("A") ? COL_FLORA_BR.withAlpha (0.92f)
                    : index == 1 ? COL_SPECTRUM_DELTA_BR.withAlpha (0.92f) : COL_TEXT_TERTIARY);
        auto target = cell.area.translated (area.getX(), area.getY());
        if (cell.text == targetRangeLegend)
        {
            const auto swatch = target.removeFromLeft (legendSwatchWidth).withTrimmedRight (legendSwatchWidth - 14);
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.30f));
            g.fillRect (swatch.toFloat().withSizeKeepingCentre (14.0f, 8.0f));
            g.setColour (COL_TEXT_TERTIARY);
        }
        if (cell.wrapped) text_style::drawLines (g, cell.text, target, juce::Justification::centredLeft, 2);
        else text_style::drawEllipsized (g, cell.text, target, juce::Justification::centredLeft);
    }
}

struct ReferenceHeaderLayout
{
    juce::Rectangle<int> title, legend;
    int titleWidth = 0, width = 0;
    float height = 0.0f;
};

// The heading and its legend share a row when they fit. `firstExtra` is room beside the legend's
// first label (the A window's arrow); the painter and the control both ask here.
inline ReferenceHeaderLayout referenceHeaderLayout (juce::Rectangle<float> bounds, const juce::String& heading,
    const juce::String& detail, presentation::Context context, int firstExtra = 0)
{
    const auto titleFont = labelFont (context, typography::TextRole::metricLabel, typography::Composition::visualization);
    const auto legendFont = labelFont (context, typography::TextRole::legend, typography::Composition::visualization);
    ReferenceHeaderLayout layout;
    layout.titleWidth = referenceTextWidth (titleFont, heading);
    layout.width = juce::jmax (1, juce::roundToInt (bounds.getWidth()) - 18);
    const auto parts = referenceLegendParts (detail);
    int legendWidth = 0;
    for (int index = 0; index < parts.size(); ++index)
        legendWidth += referenceLegendCellWidth (legendFont, parts[index], index == 0, firstExtra) + 10;
    const bool shared = layout.titleWidth + 12 + legendWidth <= layout.width;
    const auto titleHeight = juce::jmax (24, 2 + juce::roundToInt (std::ceil (
        text_style::shownWrappedHeight (heading, titleFont, layout.width))));
    const auto legendHeight = referenceLegendHeight (referenceLegendCells (legendFont, parts, layout.width, firstExtra));
    layout.height = shared ? 28.0f : float (titleHeight + legendHeight + 2);
    auto header = bounds.removeFromTop (layout.height).reduced (9.0f, 1.0f).toNearestInt();
    layout.title = shared ? header.removeFromLeft (layout.titleWidth + 12) : header.removeFromTop (titleHeight);
    layout.legend = header;
    return layout;
}

// Where the header paints its first legend label.
inline juce::Rectangle<int> referenceHeaderFirstCell (juce::Rectangle<float> bounds, const juce::String& heading,
    const juce::String& detail, presentation::Context context, int firstExtra)
{
    const auto layout = referenceHeaderLayout (bounds, heading, detail, context, firstExtra);
    const auto legendFont = labelFont (context, typography::TextRole::legend, typography::Composition::visualization);
    const auto cells = referenceLegendCells (legendFont, referenceLegendParts (detail), layout.legend.getWidth(), firstExtra);
    return cells.empty() ? juce::Rectangle<int> {} : cells.front().area.translated (layout.legend.getX(), layout.legend.getY());
}

inline void paintReferenceHeader (juce::Graphics& g, juce::Rectangle<float>& bounds,
                                 const juce::String& heading, const juce::String& detail, presentation::Context context,
                                 int firstExtra = 0)
{
    const auto layout = referenceHeaderLayout (bounds, heading, detail, context, firstExtra);
    bounds.removeFromTop (layout.height);
    g.setColour (COL_NORMAL.withAlpha (0.92f)); g.setFont (labelFont (context, typography::TextRole::metricLabel, typography::Composition::visualization));
    if (layout.titleWidth > layout.width) text_style::drawLines (g, heading, layout.title, juce::Justification::centredLeft, 2);
    else text_style::drawEllipsized (g, heading, layout.title, juce::Justification::centredLeft);
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization));
    paintReferenceLegend (g, detail, layout.legend, firstExtra);
}
}
