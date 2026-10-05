#pragma once

#include "HyphaMainFrame.h"

namespace hypha::reference_ui::window_material
{
// The Reference page owns one outer frame. Child glass and configured cards fill its interior
// afterward, so their own background stage restores the inner lip/shadow before drawing facts.
inline void paintOuter (juce::Graphics& g, juce::Rectangle<float> window)
{
    if (window.isEmpty()) return;
    const juce::Graphics::ScopedSaveState saved (g);
    g.excludeClipRegion (window.getSmallestIntegerContainer());
    main_frame::paint (g, window);
}

inline void paintInterior (juce::Graphics& g, juce::Rectangle<float> window,
                           juce::Rectangle<float> filledArea)
{
    if (window.isEmpty() || filledArea.isEmpty()) return;
    const juce::Graphics::ScopedSaveState saved (g);
    juce::Path clip;
    clip.addRectangle (window.getIntersection (filledArea));
    g.reduceClipRegion (clip);
    main_frame::paint (g, window);
}
}
