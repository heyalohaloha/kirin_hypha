#include "HyphaTimeHistoryLayout.h"

#include "HyphaTextStyle.h"
#include "HyphaTheme.h"
#include "HyphaTimeAxisContract.h"

#include <cmath>

namespace hypha::time_history
{
int auxLabelWidth (presentation::Context presentation, bool plr, bool delta,
                   int availableWidth)
{
    const auto readoutStyle = typography::resolve (
        presentation, typography::TextRole::readout,
        typography::Composition::visualization);
    const auto readoutFont = monoFont (presentation, typography::TextRole::readout,
                                       typography::Composition::visualization);
    auto required = text_style::requiredWidth (
        readoutFont, plr ? "PLR -100.0 dB" : "CORR +1.00", readoutStyle);
    if (plr)
    {
        const auto bodyStyle = typography::resolve (
            presentation, typography::TextRole::body,
            typography::Composition::visualization);
        const auto bodyFont = monoFont (presentation, typography::TextRole::body,
                                        typography::Composition::visualization);
        const auto definition = presentation.logicalWidth >= 600
            ? (delta ? "PLR / POST - PRE" : "SESSION FACT / TP MAX - LUFS-I")
            : (delta ? "PLR POST - PRE" : "TP MAX - LUFS-I");
        required = juce::jmax (required, text_style::requiredWidth (
            bodyFont, definition, bodyStyle));
    }
    return juce::jmin (required, availableWidth / 2);
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

    if (! compactMeter)
    {
        const auto laneHeight = remaining.getHeight() < 160
            ? 24 : juce::jlimit (30, 72, remaining.getHeight() / 5);
        auto auxiliary = result.mainBounds.removeFromBottom (laneHeight * 2 + 2);
        result.plrBounds = auxiliary.removeFromTop (laneHeight);
        auxiliary.removeFromTop (2);
        result.correlation.bounds = auxiliary;
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
        auto lane = result.correlation.bounds;
        const auto style = typography::resolve (
            presentation, typography::TextRole::readout,
            typography::Composition::visualization);
        const auto readoutHeight = juce::jmin (
            lane.getHeight() / 2, text_style::requiredLineHeight (style, 12));
        result.correlation.readout = lane.removeFromTop (readoutHeight)
                                              .removeFromLeft (auxLabelWidth (
                                                  presentation, false, false,
                                                  result.correlation.bounds.getWidth()));
        result.correlation.axis = lane.removeFromRight (32);
        result.correlation.data = {
            result.timelineX.getStart(), static_cast<float> (lane.getY() + 1),
            result.timelineX.getLength(),
            static_cast<float> (juce::jmax (0, lane.getHeight() - 2))
        };
    }
    return result;
}
}
