#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "HyphaTheme.h"

namespace hypha
{
// Existing product title is the label: no extra tiny footer glyph or overlapping text.
class InformationButton final : public juce::Button
{
public:
    InformationButton() : Button ("Hypha information")
    {
        setComponentID ("hypha-information");
        setTitle ("Hypha information");
        setDescription ("Loaded version, update information, downloads, and hover help");
        setTooltip ("Hypha information and downloads");
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setWantsKeyboardFocus (true);
    }

    void paintButton (juce::Graphics& g, bool highlighted, bool down) override
    {
        const bool focused = highlighted || hasKeyboardFocus (true);
        g.setColour (COL_FLORA.withAlpha (down ? 0.9f : focused ? 0.6f : 0.22f));
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (1.0f), 3.0f, 1.0f);
    }
};
}
