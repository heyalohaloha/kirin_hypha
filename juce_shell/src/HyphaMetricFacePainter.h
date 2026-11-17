#pragma once
#include "HyphaMetricNeedle.h"
#include "HyphaPresentationContext.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace hypha::metric_face
{
struct TextBox { juce::String text; juce::Rectangle<float> area; float fontHeight; };
// A single metric dial only. The owner supplies the existing VU header/TP/CLIP chrome.
void paint (juce::Graphics&, juce::Rectangle<int>, const metric_needle::Frame&,
            presentation::Context, std::vector<TextBox>* trace = nullptr, bool withVuChrome = false);
juce::Point<float> needlePoint (juce::Rectangle<int>, float normalized, float reach = 1.0f, bool withVuChrome = false);
}
