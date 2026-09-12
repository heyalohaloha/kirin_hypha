#pragma once

#include <cstddef>

#include <juce_graphics/juce_graphics.h>

#include "HyphaPresentationContext.h"
#include "kirin_hypha_ffi.h"

namespace hypha::time_history
{
struct HistoryAxis;

struct AuxiliaryLaneGeometry
{
    juce::Rectangle<int> bounds;
    juce::Rectangle<int> readout;
    juce::Rectangle<float> data;
    juce::Rectangle<int> axis;
};

struct Geometry
{
    juce::Rectangle<int> content;
    juce::Rectangle<int> legend;
    juce::Rectangle<int> mainBounds;
    juce::Rectangle<float> mainPlot;
    juce::Range<float> timelineX;
    juce::Rectangle<int> plrBounds;
    AuxiliaryLaneGeometry correlation;
};

int auxLabelWidth (presentation::Context, bool plr, bool delta, int availableWidth);
int legendBasisWidth (int availableWidth, bool compact) noexcept;
juce::Range<float> dataXRange (juce::Rectangle<int> area, bool compactMeter) noexcept;
float dataXForEntry (juce::Range<float>, const KirinMeterHistoryEntry&, const HistoryAxis&,
                     std::size_t index, std::size_t count) noexcept;
Geometry makeGeometry (juce::Rectangle<int>, bool compactMeter,
                       presentation::Context);
}
