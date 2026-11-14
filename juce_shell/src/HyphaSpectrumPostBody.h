#pragma once

#include <juce_graphics/juce_graphics.h>

#include "HyphaMonotoneCurve.h"
#include "HyphaTheme.h"
#include "kirin_hypha_ffi.h"

// The opt-in FILL RICH look of POST: one gradient body that thins towards the floor and one
// fine light along its upper edge. No shadow and no blur, so nothing is filtered per frame.
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
inline void fill (juce::Graphics& g, juce::Rectangle<float> plot, const Bins& x, const Bins& y, float brightness)
{
    juce::Path body;
    body.preallocateSpace (static_cast<int> (KIRIN_SPECTRUM_BAND_COUNT * 3u + 12u));
    body.startNewSubPath (x.front(), plot.getBottom());
    body.lineTo (x.front(), y.front());
    monotone_curve::append (body, x, y, 0, KIRIN_SPECTRUM_BAND_COUNT - 1, false, true);
    body.lineTo (x.back(), plot.getBottom());
    body.closeSubPath();
    g.setGradientFill (gradient (plot, brightness, true, 0.24f));
    g.fillPath (body);
}

inline void edge (juce::Graphics& g, const juce::Path& curve, float strokeScale, float brightness)
{
    g.setColour (COL_SPECTRUM_POST.interpolatedWith (juce::Colours::white, 0.62f)
                     .withMultipliedBrightness (brightness).withAlpha (0.86f));
    g.strokePath (curve, juce::PathStrokeType (0.65f * strokeScale, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}
}
