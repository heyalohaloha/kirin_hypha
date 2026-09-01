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
    g.setColour (colour.withAlpha (0.92f));
    g.setFont (monoFont (area.getHeight() >= 42 ? 7.5f : 6.6f));
    g.drawText (title, area.removeFromTop (juce::jmin (13, area.getHeight())), justification);
    g.setColour (colour);
    g.setFont (monoFont (area.getHeight() >= 28 ? 12.4f : 8.5f));
    g.drawText (value, area.removeFromTop (juce::jmin (19, area.getHeight())), justification);
    if (! area.isEmpty())
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (6.2f));
        g.drawText (context, area, justification);
    }
}

}
