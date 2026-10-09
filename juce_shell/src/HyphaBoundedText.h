#pragma once
#include "HyphaTextStyle.h"

namespace hypha::bounded_text
{
inline void metricState (juce::Graphics& g, const juce::String& state,
                         juce::Rectangle<float> area, presentation::Context context,
                         typography::TextRole role, juce::Justification justification)
{
    const auto text = text_style::shownText (state);
    const auto font = displayTextFont (text, context, role, typography::Composition::facts, area, true);
    const juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (area.toNearestInt());
    drawTabularText (g, font, text, area, justification);
}
}
