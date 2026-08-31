#include "HyphaAttackPainter.h"

#include "HyphaTheme.h"

namespace hypha::attack_painter
{
void drawMetricCard (juce::Graphics& g,
                     juce::Rectangle<int> area,
                     const juce::String& title,
                     const juce::String& value,
                     const juce::String& detail,
                     juce::Colour colour,
                     bool active)
{
    area = area.reduced (1, 1);
    if (area.isEmpty())
        return;
    g.setColour (juce::Colour (0xff111722).withAlpha (0.92f));
    g.fillRoundedRectangle (area.toFloat(), 2.5f);
    g.setColour ((active ? colour : COL_MUTED).withAlpha (active ? 0.95f : 0.38f));
    g.fillRect (area.removeFromTop (2));
    if (area.getHeight() < 28)
    {
        g.setFont (monoFont (7.0f));
        g.setColour (active ? colour : COL_MUTED);
        g.drawText (title + "  " + value, area.reduced (3, 0),
                    juce::Justification::centredLeft);
        return;
    }
    g.setFont (monoFont (7.2f));
    g.setColour (COL_MUTED);
    g.drawText (title, area.removeFromTop (11).reduced (4, 0),
                juce::Justification::centredLeft);
    g.setFont (monoFont (9.0f));
    g.setColour (active ? colour : COL_MUTED);
    g.drawText (value, area.removeFromTop (13).reduced (4, 0),
                juce::Justification::centredLeft);
    g.setFont (monoFont (6.8f));
    g.setColour (COL_MUTED);
    g.drawText (detail, area.reduced (4, 0), juce::Justification::centredLeft);
}
}
