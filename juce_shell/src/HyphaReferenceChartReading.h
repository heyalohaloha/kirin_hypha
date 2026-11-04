#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <utility>
#include <vector>

#include "HyphaPresentationContext.h"
#include "HyphaSpectrumPeakTags.h"
#include "HyphaSpectrumPeaks.h"
#include "HyphaSpectrumScope.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

// What the pointer reads on a Reference spectrum chart. Reference has its own analysis (a 100 ms
// window over 64 bands) and its own gain, so nothing is taken from FREQ: every value is read off
// the curve as the chart draws it, and where a curve has no band there is no number.
namespace hypha::reference_ui
{
struct ReadCurve
{
    const char* name = "";
    juce::Colour colour;
    std::vector<float> hz;     // rising
    std::vector<float> level;  // as drawn, with the gain the line carries
};

// The bands a chart really draws. `right` stands for a chart that draws its last band at the edge.
inline ReadCurve drawnCurve (const char* name, juce::Colour colour, const std::vector<double>& centers,
                             const std::vector<float>& levels, double gain, double minimumHz,
                             double maximumHz, double right = 0.0)
{
    ReadCurve curve { name, colour, {}, {} };
    for (std::size_t index = 0; index < std::min (centers.size(), levels.size()); ++index)
        if (centers[index] >= minimumHz && centers[index] <= maximumHz)
        {
            curve.hz.push_back (static_cast<float> (right > 0.0 ? std::min (centers[index], right)
                                                                : centers[index]));
            curve.level.push_back (levels[index] + static_cast<float> (gain));
        }
    return curve;
}

struct CurveReading
{
    float level = 0.0f;
    float y = 0.0f;
};

// The number comes from the two bands around `hz`; the mark sits on the line between them as
// drawn, which is straight on the screen even where a band stands outside the chart's scale.
template <typename YFor>
std::optional<CurveReading> readAt (const ReadCurve& curve, float hz, YFor&& yFor) noexcept
{
    const auto count = std::min (curve.hz.size(), curve.level.size());
    for (std::size_t index = 1u; index < count; ++index)
    {
        if (curve.hz[index] < hz)
            continue;
        const float low = curve.hz[index - 1u];
        const float from = curve.level[index - 1u];
        const float to = curve.level[index];
        if (low > hz || curve.hz[index] <= low || ! std::isfinite (from) || ! std::isfinite (to))
            return std::nullopt;
        const float position = std::log (hz / low) / std::log (curve.hz[index] / low);
        return CurveReading { from + (to - from) * position,
                              yFor (from) + (yFor (to) - yFor (from)) * position };
    }
    return std::nullopt;
}

template <typename YFor>
void paintChartReading (juce::Graphics& g, juce::Rectangle<float> chart, double minimumHz,
                        double maximumHz, juce::Point<float> pointer,
                        const std::vector<ReadCurve>& curves, YFor&& yFor,
                        presentation::Context context)
{
    const auto xFor = [=] (float hz)
    {
        return chart.getX() + chart.getWidth()
            * static_cast<float> (std::log (hz / minimumHz) / std::log (maximumHz / minimumHz));
    };
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::visualization));
    // Only your own mix is tagged. Its bands are too wide to name a note at a peak, so the tag
    // says only what they show.
    const auto mix = std::find_if (curves.begin(), curves.end(), [] (const ReadCurve& curve)
    { return std::string_view (curve.name) == "A"; });
    if (maximumHz <= spectrum_scope::lowMaximumHz && mix != curves.end())
        spectrum_peaks::paintTags (
            g, chart, 1.0f,
            spectrum_peaks::find (mix->hz.data(), mix->level.data(), std::min (mix->hz.size(), mix->level.size())),
            xFor, [&] (const spectrum_peaks::Peak& peak) { return yFor (peak.level); },
            [] (const spectrum_peaks::Peak& peak)
        { return juce::String (juce::roundToInt (peak.hz)) + " Hz " + juce::String (peak.level, 1); });
    if (! chart.contains (pointer))
        return;
    const float hz = static_cast<float> (minimumHz * std::pow (
        maximumHz / minimumHz, (pointer.x - chart.getX()) / chart.getWidth()));
    g.setColour (COL_NORMAL.withAlpha (0.34f));
    g.drawVerticalLine (juce::roundToInt (pointer.x), chart.getY(), chart.getBottom());
    auto text = juce::String (juce::roundToInt (hz)) + " Hz";
    if (hz < 1'000.0f)
        text << " " << juce::String (spectrum_peaks::noteName (hz));
    std::vector<std::pair<juce::String, juce::Colour>> parts { { text, COL_NORMAL.withAlpha (0.94f) } };
    for (const auto& curve : curves)
    {
        const auto reading = readAt (curve, hz, yFor);
        if (! reading)
            continue;
        g.setColour (curve.colour);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ pointer.x, reading->y }));
        parts.push_back ({ juce::String (curve.name) + " " + juce::String (reading->level, 1), curve.colour });
    }
    float width = 8.0f;
    for (const auto& [part, colour] : parts)
        width += text_style::shownWidth (g.getCurrentFont(), part) + 10.0f;
    auto plate = chart.withHeight (15.0f).withWidth (juce::jmin (width, chart.getWidth()));
    g.setColour (BG.withAlpha (0.82f));
    g.fillRoundedRectangle (plate, 3.0f);
    auto row = plate.reduced (4.0f, 0.0f);
    for (const auto& [part, colour] : parts)
    {
        const float partWidth = text_style::shownWidth (g.getCurrentFont(), part) + 10.0f;
        g.setColour (colour);
        text_style::drawText (g, part, row.removeFromLeft (partWidth).toNearestInt(),
                              juce::Justification::centredLeft);
    }
}
}
