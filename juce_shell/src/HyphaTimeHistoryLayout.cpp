#include "HyphaTimeHistoryLayout.h"

#include "HyphaLevelMetricContract.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"
#include "HyphaTimeAxisContract.h"

#include <cmath>

namespace hypha::time_history
{
juce::String psrDefinition (bool delta, double peak, double shortTerm)
{
    if (delta) return "POST - PRE";
    return "= PEAK " + juce::String (peak, 1) + " - S " + juce::String (shortTerm, 1);
}

PsrReadout psrReadout (presentation::Context presentation, juce::Rectangle<int> row, bool delta)
{
    constexpr auto visualization = typography::Composition::visualization;
    const auto readoutStyle = typography::resolve (
        presentation, typography::TextRole::readout, visualization);
    const auto readoutFont = monoFont (presentation, typography::TextRole::readout, visualization);
    const auto bodyStyle = typography::resolve (presentation, typography::TextRole::body, visualization);
    const auto bodyFont = monoFont (presentation, typography::TextRole::body, visualization);
    const auto gap = juce::roundToInt (readoutFont.getHeight());
    const auto value = text_style::requiredWidth (readoutFont, "PSR -10.0 dB", readoutStyle);
    const auto correlation = text_style::requiredWidth (readoutFont, "CORR -1.00", readoutStyle);
    const auto plr = delta ? 0 : text_style::requiredWidth (readoutFont, "PLR 100.0 dB", readoutStyle);
    const auto facts = delta ? correlation : plr + gap + correlation;
    const auto definition = text_style::requiredWidth (bodyFont, psrDefinition (delta), bodyStyle);
    PsrReadout result;
    result.value = row.removeFromLeft (juce::jmin (value, row.getWidth()));
    if (row.getWidth() >= gap + facts)
    {
        result.correlation = row.removeFromRight (correlation);
        if (! delta)
            result.plr = row.withTrimmedRight (gap).removeFromRight (plr);
        row.removeFromRight (facts - correlation);
    }
    if (row.getWidth() >= 2 * gap + definition)
        result.definition = row.withTrimmedLeft (gap).withWidth (definition);
    return result;
}

int legendBasisWidth (int availableWidth, bool compact) noexcept
{
    constexpr int fullMetricWidth = 72;
    return compact ? 94 : juce::jmax (184, availableWidth - fullMetricWidth * 3);
}

std::array<juce::Rectangle<int>, 3> legendCells (juce::Rectangle<int> legend, bool compact) noexcept
{
    legend.removeFromRight (legendBasisWidth (legend.getWidth(), compact));
    const int width = compact ? 42 : juce::jmin (72, legend.getWidth() / 3);
    std::array<juce::Rectangle<int>, 3> cells;
    for (auto& cell : cells)
        cell = legend.removeFromLeft (width);
    return cells;
}

namespace
{
// TIME's own helps: what it is and how it is used, never whether a value is good (R-22). The
// legend's M, S and TP and the PSR and PLR numbers say what LEVEL says of the same values.
constexpr const char* psrHistoryHelp = "PSR over time: the 400 ms peak minus the 3 s loudness. It falls where "
                                       "the song is held down; compare verse and chorus.";
constexpr const char* psrDifferenceHelp = "PSR over time: below 0 where the chain reduced the dynamics at that "
                                          "moment of the song.";
constexpr const char* psrDefinitionHelp = "PSR is PEAK - S of one 100 ms point: the 400 ms sample peak and the "
                                          "3 s loudness. Read which one moved.";
constexpr const char* correlationHelp = "Correlation (CORR) over 3 s: +1 alike, 0 unrelated, below 0 more SIDE "
                                        "than MID. A lilac tick marks each fall below 0; check it in mono.";
constexpr const char* correlationDifferenceHelp = "Correlation (CORR) over 3 s: +1 alike, 0 unrelated, below 0 "
                                                  "more SIDE than MID.";
}

juce::String helpAt (juce::Rectangle<int> area, bool delta, bool statusRow, bool compact, bool momentary,
                     presentation::Context presentation, juce::Point<int> point)
{
    if (statusRow)
        area.removeFromTop (statusRowHeight (compact));
    const auto geometry = makeGeometry (area, compact, presentation);
    using level_metrics::Metric;
    constexpr Metric legend[] { Metric::momentary, Metric::shortTerm, Metric::truePeak };
    const auto cells = legendCells (geometry.legend, compact);
    for (std::size_t shown = momentary ? 0u : 1u, cell = 0u; shown < 3u; ++shown, ++cell)
        if (cells[cell].contains (point))
            return level_metrics::scopeHelp (legend[shown]);
    if (compact || ! geometry.psr.bounds.contains (point))
        return {};
    const auto row = psrReadout (presentation, geometry.psr.readout, delta);
    if (row.value.contains (point))
        return level_metrics::scopeHelp (Metric::psr);
    if (row.plr.contains (point))
        return level_metrics::scopeHelp (Metric::plr);
    if (row.correlation.contains (point))
        return delta ? correlationDifferenceHelp : correlationHelp;
    if (! delta && row.definition.contains (point))
        return psrDefinitionHelp;
    return delta ? psrDifferenceHelp : psrHistoryHelp; // the rest of the lane is the trace
}

juce::Range<float> dataXRange (juce::Rectangle<int> area, bool compactMeter) noexcept
{
    const auto inset = compactMeter ? 4 : 32;
    const auto reduced = area.reduced (inset, 0).toFloat();
    return { reduced.getX(), reduced.getRight() };
}

float dataXForEntry (juce::Range<float> range,
                     const KirinMeterHistoryEntry& entry,
                     const HistoryAxis& axis,
                     std::size_t index,
                     std::size_t count) noexcept
{
    return range.getStart()
         + static_cast<float> (normalizedX (axis, entry, index, count)) * range.getLength();
}

Geometry makeGeometry (juce::Rectangle<int> outer, bool compactMeter,
                       presentation::Context presentation)
{
    Geometry result;
    result.content = outer.reduced (7, 6);
    auto remaining = result.content;
    result.legend = remaining.removeFromTop (16);
    result.mainBounds = remaining;

    const auto readoutStyle = typography::resolve (
        presentation, typography::TextRole::readout, typography::Composition::visualization);
    const auto readoutHeight = text_style::requiredLineHeight (readoutStyle, 12);
    if (! compactMeter)
    {
        // One PSR lane in the room of the two session-fact lanes (PLR, CORR) it replaced. PSR
        // moves with the music, so its trace gets a quarter of the page under one row of numbers,
        // never less than 24 px; PLR and CORR stay as numbers in that row.
        const auto laneHeight = juce::jlimit (readoutHeight + 24, readoutHeight + 60,
                                              remaining.getHeight() / 4);
        result.psr.bounds = result.mainBounds.removeFromBottom (laneHeight);
        result.mainBounds.removeFromBottom (2);
    }

    result.timelineX = dataXRange (result.mainBounds, compactMeter);
    result.mainPlot = {
        result.timelineX.getStart(), static_cast<float> (result.mainBounds.getY() + 7),
        result.timelineX.getLength(),
        static_cast<float> (juce::jmax (0, result.mainBounds.getHeight() - 14))
    };
    result.mainPlot.removeFromBottom (3.0f);

    if (! compactMeter)
    {
        auto lane = result.psr.bounds;
        result.psr.readout = lane.removeFromTop (readoutHeight).reduced (4, 0);
        result.psr.axis = lane.removeFromRight (32);
        result.psr.data = {
            result.timelineX.getStart(), static_cast<float> (lane.getY() + 1),
            result.timelineX.getLength(),
            static_cast<float> (juce::jmax (0, lane.getHeight() - 2))
        };
    }
    return result;
}
}
