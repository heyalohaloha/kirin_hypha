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

inline void paintReferenceLegend (juce::Graphics& g, const juce::String& detail, juce::Rectangle<int> area)
{
    const auto parts = referenceLegendParts (detail);
    const auto rows = referenceLegendRows (g.getCurrentFont(), parts, area.getWidth());
    const auto height = area.getHeight() / rows;
    const auto left = area.getX(), right = area.getRight();
    auto x = left, y = area.getY();
    for (int index = 0; index < parts.size(); ++index)
    {
        const auto width = referenceTextWidth (g.getCurrentFont(), parts[index]);
        if (x > left && x + width > right) { x = left; y += height; }
        g.setColour (index == 0 && parts[index].startsWith ("A") ? COL_FLORA_BR.withAlpha (0.92f)
                    : index == 1 ? COL_SPECTRUM_DELTA_BR.withAlpha (0.92f) : COL_TEXT_TERTIARY);
        text_style::drawEllipsized (g, parts[index], {x, y, juce::jmin (width, right - x), height}, juce::Justification::centredLeft);
        x += width + 10;
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
    const auto rows = referenceLegendRows (legendFont, parts, width);
    auto header = bounds.removeFromTop (shared ? 28.0f : 26.0f + 18.0f * rows).reduced (9.0f, 1.0f).toNearestInt();
    auto title = shared ? header.removeFromLeft (titleWidth + 12) : header.removeFromTop (24);
    g.setColour (COL_NORMAL.withAlpha (0.92f)); g.setFont (labelFont (context, typography::TextRole::metricLabel, typography::Composition::visualization));
    text_style::drawEllipsized (g, heading, title, juce::Justification::centredLeft);
    g.setFont (labelFont (context, typography::TextRole::legend, typography::Composition::visualization));
    paintReferenceLegend (g, detail, header);
}
}
