#pragma once

#include <cmath>
#include <juce_graphics/juce_graphics.h>

#include "HyphaMonotoneCurve.h"
#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumPathWorkspace.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"
#include "kirin_hypha_ffi.h"

// The opt-in FILL RICH look of POST: one gradient body that thins towards the floor and one
// fine light along its upper edge. No shadow and no blur, so nothing is filtered per frame.
namespace hypha::spectrum_painter
{
float yForMagnitude (float dbfs, juce::Rectangle<float> plot, float range) noexcept;
}
namespace hypha::spectrum_painter::post_body
{
using Bins = std::array<float, KIRIN_SPECTRUM_BAND_COUNT>;

// The established POST-alone body (0.19 at the top of the plot, 0.015 at its floor) or the rich one.
inline juce::ColourGradient gradient (juce::Rectangle<float> plot, float brightness, bool rich,
                                      float richTopAlpha = 0.46f)
{
    const auto colour = COL_SPECTRUM_POST.withMultipliedBrightness (brightness);
    juce::ColourGradient result (colour.withAlpha (rich ? richTopAlpha : 0.19f), plot.getX(), plot.getY(),
                                 colour.withAlpha (rich ? 0.02f : 0.015f), plot.getX(), plot.getBottom(), false);
    if (rich) result.addColour (0.55, colour.withAlpha (richTopAlpha * 0.38f));
    return result;
}

// The body under POST where the view had none (PRE / POST / delta). Kept light: delta stays primary.
inline void fill (juce::Graphics& g, juce::Rectangle<float> plot, const Bins& x, const Bins& y, float brightness,
                  juce::Path* reusable = nullptr)
{
    juce::Path temporary;
    auto& body = reusable != nullptr ? *reusable : temporary;
    if (reusable == nullptr)
        body.preallocateSpace (static_cast<int> (KIRIN_SPECTRUM_BAND_COUNT * 3u + 12u));
    body.startNewSubPath (x.front(), plot.getBottom());
    body.lineTo (x.front(), y.front());
    monotone_curve::append (body, x, y, 0, KIRIN_SPECTRUM_BAND_COUNT - 1, false, true);
    body.lineTo (x.back(), plot.getBottom());
    body.closeSubPath();
    g.setGradientFill (gradient (plot, brightness, true, 0.24f));
    g.fillPath (body);
}

// The slow PEAK line while playback is stopped: the curve is not drawn then, the held line is.
// `hold` is in dB with TILT applied; nothing is drawn before a value has been held.
inline void stoppedPeak (juce::Graphics& g, juce::Rectangle<float> plot, const Bins& hold, float range,
                         float visualScale, float brightness, PathWorkspace* workspace = nullptr)
{
    Bins x {}, y {};
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        if (! std::isfinite (hold[index])) return;
        x[index] = juce::jmap (spectrum_geometry::bandCentreNormalisedX (index), plot.getX(), plot.getRight());
        y[index] = yForMagnitude (hold[index], plot, range);
    }
    juce::Path temporary;
    if (workspace != nullptr) workspace->beginFrame();
    auto& line = workspace != nullptr
        ? workspace->path (0, static_cast<int> (KIRIN_SPECTRUM_BAND_COUNT * 7u)) : temporary;
    monotone_curve::append (line, x, y, 0, KIRIN_SPECTRUM_BAND_COUNT - 1, true, true);
    g.setColour (COL_FLORA_BR.withMultipliedBrightness (brightness).withAlpha (ui_contract::spectrumHoldAlpha));
    g.strokePath (line, juce::PathStrokeType (0.85f * ui_contract::spectrumStrokeScale (visualScale),
                                              juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

inline void edge (juce::Graphics& g, const juce::Path& curve, float strokeScale, float brightness)
{
    g.setColour (COL_SPECTRUM_POST.interpolatedWith (juce::Colours::white, 0.62f)
                     .withMultipliedBrightness (brightness).withAlpha (0.86f));
    g.strokePath (curve, juce::PathStrokeType (0.65f * strokeScale, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}
}
