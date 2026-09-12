#include "HyphaTimeHistoryPainter.h"
#include "HyphaTimeAxisContract.h"

#include "HyphaSurfaceMaterial.h"
#include "HyphaTheme.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace hypha::time_history
{
namespace
{
enum class Metric { momentary, shortTerm, truePeak, plr, correlation };

struct MetricVisual
{
    Metric metric;
    const char* label;
    juce::Colour colour;
    float glowWidth;
    float lineWidth;
};

const KirinMeterHistoryRange& rangeFor (const KirinMeterHistoryEntry& entry,
                                        Metric metric) noexcept
{
    if (metric == Metric::shortTerm)
        return entry.lufs_s;
    if (metric == Metric::truePeak)
        return entry.true_peak;
    if (metric == Metric::plr)
        return entry.plr;
    if (metric == Metric::correlation)
        return entry.correlation;
    return entry.lufs_m;
}

float normalizedMagnitude (Metric metric, double value, bool delta,
                           meter_context::ScaleMode scaleMode) noexcept
{
    if (delta)
        return (float) juce::jlimit (0.0, 1.0, (12.0 - value) / 24.0);
    const double floor = metric == Metric::truePeak ? -24.0
                                                    : meter_context::loudnessFloor (scaleMode);
    const double ceiling = metric == Metric::truePeak ? 6.0 : 0.0;
    return 1.0f - (float) juce::jlimit (
        0.0, 1.0, (value - floor) / (ceiling - floor));
}

float yFor (juce::Rectangle<float> plot, Metric metric, double value, bool delta,
            meter_context::ScaleMode scaleMode) noexcept
{
    return plot.getY()
         + normalizedMagnitude (metric, value, delta, scaleMode) * plot.getHeight();
}

float xFor (juce::Rectangle<float> plot,
            const KirinMeterHistoryEntry& entry,
            const HistoryAxis& axis,
            size_t index,
            size_t count) noexcept
{
    return dataXForEntry ({ plot.getX(), plot.getRight() }, entry, axis, index, count);
}

juce::String latestText (const std::vector<KirinMeterHistoryEntry>& history,
                         Metric metric,
                         bool delta)
{
    for (auto iterator = history.rbegin(); iterator != history.rend(); ++iterator)
    {
        const auto value = rangeFor (*iterator, metric).mean;
        if (std::isfinite (value))
            return ((delta || metric == Metric::correlation) && value >= 0.0 ? "+" : "")
                 + juce::String (value, metric == Metric::correlation ? 2 : 1);
    }
    return "---";
}

double latestValue (const std::vector<KirinMeterHistoryEntry>& history,
                    Metric metric) noexcept
{
    for (auto iterator = history.rbegin(); iterator != history.rend(); ++iterator)
    {
        const auto value = rangeFor (*iterator, metric).mean;
        if (std::isfinite (value)) return value;
    }
    return std::numeric_limits<double>::quiet_NaN();
}

float normalizedAux (Metric metric, double value, bool delta) noexcept
{
    const double minimum = metric == Metric::plr ? (delta ? -12.0 : 0.0)
                                                  : (delta ? -2.0 : -1.0);
    const double maximum = metric == Metric::plr ? (delta ? 12.0 : 24.0)
                                                  : (delta ? 2.0 : 1.0);
    return 1.0f - (float) juce::jlimit (0.0, 1.0, (value - minimum) / (maximum - minimum));
}

void paintAuxLane (juce::Graphics& g,
                   juce::Rectangle<int> area,
                   const std::vector<KirinMeterHistoryEntry>& history,
                   Metric metric,
                   const char* label,
                   juce::Colour colour,
                   const HistoryAxis& axis,
                   juce::Range<float> timelineX,
                   const AuxiliaryLaneGeometry* sharedGeometry,
                   bool delta,
                   presentation::Context presentation)
{
    g.setColour (COL_MUTED.withAlpha (0.16f));
    g.fillRoundedRectangle (area.toFloat(), 2.0f);
    auto labelArea = sharedGeometry != nullptr ? sharedGeometry->readout
        : area.removeFromLeft (auxLabelWidth (
            presentation, metric == Metric::plr, delta, area.getWidth()));
    g.setColour (colour.withAlpha (0.90f));
    g.setFont (monoFont (presentation, typography::TextRole::readout,
                         typography::Composition::visualization));
    const auto labelText = juce::String (label) + " "
                         + latestText (history, metric, delta) + (metric == Metric::plr ? " dB" : "");
    if (metric == Metric::plr)
    {
        g.drawText (labelText, labelArea.removeFromTop (labelArea.getHeight() / 2),
                    juce::Justification::centredLeft);
        g.setFont (monoFont (presentation, typography::TextRole::body,
                             typography::Composition::visualization));
        const auto definition = presentation.logicalWidth >= 600
            ? (delta ? "PLR / POST - PRE" : "SESSION FACT / TP MAX - LUFS-I")
            : (delta ? "PLR POST - PRE" : "TP MAX - LUFS-I");
        g.drawText (definition, labelArea,
                    juce::Justification::centredLeft);

        // PLR is a cumulative session fact and normally changes very little. Present it as a
        // restrained horizontal fact gauge instead of a misleading near-flat time trace.
        const auto value = latestValue (history, metric);
        const auto minimum = delta ? -12.0 : 0.0;
        const auto maximum = delta ? 12.0 : 24.0;
        auto gauge = area.reduced (6, juce::jmax (4, area.getHeight() / 3)).toFloat();
        const auto centreY = gauge.getCentreY();
        g.setColour (COL_MUTED.withAlpha (0.28f));
        g.drawLine (gauge.getX(), centreY, gauge.getRight(), centreY, 1.0f);
        if (std::isfinite (value))
        {
            const auto proportion = (float) juce::jlimit (
                0.0, 1.0, (value - minimum) / (maximum - minimum));
            const auto start = delta ? gauge.getCentreX() : gauge.getX();
            const auto end = gauge.getX() + proportion * gauge.getWidth();
            g.setColour (colour.withAlpha (0.82f));
            g.drawLine (start, centreY, end, centreY, 1.15f);
            g.fillEllipse (end - 2.0f, centreY - 2.0f, 4.0f, 4.0f);
        }
        return;
    }
    else
        g.drawText (labelText, labelArea.reduced (2, 0), juce::Justification::centredLeft);

    auto axisArea = sharedGeometry != nullptr ? sharedGeometry->axis
                                              : area.removeFromRight (32);
    auto plot = sharedGeometry != nullptr ? sharedGeometry->data
        : juce::Rectangle<float> (
            timelineX.getStart(), static_cast<float> (area.getY() + 2),
            timelineX.getLength(), static_cast<float> (juce::jmax (0, area.getHeight() - 4)));
    const auto zeroY = plot.getY() + normalizedAux (metric, 0.0, delta) * plot.getHeight();
    g.setColour (COL_MUTED.withAlpha (0.28f));
    g.drawHorizontalLine (juce::roundToInt (zeroY), plot.getX(), plot.getRight());

    juce::Path path;
    bool open = false;
    uint64_t previousGeneration = 0u;
    uint64_t previousRun = 0u;
    for (size_t index = 0u; index < history.size(); ++index)
    {
        const auto& entry = history[index];
        const auto value = rangeFor (entry, metric).mean;
        if (! std::isfinite (value))
        {
            open = false;
            continue;
        }
        const auto x = xFor (plot, entry, axis, index, history.size());
        const auto y = plot.getY() + normalizedAux (metric, value, delta) * plot.getHeight();
        const bool newRun = ! open || entry.generation != previousGeneration
                         || entry.run_id != previousRun;
        if (newRun) path.startNewSubPath (x, y); else path.lineTo (x, y);
        open = true;
        previousGeneration = entry.generation;
        previousRun = entry.run_id;
    }
    g.setColour (colour.withAlpha (0.88f));
    g.strokePath (path, juce::PathStrokeType (1.0f));

    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::visualization));
    const auto top = metric == Metric::plr ? (delta ? "+12" : "24")
                                            : (delta ? "+2" : "+1");
    const auto bottom = metric == Metric::plr ? (delta ? "-12" : "0")
                                               : (delta ? "-2" : "-1");
    g.drawText (top, axisArea.removeFromTop (axisArea.getHeight() / 2),
                juce::Justification::centredRight);
    g.drawText (bottom, axisArea, juce::Justification::centredRight);
}

