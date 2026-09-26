#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaObservatoryResizeContract.h"

// The editor's size rule: any 3:2 size from 100% to 300%, then only the magnified steps that keep
// the Inspection View on whole device pixels for the display it is on
// (HyphaObservatoryResizeContract.h). A corner drag or a host resize past 300% lands on the nearest
// step; the edge that is not being dragged stays where it was.
namespace hypha
{
class EditorSizeConstrainer final : public juce::ComponentBoundsConstrainer
{
public:
    EditorSizeConstrainer() { setFixedAspectRatio (1.5); }

    void setDisplayScale (float scale) noexcept { displayScale = scale > 0.0f ? scale : 1.0f; }
    float getDisplayScale() const noexcept { return displayScale; }

    // The size this rule allows nearest to `requested` (a saved size, a menu choice).
    observatory::EditorSize allowedSize (observatory::EditorSize requested) const noexcept
    {
        if (requested.width > getMaximumWidth())
            requested = { getMaximumWidth(), getMaximumHeight() };
        if (requested.width <= observatory::sizePresets.back().width)
            return requested;
        return observatory::nearestMagnifiedSize (requested.width, displayScale, getMaximumWidth());
    }

    void checkBounds (juce::Rectangle<int>& bounds, const juce::Rectangle<int>& previous,
                      const juce::Rectangle<int>& limits, bool isStretchingTop, bool isStretchingLeft,
                      bool isStretchingBottom, bool isStretchingRight) override
    {
        juce::ComponentBoundsConstrainer::checkBounds (bounds, previous, limits, isStretchingTop,
                                                       isStretchingLeft, isStretchingBottom,
                                                       isStretchingRight);
        if (bounds.getWidth() <= observatory::sizePresets.back().width)
            return;
        const auto size = observatory::nearestMagnifiedSize (bounds.getWidth(), displayScale,
                                                             getMaximumWidth());
        if (isStretchingLeft)
            bounds.setLeft (bounds.getRight() - size.width);
        else
            bounds.setWidth (size.width);
        if (isStretchingTop)
            bounds.setTop (bounds.getBottom() - size.height);
        else
            bounds.setHeight (size.height);
    }

private:
    float displayScale = 1.0f;
};
}
