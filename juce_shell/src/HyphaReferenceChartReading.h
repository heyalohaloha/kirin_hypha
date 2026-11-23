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
    float plateHeadroom = 0.0f;  // Compact BALANCE may use its own caption row while pointed.
    bool reservePlateTop = false;  // A visible Blauert row must not be covered by the hover reading.
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

inline juce::Rectangle<int> chartReadingClip (juce::Rectangle<float> chart, float captionHeadroom) noexcept
{
    return chart.withTop (chart.getY() - captionHeadroom).toNearestInt();
}

template <typename YFor>
bool paintChartReading (juce::Graphics& g, juce::Rectangle<float> chart, double minimumHz,
                        double maximumHz, juce::Point<float> pointer,
                        const std::vector<ReadCurve>& curves, YFor&& yFor,
                        presentation::Context context, const ReadRange* range = nullptr,
                        const char* levelUnit = "")
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
        return false;
    const float hz = static_cast<float> (minimumHz * std::pow (
        maximumHz / minimumHz, (pointer.x - chart.getX()) / chart.getWidth()));
    g.setColour (COL_NORMAL.withAlpha (0.34f));
    g.drawVerticalLine (juce::roundToInt (pointer.x), chart.getY(), chart.getBottom());
    auto text = juce::String (juce::roundToInt (hz)) + " Hz";
    if (hz < 1'000.0f)
        text << " " << juce::String (spectrum_peaks::noteName (hz));
    using Part = std::pair<juce::String, juce::Colour>;
    std::vector<Part> parts { { text, COL_NORMAL.withAlpha (0.94f) } };
    std::optional<juce::String> compactA, compactB;
    std::optional<Part> target;
    for (const auto& curve : curves)
    {
        const auto reading = readAt (curve, hz, yFor);
        if (! reading)
            continue;
        g.setColour (curve.colour);
        g.fillEllipse (juce::Rectangle<float> (5.0f, 5.0f).withCentre ({ pointer.x, reading->y }));
        const auto level = juce::String (reading->level, 1);
        if (std::string_view (curve.name) == "A") compactA = level;
        if (std::string_view (curve.name) == "B") compactB = level;
        parts.push_back ({ juce::String (curve.name) + " " + level + levelUnit, curve.colour });
    }
    if (range != nullptr)
        if (const auto low = readAt (range->low, hz, yFor), high = readAt (range->high, hz, yFor); low && high)
            target = Part { "TARGET " + juce::String (low->level, 1) + " TO " + juce::String (high->level, 1) + levelUnit,
                            COL_NORMAL.withAlpha (0.86f) };
    if (target) parts.push_back (*target);
    // Normal-height readouts wrap each complete reading to the next row when necessary.
    const auto partWidth = [&g] (const Part& part) { return text_style::shownWidth (g.getCurrentFont(), part.first) + 10.0f; };
    constexpr float padding = 8.0f;
    std::vector<std::pair<float, std::vector<Part>>> rows (1, { padding, {} });
    for (const auto& part : parts)
    {
        if (! rows.back().second.empty() && rows.back().first + partWidth (part) > chart.getWidth())
            rows.push_back ({ padding, {} });
        rows.back().first += partWidth (part);
        rows.back().second.push_back (part);
    }
    const float headroom = range != nullptr ? range->plateHeadroom : 0.0f;
    constexpr float minRowHeight = 12.0f;
    const bool compactPair = headroom > 0.0f && chart.getHeight() < 40.0f && compactA && compactB
        && parts.size() == (target ? 4u : 3u) && static_cast<float> (rows.size()) * minRowHeight > chart.getHeight() + headroom;
    constexpr float compactInset = 2.0f, compactGap = 4.0f;
    const auto glyphWidth = [&g] (const Part& part)
    {
        return std::ceil (text_style::shownWidth (g.getCurrentFont(), part.first));
    };
    if (compactPair)
    {
        Part combined { "A " + *compactA + " / B " + *compactB + levelUnit, COL_NORMAL };
        const auto second = target ? *target : parts.front();
        const auto outerWidth = [&] (const Part& part) { return 2.0f * compactInset + glyphWidth (part); };
        const auto besideFrequency = [&] (const Part& part)
        {
            return outerWidth (part) + compactGap + glyphWidth (parts.front());
        };
        // Keep all readings. At the narrowest width TARGET can carry the dB unit for both rows.
        if (target && juce::jmin (besideFrequency (combined), besideFrequency (second)) > chart.getWidth())
            combined.first = "A " + *compactA + " / B " + *compactB;
        rows = { { outerWidth (combined), { combined } }, { outerWidth (second), { second } } };
        if (target)
        {
            const auto firstWidth = besideFrequency (combined), secondWidth = besideFrequency (second);
            const bool onFirst = firstWidth <= chart.getWidth() || firstWidth <= secondWidth;
            auto& [width, row] = rows[onFirst ? 0u : 1u];
            width = onFirst ? firstWidth : secondWidth;
            row.insert (row.begin(), parts.front());
        }
    }
    const auto rowCount = static_cast<float> (rows.size());
    const bool reserveBlauert = range != nullptr && range->reservePlateTop;
    const auto belowBlauert = reserveBlauert ? chart.getHeight() - range->plateTop : chart.getHeight();
    const bool compactBelowBlauert = reserveBlauert && rowCount * 16.0f - 1.0f > belowBlauert;
    const bool compactPlot = compactPair || (headroom > 0.0f && chart.getHeight() < 40.0f && rows.size() >= 3);
    const bool littleHeight = headroom > 0.0f && chart.getHeight() < 31.0f;
    const bool needsCaption = compactPlot && chart.getHeight() < rowCount * minRowHeight;
    const auto available = chart.getHeight() + (needsCaption ? headroom : 0.0f);
    const float rowHeight = compactPlot ? juce::jmax (minRowHeight, available / rowCount)
                                      : compactBelowBlauert ? minRowHeight : littleHeight ? 14.0f : 15.0f;
    const float rowGap = compactBelowBlauert || littleHeight || compactPlot ? 0.0f : 1.0f;
    const auto height = rowCount * (rowHeight + rowGap) - rowGap;
    // If the complete set of readings still does not fit below the reserved row, give Blauert
    // the compact Balance caption and use the whole plot for the readings.
    const bool blauertInCaption = reserveBlauert && height > belowBlauert;
    const float latest = juce::jmax (-headroom, chart.getHeight() - height);
    auto top = chart.getY() + (needsCaption ? chart.getHeight() - height
        : range == nullptr || blauertInCaption ? 0.0f
        : reserveBlauert ? range->plateTop : juce::jlimit (-headroom, latest, range->plateTop));
    for (const auto& [width, row] : rows)
    {
        // Rows that share a compact plot can be a fraction of a pixel tall. Text is drawn on whole
        // pixels, so each row ends where the next begins instead of rounding into it.
        const float rowTop = compactPlot ? std::round (top) : top;
        const float rowBottom = compactPlot ? std::round (top + rowHeight) : top + rowHeight;
        const juce::Rectangle<float> plate (chart.getX(), rowTop, juce::jmin (width, chart.getWidth()), rowBottom - rowTop);
        top += rowHeight + rowGap;
        g.setColour (BG.withAlpha (0.82f));
        g.fillRoundedRectangle (plate, 3.0f);
        auto cells = plate.reduced (compactPair ? compactInset : 4.0f, 0.0f);
        for (const auto& part : row)
        {
            g.setColour (part.second);
            const auto cell = cells.removeFromLeft (compactPair ? glyphWidth (part) : partWidth (part));
            text_style::drawText (g, part.first, cell.toNearestInt(), juce::Justification::centredLeft);
            if (compactPair) cells.removeFromLeft (compactGap);
        }
    }
    return blauertInCaption;
}
}
