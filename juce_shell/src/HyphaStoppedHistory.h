#pragma once

#include <juce_graphics/juce_graphics.h>

// When the transport stops, or a rest outlasts the Watch window, the pages with a six-second
// history (FREQ's field and landscape, LIVE, SHARP, SPACE's MONO field) keep what was measured on
// screen, dimmed to one shared level, and withdraw only the present: the live curve and the current
// values. What was measured stays a fact; only "now" has stopped. Nothing here changes a value.
namespace hypha::stopped_history
{
constexpr float opacity = 0.42f;

template <typename Paint>
void paintDimmed (juce::Graphics& g, Paint&& paint)
{
    g.beginTransparencyLayer (opacity);
    paint();
    g.endTransparencyLayer();
}
}
