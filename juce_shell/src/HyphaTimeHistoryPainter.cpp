#include "HyphaTimeHistoryPainter.h"

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
enum class Metric { momentary, shortTerm, truePeak };

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
    return entry.lufs_m;
}

float normalizedMagnitude (Metric metric, double value) noexcept
{
    const double floor = metric == Metric::truePeak ? -24.0 : -48.0;
    return (float) juce::jlimit (0.0, 1.0, (value - floor) / -floor);
}

float yFor (juce::Rectangle<float> plot, Metric metric, double value) noexcept
{
    return plot.getBottom() - normalizedMagnitude (metric, value) * plot.getHeight();
}

float xFor (juce::Rectangle<float> plot,
            const KirinMeterHistoryEntry& entry,
            uint64_t firstObserved,
            uint64_t observedSpan,
            size_t index,
            size_t count) noexcept
{
    if (observedSpan > 0u && entry.last_observed_frames >= firstObserved)
        return plot.getX() + (float) (entry.last_observed_frames - firstObserved)
                           / (float) observedSpan * plot.getWidth();
    return plot.getX() + (count > 1u ? (float) index / (float) (count - 1u) : 1.0f)
                       * plot.getWidth();
}

juce::String latestText (const std::vector<KirinMeterHistoryEntry>& history,
                         Metric metric)
{
    for (auto iterator = history.rbegin(); iterator != history.rend(); ++iterator)
    {
        const auto value = rangeFor (*iterator, metric).mean;
        if (std::isfinite (value))
            return juce::String (value, 1);
    }
    return "---";
}

bool hasExactDawEndpoint (const std::vector<KirinMeterHistoryEntry>& history) noexcept
{
    constexpr auto unavailable = std::numeric_limits<int64_t>::min();
    return ! history.empty()
        && std::all_of (history.begin(), history.end(), [] (const auto& entry) {
               return entry.last_timeline_endpoint_samples != unavailable;
           });
}

void paintAxes (juce::Graphics& g, juce::Rectangle<float> plot)
{
    constexpr std::array<const char*, 5> loudness { "0", "-12", "-24", "-36", "-48" };
    constexpr std::array<const char*, 5> peak { "0", "-6", "-12", "-18", "-24" };
    g.setFont (monoFont (8.0f));
    for (size_t index = 0u; index < loudness.size(); ++index)
    {
        const float proportion = (float) index / (float) (loudness.size() - 1u);
        const int y = juce::roundToInt (plot.getY() + proportion * plot.getHeight());
        g.setColour (COL_MUTED.withAlpha (index == 0u || index == 4u ? 0.34f : 0.20f));
        g.drawHorizontalLine (y, plot.getX(), plot.getRight());
        g.setColour (COL_MUTED.withAlpha (0.78f));
        g.drawText (loudness[index], juce::roundToInt (plot.getX()) - 27, y - 5,
                    23, 10, juce::Justification::centredRight);
        g.drawText (peak[index], juce::roundToInt (plot.getRight()) + 4, y - 5,
                    23, 10, juce::Justification::centredLeft);
    }
}

void paintMetric (juce::Graphics& g,
                  juce::Rectangle<float> plot,
                  const std::vector<KirinMeterHistoryEntry>& history,
                  const MetricVisual& visual,
                  uint64_t firstObserved,
                  uint64_t observedSpan)
{
    juce::Path mean;
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
        const float x = xFor (plot, entry, firstObserved, observedSpan,
                              index, history.size());
        if (entry.observation_count > 1u
            && std::isfinite (range.min) && std::isfinite (range.max))
        {
            g.setColour (visual.colour.withAlpha (0.16f));
            g.drawVerticalLine (juce::roundToInt (x),
                                yFor (plot, visual.metric, range.max),
                                yFor (plot, visual.metric, range.min));
        }
        if (! std::isfinite (range.mean))
        {
            open = false;
            continue;
        }
        const float y = yFor (plot, visual.metric, range.mean);
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
    }
}

void paintLegend (juce::Graphics& g,
                  juce::Rectangle<int> area,
                  const std::vector<KirinMeterHistoryEntry>& history,
                  const juce::String& rangeLabel,
                  const std::array<MetricVisual, 3>& visuals)
{
    const bool compact = area.getWidth() < 350;
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
                                      + latestText (history, visual.metric);
        g.drawText (text, cell, juce::Justification::centredLeft);
    }
    g.setColour (COL_MUTED.withAlpha (0.82f));
    const auto basis = hasExactDawEndpoint (history) ? "  DAW RUNS" : "  SESSION";
    g.drawText (rangeLabel + (compact ? "" : basis), range,
                juce::Justification::centredRight);
}
}

void paint (juce::Graphics& g,
            juce::Rectangle<int> area,
            const std::vector<KirinMeterHistoryEntry>& history,
            const juce::String& rangeLabel)
{
    g.setColour (BG.withAlpha (0.82f));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);
    area.reduce (7, 6);
    if (history.empty())
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (12.0f));
        g.drawText ("HISTORY —", area, juce::Justification::centred);
        return;
    }

    const std::array<MetricVisual, 3> visuals {{
        { Metric::momentary, "M", COL_SPECTRUM_POST, 4.0f, 1.35f },
        { Metric::shortTerm, "S", COL_NORMAL, 3.0f, 1.05f },
        { Metric::truePeak, "TP", COL_FLORA_BR, 2.4f, 0.9f },
    }};
    paintLegend (g, area.removeFromTop (16), history, rangeLabel, visuals);
    auto plot = area.reduced (27, 2).toFloat();
    plot.removeFromBottom (3.0f);
    paintAxes (g, plot);

    const uint64_t firstObserved = history.front().last_observed_frames;
    const uint64_t lastObserved = history.back().last_observed_frames;
    const uint64_t observedSpan = lastObserved >= firstObserved
        ? lastObserved - firstObserved : 0u;
    for (const auto& visual : visuals)
        paintMetric (g, plot, history, visual, firstObserved, observedSpan);

    g.setColour (COL_MUTED.withAlpha (0.62f));
    g.setFont (labelFont (7.5f));
    g.drawText ("LUFS", juce::roundToInt (plot.getX()) - 27,
                juce::roundToInt (plot.getBottom()) - 8, 23, 9,
                juce::Justification::centredRight);
    g.drawText ("dBTP", juce::roundToInt (plot.getRight()) + 4,
                juce::roundToInt (plot.getBottom()) - 8, 23, 9,
                juce::Justification::centredLeft);
}
}
