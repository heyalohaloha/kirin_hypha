#include "HyphaTimeHistoryPainter.h"
#include "HyphaTimeAxisContract.h"

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
    return plot.getX() + (float) normalizedX (axis, entry, index, count) * plot.getWidth();
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
                   bool delta)
{
    g.setColour (COL_MUTED.withAlpha (0.16f));
    g.fillRoundedRectangle (area.toFloat(), 2.0f);
    auto labelArea = area.removeFromLeft (78);
    g.setColour (colour.withAlpha (0.90f));
    g.setFont (monoFont (8.0f));
    const auto labelText = juce::String (label) + " "
                         + latestText (history, metric, delta);
    g.drawText (labelText, labelArea.reduced (2, 0), juce::Justification::centredLeft);

    const auto axisWidth = 23;
    auto axisArea = area.removeFromRight (axisWidth);
    auto plot = area.reduced (2, 2).toFloat();
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

    g.setColour (COL_MUTED.withAlpha (0.72f));
    g.setFont (monoFont (6.5f));
    const auto top = metric == Metric::plr ? (delta ? "+12" : "24")
                                            : (delta ? "+2" : "+1");
    const auto bottom = metric == Metric::plr ? (delta ? "-12" : "0")
                                               : (delta ? "-2" : "-1");
    g.drawText (top, axisArea.removeFromTop (axisArea.getHeight() / 2),
                juce::Justification::centredRight);
    g.drawText (bottom, axisArea, juce::Justification::centredRight);
}

void paintAxes (juce::Graphics& g, juce::Rectangle<float> plot, bool delta,
                bool detailedAxes, meter_context::ScaleMode scaleMode)
{
    constexpr std::array<const char*, 5> difference { "+12", "+6", "0", "-6", "-12" };
    const auto floor = meter_context::loudnessFloor (scaleMode);
    g.setFont (monoFont (8.0f));
    for (size_t index = 0u; index < difference.size(); ++index)
    {
        const float proportion = (float) index / (float) (difference.size() - 1u);
        const int y = juce::roundToInt (plot.getY() + proportion * plot.getHeight());
        const bool zero = delta && index == 2u;
        g.setColour ((zero ? COL_FLORA_BR : COL_MUTED)
                         .withAlpha (zero ? 0.42f
                                         : index == 0u || index == 4u ? 0.34f : 0.20f));
        g.drawHorizontalLine (y, plot.getX(), plot.getRight());
        if (detailedAxes)
        {
            g.setColour (COL_MUTED.withAlpha (0.78f));
            const auto loudness = juce::String (floor * (double) index / 4.0, 0);
            g.drawText (delta ? juce::String (difference[index]) : loudness,
                        juce::roundToInt (plot.getX()) - 27, y - 5,
                        23, 10, juce::Justification::centredRight);
            if (delta)
                g.drawText (difference[index], juce::roundToInt (plot.getRight()) + 4,
                            y - 5, 23, 10, juce::Justification::centredLeft);
        }
    }
    if (! detailedAxes || delta)
        return;
    constexpr std::array<double, 6> peakTicks { 6.0, 0.0, -6.0, -12.0, -18.0, -24.0 };
    for (const auto value : peakTicks)
    {
        const auto y = juce::roundToInt (
            yFor (plot, Metric::truePeak, value, false, scaleMode));
        g.setColour ((value == 0.0 ? COL_FLORA_BR : COL_MUTED).withAlpha (0.78f));
        const auto label = juce::String (value > 0.0 ? "+" : "") + juce::String (value, 0);
        g.drawText (label, juce::roundToInt (plot.getRight()) + 4, y - 5,
                    23, 10, juce::Justification::centredLeft);
    }
}

void paintMetric (juce::Graphics& g,
                  juce::Rectangle<float> plot,
                  const std::vector<KirinMeterHistoryEntry>& history,
                  const MetricVisual& visual,
                  const HistoryAxis& axis,
                  bool delta,
                  meter_context::ScaleMode scaleMode)
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
        if (entry.observation_count > 1u
            && std::isfinite (range.min) && std::isfinite (range.max))
        {
            const auto top = yFor (plot, visual.metric, range.max, delta, scaleMode);
            const auto bottom = yFor (plot, visual.metric, range.min, delta, scaleMode);
            if (top < bottom)
                ranges.addRectangle ((float) juce::roundToInt (x), top, 1.0f, bottom - top);
        }
        if (! std::isfinite (range.mean))
        {
            open = false;
            continue;
        }
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
            g.setFont (monoFont (7.5f));
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
                  bool compact)
{
    auto left = area;
    const auto range = left.removeFromRight (compact ? 94 : 154);
    const int metricWidth = compact ? 42 : 72;
    g.setFont (monoFont (compact ? 8.0f : 9.0f));
    for (const auto& visual : visuals)
    {
        auto cell = left.removeFromLeft (metricWidth);
        g.setColour (visual.colour);
        const auto text = compact ? juce::String (visual.label)
                                  : juce::String (visual.label) + " "
                                      + latestText (history, visual.metric, delta);
        g.drawText (text, cell, juce::Justification::centredLeft);
    }
    g.setColour (COL_MUTED.withAlpha (0.82f));
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
            meter_context::ScaleMode scaleMode)
{
    g.setColour (BG.withAlpha (compactMeter ? 0.96f : 0.76f));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);
    area.reduce (7, 6);
    if (history.empty())
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (12.0f));
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
    paintLegend (g, area.removeFromTop (16), history, rangeLabel,
                 visuals, axis, delta, compactMeter);
    auto plotArea = area;
    juce::Rectangle<int> plrArea;
    juce::Rectangle<int> correlationArea;
    if (! compactMeter)
    {
        const auto auxLaneHeight = juce::jlimit (30, 72, plotArea.getHeight() / 5);
        auto auxArea = plotArea.removeFromBottom (auxLaneHeight * 2 + 2);
        plrArea = auxArea.removeFromTop (auxLaneHeight);
        auxArea.removeFromTop (2);
        correlationArea = auxArea;
    }
    auto plot = plotArea.reduced (compactMeter ? 4 : 27, 2).toFloat();
    plot.removeFromBottom (3.0f);
    paintAxes (g, plot, delta, ! compactMeter, scaleMode);

    for (const auto& visual : visuals)
        paintMetric (g, plot, history, visual, axis, delta, scaleMode);
    if (! compactMeter)
    {
        paintAuxLane (g, plrArea, history, Metric::plr, "PLR", COL_GUIDE_BR,
                      axis, delta);
        paintAuxLane (g, correlationArea, history, Metric::correlation, "CORR",
                      COL_SPECTRUM_DELTA_BR, axis, delta);
    }

    if (! compactMeter)
    {
        g.setColour (COL_MUTED.withAlpha (0.62f));
        g.setFont (labelFont (7.5f));
        g.drawText (delta ? "LU" : "LUFS", juce::roundToInt (plot.getX()) - 27,
                    juce::roundToInt (plot.getBottom()) - 8, 23, 9,
                    juce::Justification::centredRight);
        g.drawText (delta ? "dB" : "dBTP", juce::roundToInt (plot.getRight()) + 4,
                    juce::roundToInt (plot.getBottom()) - 8, 23, 9,
                    juce::Justification::centredLeft);
    }
}
}
