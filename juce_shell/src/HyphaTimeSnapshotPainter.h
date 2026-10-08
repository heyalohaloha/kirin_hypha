#pragma once

#include "HyphaTimeSnapshotPresentation.h"
#include "HyphaMeterContext.h"
#include "HyphaPresentationContext.h"
#include "HyphaTypographyContract.h"
#include <juce_graphics/juce_graphics.h>

namespace hypha::time_snapshot
{
inline constexpr auto psrReadoutRole = typography::TextRole::legend;
struct Geometry
{
    juce::Rectangle<int> mainReadout, mainFacts, mainStatus, mainAxis;
    juce::Rectangle<float> mainPlot;
    juce::Rectangle<int> psrReadout, psrHelp, psrStatus, psrAxis;
    juce::Rectangle<float> psrPlot;
};

Geometry geometry (juce::Rectangle<int>, presentation::Context, bool showPsr);
juce::String targetText (std::uint8_t);
juce::String currentText (const Component&, Metric, bool includeTarget = false);
juce::String currentReason (const Component&);
juce::String helpAt (juce::Rectangle<int>, const Presentation&, presentation::Context,
                     juce::Point<int>);
void paint (juce::Graphics&, juce::Rectangle<int>, const Presentation&,
            meter_context::ScaleMode, presentation::Context, const juce::String& rangeLabel);
}
