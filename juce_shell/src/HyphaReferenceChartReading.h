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

// The range a chart fills behind its lines, read the same way as a line. `plateTop` keeps the
// plate under what the chart prints along its top.
struct ReadRange
{
    ReadCurve low, high;
    float plateTop = 0.0f;
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
                        presentation::Context context, const ReadRange* range = nullptr)
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
    using Part = std::pair<juce::String, juce::Colour>;
    std::vector<Part> parts { { text, COL_NORMAL.withAlpha (0.94f) } };
    for (const auto& curve : curves)
    {
        const auto reading = readAt (curve, hz, yFor);
        if (! reading)
            continue;
        g.setColour (curve.colour);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ pointer.x, reading->y }));
        parts.push_back ({ juce::String (curve.name) + " " + juce::String (reading->level, 1), curve.colour });
    }
    if (range != nullptr)
        if (const auto low = readAt (range->low, hz, yFor), high = readAt (range->high, hz, yFor); low && high)
            parts.push_back ({ "TARGET " + juce::String (low->level, 1) + " TO " + juce::String (high->level, 1),
                               COL_NORMAL.withAlpha (0.86f) });
    // What does not fit a row of the chart goes to the next row: no reading is dropped.
    const auto partWidth = [&g] (const Part& part) { return text_style::shownWidth (g.getCurrentFont(), part.first) + 10.0f; };
    constexpr float padding = 8.0f, rowHeight = 15.0f, rowGap = 1.0f;
    std::vector<std::pair<float, std::vector<Part>>> rows (1, { padding, {} });
    for (const auto& part : parts)
    {
        if (! rows.back().second.empty() && rows.back().first + partWidth (part) > chart.getWidth())
            rows.push_back ({ padding, {} });
        rows.back().first += partWidth (part);
        rows.back().second.push_back (part);
    }
    const auto height = static_cast<float> (rows.size()) * (rowHeight + rowGap) - rowGap;
    auto top = chart.getY() + (range != nullptr ? juce::jlimit (0.0f, juce::jmax (0.0f, chart.getHeight() - height), range->plateTop) : 0.0f);
    for (const auto& [width, row] : rows)
    {
        const juce::Rectangle<float> plate (chart.getX(), top, juce::jmin (width, chart.getWidth()), rowHeight);
        top += rowHeight + rowGap;
        g.setColour (BG.withAlpha (0.82f));
        g.fillRoundedRectangle (plate, 3.0f);
        auto cells = plate.reduced (4.0f, 0.0f);
        for (const auto& part : row)
        {
            g.setColour (part.second);
            text_style::drawText (g, part.first, cells.removeFromLeft (partWidth (part)).toNearestInt(),
                                  juce::Justification::centredLeft);
        }
    }
}
}
