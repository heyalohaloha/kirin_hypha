#pragma once

#include "HyphaTheme.h"

namespace hypha::text_style
{
int requiredWidth (const juce::Font&, const juce::String&,
                   const typography::TextStyle&, int minimum = 0);
int requiredLineHeight (const typography::TextStyle&, int minimum = 0) noexcept;
void draw (juce::Graphics&, const juce::String&, juce::Rectangle<int>,
           const presentation::Context&, typography::TextRole,
           juce::Justification, int maximumLines = 1,
           typography::Composition = typography::Composition::shell);
void drawEllipsized (juce::Graphics&, const juce::String&, juce::Rectangle<int>,
                     juce::Justification);
}
