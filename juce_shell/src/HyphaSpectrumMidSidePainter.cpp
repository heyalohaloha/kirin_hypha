#include "HyphaSpectrumPainter.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"
#include "HyphaMonotoneCurve.h"

#include <cmath>

namespace hypha::spectrum_painter
{
namespace
{
juce::Path magnitudePath (const SpectrumBins& values, juce::Rectangle<float> plot, float range)
{
    SpectrumBins xs, ys;
    for (size_t index = 0u; index < values.size(); ++index)
    {
        const float x = juce::jmap (spectrum_geometry::bandCentreNormalisedX (index),
                                    plot.getX(), plot.getRight());
        const float dbfs = std::isfinite (values[index]) ? values[index] : -96.0f;
        const float y = juce::jmap (juce::jlimit (-range, 0.0f, dbfs),
                                    0.0f, -range, plot.getY(), plot.getBottom());
        xs[index] = x; ys[index] = y;
    }
    return monotone_curve::make (xs, ys);
}
}

void paintMidSide (juce::Graphics& g,
                   juce::Rectangle<float> plot,
                   float visualScale,
                   const SpectrumBins& mid,
                   const SpectrumBins& side, float range, float brightness)
{
    const float width = ui_contract::spectrumMidSideStrokeWidth
                      * ui_contract::spectrumStrokeScale (visualScale);
    const auto midPath = magnitudePath (mid, plot, range);
    const auto sidePath = magnitudePath (side, plot, range);
    const juce::PathStrokeType stroke (width, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded);

    g.setColour (COL_SPECTRUM_MID.withMultipliedBrightness (brightness).withAlpha (0.96f));
    g.strokePath (midPath, stroke);

    g.setColour (COL_SPECTRUM_SIDE.withMultipliedBrightness (brightness).withAlpha (0.90f));
    g.strokePath (sidePath, stroke);
}
}