void paintAxes (juce::Graphics& g, juce::Rectangle<float> plot, bool delta,
                bool detailedAxes, meter_context::ScaleMode scaleMode,
                presentation::Context presentation)
{
    constexpr std::array<const char*, 5> difference { "+12", "+6", "0", "-6", "-12" };
    const auto floor = meter_context::loudnessFloor (scaleMode);
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::visualization));
    for (size_t index = 0u; index < difference.size(); ++index)
    {
        if (plot.getHeight() < 100.0f && index % 2u != 0u) continue;
        const float proportion = (float) index / (float) (difference.size() - 1u);
        const int y = juce::roundToInt (plot.getY() + proportion * plot.getHeight());
        const bool zero = delta && index == 2u;
        g.setColour ((zero ? COL_FLORA_BR : COL_MUTED)
                         .withAlpha (zero ? 0.42f
                                         : index == 0u || index == 4u ? 0.34f : 0.20f));
        g.drawHorizontalLine (y, plot.getX(), plot.getRight());
        if (detailedAxes)
        {
            g.setColour (COL_TEXT_TERTIARY);
            const auto loudness = juce::String (floor * (double) index / 4.0, 0);
            g.drawText (delta ? juce::String (difference[index]) : loudness,
                        juce::roundToInt (plot.getX()) - 32, y - 7,
                        28, 14, juce::Justification::centredRight);
            if (delta)
                g.drawText (difference[index], juce::roundToInt (plot.getRight()) + 4,
                            y - 7, 28, 14, juce::Justification::centredLeft);
        }
    }
    if (! detailedAxes || delta)
        return;
    constexpr std::array<double, 6> peakTicks { 6.0, 0.0, -6.0, -12.0, -18.0, -24.0 };
    for (const auto value : peakTicks)
    {
        if (plot.getHeight() < 100.0f && std::abs (value) > 0.01 && value > -23.99) continue;
        const auto y = juce::roundToInt (
            yFor (plot, Metric::truePeak, value, false, scaleMode));
        g.setColour ((value == 0.0 ? COL_FLORA_BR : COL_MUTED).withAlpha (0.78f));
        const auto label = juce::String (value > 0.0 ? "+" : "") + juce::String (value, 0);
        g.drawText (label, juce::roundToInt (plot.getRight()) + 4, y - 7,
                    28, 14, juce::Justification::centredLeft);
    }
}

