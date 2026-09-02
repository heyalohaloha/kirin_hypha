#include "HyphaObservatoryView.h"

#include <utility>

namespace hypha::observatory
{
Button::Button (juce::String text, bool tabIn)
    : juce::TextButton (std::move (text)), tab (tabIn)
{
    setWantsKeyboardFocus (true);
}

void Button::paintButton (juce::Graphics& g, bool highlighted, bool down)
{
    const auto area = getLocalBounds().toFloat().reduced (1.0f);
    const bool selected = getToggleState();
    if (! tab)
    {
        g.setColour ((down ? kFieldFill.brighter (0.08f) : kFieldFill)
                         .withAlpha (highlighted ? 0.92f : 0.72f));
        g.fillRoundedRectangle (area, 3.0f);
        g.setColour ((selected ? COL_SPECTRUM_DELTA_BR : COL_MUTED)
                         .withAlpha (selected ? 0.78f : 0.34f));
        g.drawRoundedRectangle (area, 3.0f, selected ? 0.9f : 0.6f);
    }
    else if (highlighted)
    {
        g.setColour (kFieldFill.withAlpha (0.34f));
        g.fillRoundedRectangle (area, 2.0f);
    }

    const auto textColour = ! isEnabled() ? COL_MUTED.withAlpha (0.32f)
                          : selected ? COL_FLORA_BR
                          : highlighted ? COL_NORMAL.withAlpha (0.82f) : COL_MUTED;
    g.setColour (textColour);
    g.setFont (labelFont (juce::jlimit (7.0f, tab ? 11.0f : 10.0f,
                                        static_cast<float> (getHeight()) * 0.38f)));
    g.drawFittedText (getButtonText(), getLocalBounds().reduced (3, 1),
                      juce::Justification::centred, 1, 0.78f);
    if (tab && selected)
    {
        const float width = juce::jmin (area.getWidth() * 0.66f, 34.0f);
        g.setColour (COL_FLORA_BR.withAlpha (0.92f));
        g.fillRect (area.getCentreX() - width * 0.5f, area.getBottom() - 1.0f, width, 1.0f);
    }
    if (hasKeyboardFocus (true))
    {
        g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.92f));
        g.drawRoundedRectangle (area.reduced (1.0f), 3.0f, 1.0f);
    }
}
}
