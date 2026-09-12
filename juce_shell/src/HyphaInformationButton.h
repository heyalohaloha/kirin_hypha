#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "HyphaSurfaceMaterial.h"
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
        if (! focused && ! down)
            return;

        const auto area = getLocalBounds().toFloat().reduced (1.0f);
        if (down)
        {
            g.setColour (kFieldFill.withAlpha (0.16f));
            g.fillRoundedRectangle (area, 3.0f);
        }
        g.setColour (COL_FLORA_BR.withAlpha (down ? 0.48f : 0.30f));
        g.drawRoundedRectangle (area, 3.0f, down ? 0.90f : 0.65f);
    }
};
}
