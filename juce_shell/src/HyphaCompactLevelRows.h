#pragma once

#include "HyphaBoundedText.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

namespace hypha::observatory::compact_level_rows
{
inline void paintRow (juce::Graphics& g, juce::Rectangle<int> area, const juce::String& key,
                      const juce::String& value, const juce::String& unit, const juce::String& bound,
                      bool available, presentation::Context context)
{
    auto columns = area.reduced (10, 0);
    const auto name = columns.removeFromLeft (columns.getWidth() / 3);
    const auto unitBox = columns.removeFromRight (columns.getWidth() / 3);
    g.setColour (COL_TEXT_SECONDARY);
    g.setFont (labelFont (context, typography::TextRole::metricLabel, typography::Composition::facts));
    text_style::drawText (g, key, name, juce::Justification::centredLeft);
    g.setColour (available ? COL_OBSERVATORY_VALUE : COL_MUTED);
    if (bound.isEmpty())
        drawTabularText (g, monoFont (context, typography::TextRole::primaryValue, typography::Composition::facts),
                         value, columns.reduced (3, 0).toFloat(), juce::Justification::centred);
    else
        bounded_text::metricState (g, bound, columns.reduced (3, 0).toFloat(), context,
                                   typography::TextRole::primaryValue, juce::Justification::centred);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::facts));
    text_style::drawText (g, unit, unitBox, juce::Justification::centredRight);
}

inline juce::Rectangle<int> take (juce::Rectangle<int>& area, int left)
{
    const auto row = area.removeFromTop (area.getHeight() / left);
    return row.reduced (4, 1);
}
}
