#include "HyphaSpectrumPainter.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"

#include <cmath>

namespace hypha::spectrum_painter
{
namespace
{
juce::Path magnitudePath (const SpectrumBins& values, juce::Rectangle<float> plot)
{
    juce::Path path;
    for (size_t index = 0u; index < values.size(); ++index)
    {
        const float x = juce::jmap (spectrum_geometry::bandCentreNormalisedX (index),
                                    plot.getX(), plot.getRight());
        const float dbfs = std::isfinite (values[index]) ? values[index] : -96.0f;
        const float y = juce::jmap (juce::jlimit (-96.0f, 0.0f, dbfs),
                                    0.0f, -96.0f, plot.getY(), plot.getBottom());
        if (index == 0u)
            path.startNewSubPath (x, y);
        else
            path.lineTo (x, y);
    }
    return path;
}
}

void paintMidSide (juce::Graphics& g,
                   juce::Rectangle<float> plot,
                   float visualScale,
                   const SpectrumBins& mid,
                   const SpectrumBins& side)
{
    const float width = ui_contract::spectrumMidSideStrokeWidth
                      * ui_contract::spectrumStrokeScale (visualScale);
    const auto midPath = magnitudePath (mid, plot);
    const auto sidePath = magnitudePath (side, plot);
    const juce::PathStrokeType stroke (width, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded);

    g.setColour (COL_SPECTRUM_MID.withAlpha (0.96f));
    g.strokePath (midPath, stroke);

    g.setColour (COL_SPECTRUM_SIDE.withAlpha (0.90f));
    g.strokePath (sidePath, stroke);
}
}
