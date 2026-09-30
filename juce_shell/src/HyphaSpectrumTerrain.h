#pragma once

#include <cstddef>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "kirin_hypha_ffi.h"

namespace hypha::absolute_spectrum { class History; }

// FREQ landscape: the last six seconds of measured POST level as a perspective range of ridges
// behind the flat reading curve. Its front edge is the reading plot itself (the same x for every
// frequency and the same y for every level), and older ridges recede toward a horizon, shrinking
// with perspective and fading with age. Each ridge is a quarter second on the running clock and
// holds the highest level each column reached in it, so a hit keeps its top as it moves back
// instead of showing how it decayed; its depth follows the time since the quarter ended, so a
// ridge slides back continuously and fades out at the far end (2026-09-29, Daisuke). Ridges are
// drawn from the newest back, each with a curtain that hides what lies behind it, so the range
// reads as solid (the ridge-line technique). A quarter with no measured frame has no ridge, and
// grid lines join only neighbouring quarters, so a gap in the measurement stays empty. The
// landscape adds no value to the reading: the current level is still the flat curve drawn over it.
namespace hypha::spectrum_terrain
{
constexpr double sliceSeconds = 0.25;      // one ridge a quarter second, on the running clock
constexpr int columnCount = 96;            // across the frequency axis
constexpr float minimumPlotWidth = 360.0f; // narrower plots keep the flat six-second field
constexpr float levelFloorDbfs = -96.0f;

struct Source
{
    size_t count = 0u;                               // chronological, oldest first
    std::function<double (size_t)> timeSeconds;     // each frame's end on the running clock
    // The highest value between two normalised plot x positions, so a narrow peak between two
    // ridge columns is kept rather than interpolated away.
    std::function<float (size_t, float, float)> peakIn;
};

struct Scale
{
    float frontZeroY = 0.0f;      // y of value 0 on the flat plot
    float pixelsPerUnit = 1.0f;   // flat-plot pixels per unit, upward
    float minimum = 0.0f;         // values are clipped to the plot's own range
    float maximum = 0.0f;
    float horizon = 0.06f;        // the oldest ridge's floor, as a fraction from the plot top
};

// Geometry shared by the painter and its contract. The age of the quarter that holds a frame
// ending at `frameSeconds` when the newest frame ends at `newestSeconds`: the time since the
// quarter ended, 0 for the quarter still filling.
double sliceAge (double frameSeconds, double newestSeconds) noexcept;
juce::Point<float> project (juce::Rectangle<float> plot, const Scale&, float depth,
                            float normalisedX, float value) noexcept;
Scale levelScale (juce::Rectangle<float> plot) noexcept; // mountains from the -96 dBFS floor

void paint (juce::Graphics&, juce::Rectangle<float> plot, const Source&, const Scale&,
            juce::Colour ink, juce::Colour floor, double seconds);

// The six seconds of POST level on the level scale. Returns false when the plot is too narrow or
// the history too short, so the caller keeps its flat six-second field.
bool paintLevelLandscape (juce::Graphics&, juce::Rectangle<float> plot,
                          const absolute_spectrum::History&);

// Instrument notes on the plot: corner marks, the definition of what is drawn, and the exact
// analysis the plot shows (aperture, FFT, bands, presentation rate), all from the snapshot.
void paintInstrumentNotes (juce::Graphics&, juce::Rectangle<float> plot, const KirinSpectrumView&,
                           bool delta, presentation::Context);
}