void paintMetric (juce::Graphics& g,
                  juce::Rectangle<float> plot,
                  const std::vector<KirinMeterHistoryEntry>& history,
                  const MetricVisual& visual,
                  const HistoryAxis& axis,
                  bool delta,
                 meter_context::ScaleMode scaleMode,
                 presentation::Context presentation)
{
    juce::Path mean;
    juce::Path ranges;
    bool open = false;
    uint64_t previousGeneration = 0u;
    uint64_t previousRun = 0u;
    float lastX = 0.0f;
    float lastY = 0.0f;
    bool haveLast = false;
    for (size_t index = 0u; index < history.size(); ++index)
    {
        const auto& entry = history[index];
        const auto& range = rangeFor (entry, visual.metric);
        const float x = xFor (plot, entry, axis,
                              index, history.size());
        if (! std::isfinite (range.mean))
        {
            open = false;
            continue;
        }
        if (entry.observation_count > 1u
            && std::isfinite (range.min) && std::isfinite (range.max))
        {
            const auto top = yFor (plot, visual.metric, range.max, delta, scaleMode);
            const auto bottom = yFor (plot, visual.metric, range.min, delta, scaleMode);
            if (top < bottom)
                ranges.addRectangle ((float) juce::roundToInt (x), top, 1.0f, bottom - top);
        }
        // Missing data breaks only this metric. Generation/run boundaries still break every
        // metric at the same factual endpoint without inventing a cross-metric validity rule.
        const float y = yFor (plot, visual.metric, range.mean, delta, scaleMode);
        const bool newRun = ! open || entry.generation != previousGeneration
                         || entry.run_id != previousRun;
        if (newRun)
            mean.startNewSubPath (x, y);
        else
            mean.lineTo (x, y);
        open = true;
        previousGeneration = entry.generation;
        previousRun = entry.run_id;
        lastX = x;
        lastY = y;
        haveLast = true;
    }
    // One retained path preserves every exact min/max column while avoiding dozens of separate
    // Direct2D brush/transform submissions per metric on Windows.
    g.setColour (visual.colour.withAlpha (0.16f));
    g.fillPath (ranges);
    g.setColour (visual.colour.withAlpha (0.14f));
    g.strokePath (mean, juce::PathStrokeType (visual.glowWidth,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (visual.colour.withAlpha (0.92f));
    g.strokePath (mean, juce::PathStrokeType (visual.lineWidth,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    if (haveLast)
    {
        g.setColour (visual.colour.withAlpha (0.20f));
        g.fillEllipse (lastX - 4.5f, lastY - 4.5f, 9.0f, 9.0f);
        g.setColour (visual.colour);
        g.fillEllipse (lastX - 1.7f, lastY - 1.7f, 3.4f, 3.4f);
        if (plot.getHeight() >= 100.0f && visual.metric != Metric::truePeak)
        {
            const auto offset = visual.metric == Metric::momentary ? -12 : 3;
            g.setFont (monoFont (presentation, typography::TextRole::legend,
                                 typography::Composition::visualization));
            g.drawText (visual.label, juce::roundToInt (lastX) - 22,
                        juce::roundToInt (lastY) + offset, 18, 10,
                        juce::Justification::centredRight);
        }
    }
}

void paintLegend (juce::Graphics& g,
                  juce::Rectangle<int> area,
                  const std::vector<KirinMeterHistoryEntry>& history,
                  const juce::String& rangeLabel,
                  const std::array<MetricVisual, 3>& visuals,
                  const HistoryAxis& axis,
                  bool delta,
                  bool compact,
                  presentation::Context presentation)
{
    auto left = area;
    const auto range = left.removeFromRight (legendBasisWidth (area.getWidth(), compact));
    const int metricWidth = compact ? 42 : juce::jmin (72, left.getWidth() / 3);
    g.setFont (monoFont (presentation, typography::TextRole::legend,
                         typography::Composition::visualization));
    for (const auto& visual : visuals)
    {
        auto cell = left.removeFromLeft (metricWidth);
        g.setColour (visual.colour);
        const auto text = compact ? juce::String (visual.label)
                                  : juce::String (visual.label) + " "
                                      + latestText (history, visual.metric, delta);
        g.drawText (text, cell, juce::Justification::centredLeft);
    }
    g.setColour (COL_TEXT_TERTIARY);
    const auto basis = delta
        ? juce::String ("  EXACT ") + hypha::delta() + " / " + axisLabel (axis.mode)
        : juce::String ("  ") + axisLabel (axis.mode);
    g.drawText (rangeLabel + (compact ? "" : basis), range,
                juce::Justification::centredRight);
}
}

void paint (juce::Graphics& g,
            juce::Rectangle<int> area,
            const std::vector<KirinMeterHistoryEntry>& history,
            const juce::String& rangeLabel,
            bool delta,
            bool compactMeter,
            meter_context::ScaleMode scaleMode,
            presentation::Context presentation)
{
    surface_material::paintPanel (g, area.toFloat(), compactMeter ? 0.96f : 0.76f);
    const auto geometry = makeGeometry (area, compactMeter, presentation);
    area = geometry.content;
    if (history.empty())
    {
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (monoFont (presentation, typography::TextRole::status,
                             typography::Composition::visualization));
        const auto emptyText = delta
            ? juce::String ("EXACT ") + hypha::delta() + " HISTORY " + hypha::emDash()
            : juce::String ("HISTORY ") + hypha::emDash();
        g.drawText (emptyText, area, juce::Justification::centred);
        return;
    }

    const std::array<MetricVisual, 3> visuals {{
        { Metric::momentary, "M", COL_SPECTRUM_POST, 4.0f, 1.35f },
        { Metric::shortTerm, "S", COL_NORMAL, 3.0f, 1.05f },
        { Metric::truePeak, "TP", COL_FLORA_BR, 2.4f, 0.9f },
    }};
    const auto axis = selectAxis (history);
    paintLegend (g, geometry.legend, history, rangeLabel,
                 visuals, axis, delta, compactMeter, presentation);
    paintAxes (g, geometry.mainPlot, delta,
               ! compactMeter && geometry.mainPlot.getHeight() >= 55.0f, scaleMode,
               presentation);

    for (const auto& visual : visuals)
        paintMetric (g, geometry.mainPlot, history, visual, axis, delta, scaleMode, presentation);
    if (! compactMeter)
    {
        paintAuxLane (g, geometry.plrBounds, history, Metric::plr, "PLR", COL_GUIDE_BR,
                      axis, geometry.timelineX, nullptr, delta, presentation);
        paintAuxLane (g, geometry.correlation.bounds, history, Metric::correlation, "CORR",
                      COL_SPECTRUM_DELTA_BR, axis, geometry.timelineX,
                      &geometry.correlation, delta, presentation);
    }

}
}
