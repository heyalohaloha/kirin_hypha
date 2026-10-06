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
    AuxiliaryLaneGeometry psr;
};

// The PSR lane's row of numbers, split so nothing in it overlaps: the current PSR on the left,
// the session facts that barely move (PLR, CORR) on the right, and what PSR is between them when
// the row has room. Each part is as wide as the widest value it can show, so nothing moves.
struct PsrReadout
{
    juce::Rectangle<int> value;
    juce::Rectangle<int> definition; // empty when the row is too narrow
    juce::Rectangle<int> facts;      // empty when the row is too narrow
};

juce::String psrDefinition (bool delta);
PsrReadout psrReadout (presentation::Context, juce::Rectangle<int> row, bool delta);
int legendBasisWidth (int availableWidth, bool compact) noexcept;
juce::Range<float> dataXRange (juce::Rectangle<int> area, bool compactMeter) noexcept;
float dataXForEntry (juce::Range<float>, const KirinMeterHistoryEntry&, const HistoryAxis&,
                     std::size_t index, std::size_t count) noexcept;
Geometry makeGeometry (juce::Rectangle<int>, bool compactMeter,
                       presentation::Context);
}
