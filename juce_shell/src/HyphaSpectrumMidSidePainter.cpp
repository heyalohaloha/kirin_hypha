#include "HyphaSpectrumPainter.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"
#include "HyphaMonotoneCurve.h"

#include <cmath>
#include <optional>

namespace hypha::spectrum_painter
{
namespace
{
void magnitudePath (juce::Path& path, const SpectrumBins& values, juce::Rectangle<float> plot, float range)
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
    for (size_t first = 0; first < xs.size();)
    {
        if (! std::isfinite (ys[first])) { ++first; continue; }
        auto last = first;
        while (last + 1 < xs.size() && std::isfinite (ys[last + 1])) ++last;
        monotone_curve::append (path, xs, ys, first, last);
        first = last + 1;
    }
}
}

void paintMidSide (juce::Graphics& g,
                   juce::Rectangle<float> plot,
                   float visualScale,
                   const SpectrumBins& mid,
                   const SpectrumBins& side, float range, float brightness,
                   PathWorkspace* workspace)
{
    std::optional<PathWorkspace> localWorkspace;
    if (workspace == nullptr) localWorkspace.emplace();
    auto& paths = workspace != nullptr ? *workspace : *localWorkspace;
    paths.beginFrame();
    const float width = ui_contract::spectrumMidSideStrokeWidth
                      * ui_contract::spectrumStrokeScale (visualScale);
    constexpr int capacity = static_cast<int> (KIRIN_SPECTRUM_BAND_COUNT * 7u);
    auto& midPath = paths.path (0, capacity);
    auto& sidePath = paths.path (1, capacity);
    magnitudePath (midPath, mid, plot, range); magnitudePath (sidePath, side, plot, range);
    const juce::PathStrokeType stroke (width, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded);

    g.setColour (COL_SPECTRUM_MID.withMultipliedBrightness (brightness).withAlpha (0.96f));
    g.strokePath (midPath, stroke);

    g.setColour (COL_SPECTRUM_SIDE.withMultipliedBrightness (brightness).withAlpha (0.90f));
    g.strokePath (sidePath, stroke);
}
}
