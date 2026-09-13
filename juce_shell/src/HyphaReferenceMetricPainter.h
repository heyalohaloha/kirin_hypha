#pragma once

#include <juce_graphics/juce_graphics.h>

#include "HyphaPresentationContext.h"

namespace hypha::reference_metric_painter
{
void paintPanel (juce::Graphics&, juce::Rectangle<float>, float alpha = 0.66f);
void paintComparisonRoots (juce::Graphics&, juce::Rectangle<float>);
void paintMetric (juce::Graphics&, juce::Rectangle<float>, const juce::String& name,
                  const juce::String& unit, double a, double b, double delta,
                  presentation::Context, const juce::String& side = "B");
void paintCompactDelta (juce::Graphics&, juce::Rectangle<float>, const juce::String& name,
                        double value, const juce::String& unit, presentation::Context, const juce::String& side = "B");
}
