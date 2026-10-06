#include "HyphaTimeHistoryLayout.h"

#include "HyphaTextStyle.h"
#include "HyphaTheme.h"
#include "HyphaTimeAxisContract.h"

#include <cmath>

namespace hypha::time_history
{
juce::String psrDefinition (bool delta)
{
    return delta ? "POST - PRE" : "PEAK - LUFS-S";
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
    const auto facts = delta ? correlation
        : text_style::requiredWidth (readoutFont, "PLR 100.0 dB", readoutStyle) + gap + correlation;
    const auto definition = text_style::requiredWidth (bodyFont, psrDefinition (delta), bodyStyle);
    PsrReadout result;
    result.value = row.removeFromLeft (juce::jmin (value, row.getWidth()));
    if (row.getWidth() >= gap + facts)
        result.facts = row.removeFromRight (facts);
    if (row.getWidth() >= 2 * gap + definition)
        result.definition = row.withTrimmedLeft (gap).withWidth (definition);
    return result;
}

int legendBasisWidth (int availableWidth, bool compact) noexcept
{
    constexpr int fullMetricWidth = 72;
    return compact ? 94 : juce::jmax (184, availableWidth - fullMetricWidth * 3);
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
