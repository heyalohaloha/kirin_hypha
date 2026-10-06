#pragma once

#include <array>
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
    juce::Rectangle<int> definition;  // empty when the row is too narrow
    juce::Rectangle<int> plr;         // the session's PLR; empty for POST - PRE or a narrow row
    juce::Rectangle<int> correlation; // empty when the row is too narrow
};

// What PSR is, with the numbers it is made of so it never reads as a value left blank:
// "= PEAK -1.2 - S -15.8" for a measured point, "POST - PRE" for the difference. The default
// arguments give the widest text, which the layout reserves.
juce::String psrDefinition (bool delta, double peak = -70.0, double shortTerm = -70.0);
PsrReadout psrReadout (presentation::Context, juce::Rectangle<int> row, bool delta);
int legendBasisWidth (int availableWidth, bool compact) noexcept;
// The legend's value cells from the left: M, S and TP, or S and TP when M is not drawn.
std::array<juce::Rectangle<int>, 3> legendCells (juce::Rectangle<int> legend, bool compact) noexcept;
// The comparison status over a POST - PRE history.
constexpr int statusRowHeight (bool compact) noexcept { return compact ? 18 : 22; }
// What the pointed number or trace is and how it is used, for the help line and the bubble; empty
// where nothing is pointed at. `area` is the one the history is painted in. The caller names the
// side first ("POST. ", "PRE. ", "POST minus PRE. "), as for LEVEL.
juce::String helpAt (juce::Rectangle<int> area, bool delta, bool statusRow, bool compact, bool momentary,
                     presentation::Context, juce::Point<int>);
juce::Range<float> dataXRange (juce::Rectangle<int> area, bool compactMeter) noexcept;
float dataXForEntry (juce::Range<float>, const KirinMeterHistoryEntry&, const HistoryAxis&,
                     std::size_t index, std::size_t count) noexcept;
Geometry makeGeometry (juce::Rectangle<int>, bool compactMeter,
                       presentation::Context);
}
