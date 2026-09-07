#include "HyphaAttackPainter.h"

#include "HyphaTheme.h"

namespace hypha::attack_painter
{
void drawMetricFact (juce::Graphics& g, juce::Rectangle<int> area,
                     const juce::String& title, const juce::String& value,
                     const juce::String& context, juce::Colour colour, bool alignRight)
{
    const auto justification = alignRight ? juce::Justification::centredRight
                                          : juce::Justification::centredLeft;
    const bool inspection = area.getWidth() >= 150 && area.getHeight() >= 60;
    g.setColour (colour.withAlpha (0.92f));
    g.setFont (monoFont (inspection ? 14.0f : 11.0f));
    g.drawText (title, area.removeFromTop (juce::jmin (inspection ? 18 : 13,
                                                       area.getHeight())), justification);
    g.setColour (colour);
    g.setFont (monoFont (inspection ? 18.0f : area.getHeight() >= 28 && area.getWidth() >= 76 ? 12.4f : 11.0f));
    g.drawText (value, area.removeFromTop (juce::jmin (inspection ? 27 : 19,
                                                       area.getHeight())), justification);
    if (area.getHeight() >= 11 && context.isNotEmpty())
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (inspection ? 12.0f : 11.0f));
        g.drawText (context, area, justification);
    }
}

}
