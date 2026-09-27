#pragma once

#include <cstddef>
#include <functional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "kirin_hypha_ffi.h"

namespace hypha::absolute_spectrum { class History; }

// FREQ landscape: the last six seconds of measured POST level as a perspective range of ridges
// behind the flat reading curve. Its front edge is the reading plot itself (the same x for every
// frequency and the same y for every level), and older spectra recede toward a horizon, shrinking
// with perspective and fading with age. Ridges are drawn from the oldest to the newest, each with a
// curtain that hides what lies behind it, so the range reads as solid (the ridge-line technique).
// A ridge is drawn only where a measured frame exists near its age, and grid lines join only
// neighbouring ridges, so a gap in the measurement stays empty. The landscape adds no value to the
// reading: the current level is still the flat curve drawn over it.
namespace hypha::spectrum_terrain
{
constexpr int ridgeCount = 24;             // one ridge every 0.26 s of the six seconds
constexpr int columnCount = 96;            // across the frequency axis
constexpr float minimumPlotWidth = 360.0f; // narrower plots keep the flat six-second field
constexpr float levelFloorDbfs = -96.0f;

struct Source
{
    size_t count = 0u;                               // chronological, oldest first
    std::function<double (size_t)> ageSeconds;       // 0 for the newest frame
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

// Geometry shared by the painter and its contract.
double ridgeAge (int ridge, double seconds) noexcept;   // oldest first
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
