#include "HyphaCaptureHistoryPainter.h"

#include "HyphaTheme.h"
#include "HyphaTimeAxisContract.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace hypha::capture_history
{
namespace
{
struct Layout
{
    juce::Rectangle<int> legend;
    juce::Rectangle<int> labels;
    juce::Rectangle<float> loudnessPlot;
    juce::Rectangle<float> truePeakRail;
};

Layout layoutFor (juce::Rectangle<int> area)
{
    area.reduce (7, 5);
    const auto inspection = area.getWidth() >= 700;
    Layout result;
    result.legend = area.removeFromTop (inspection ? 20 : 15);
    result.labels = area.removeFromLeft (inspection ? 32 : 25);
    auto plot = area.reduced (2, 2).toFloat();
    result.truePeakRail = plot.removeFromBottom (inspection ? 24.0f : 18.0f);
    plot.removeFromBottom (3.0f);
    result.loudnessPlot = plot;
    return result;
}

float yForLoudness (juce::Rectangle<float> plot, double value, bool delta) noexcept
{
    const auto minimum = delta ? -12.0 : -48.0;
    const auto maximum = delta ? 12.0 : 0.0;
    const auto normalized = juce::jlimit (0.0, 1.0, (value - minimum) / (maximum - minimum));
    return plot.getBottom() - static_cast<float> (normalized) * plot.getHeight();
}

float yForTruePeak (juce::Rectangle<float> rail, double value) noexcept
{
    constexpr double minimum = -24.0;
    constexpr double maximum = 0.0;
    const auto normalized = juce::jlimit (0.0, 1.0, (value - minimum) / (maximum - minimum));
    const auto baseline = rail.getBottom() - 7.0f;
    return baseline - static_cast<float> (normalized)
                    * juce::jmax (1.0f, rail.getHeight() - 9.0f);
}

double meanFor (const KirinMeterHistoryEntry& entry, bool shortTerm) noexcept
{
    return shortTerm ? entry.lufs_s.mean : entry.lufs_m.mean;
}

bool sameRun (const KirinMeterHistoryEntry& first,
              const KirinMeterHistoryEntry& second) noexcept
{
    return first.generation == second.generation && first.run_id == second.run_id;
}

double secondsBeforeEnd (const std::vector<KirinMeterHistoryEntry>& history,
                         std::size_t index,
                         double sampleRate) noexcept
{
    if (history.empty() || index >= history.size() || ! std::isfinite (sampleRate)
        || sampleRate <= 0.0)
        return 0.0;
    const auto latest = history.back().last_observed_frames;
    const auto observed = history[index].last_observed_frames;
    return observed <= latest ? static_cast<double> (latest - observed) / sampleRate : 0.0;
}

double normalizedHistoryX (const std::vector<KirinMeterHistoryEntry>& history,
                           const time_history::HistoryAxis& fallbackAxis,
                           const KirinMeterHistoryEntry& entry,
                           std::size_t index,
                           double sampleRate) noexcept
{
    if (! history.empty() && std::isfinite (sampleRate) && sampleRate > 0.0)
    {
        constexpr double windowSeconds = 60.0;
        const auto latest = history.back().last_observed_frames;
        if (entry.last_observed_frames <= latest)
        {
            const auto ageFrames = latest - entry.last_observed_frames;
            return juce::jlimit (
                0.0, 1.0, 1.0 - static_cast<double> (ageFrames)
                                     / (sampleRate * windowSeconds));
        }
    }
    return time_history::normalizedX (fallbackAxis, entry, index, history.size());
}

juce::String relativeTimeText (double seconds)
{
    return seconds < 0.05 ? "NOW" : "-" + juce::String (seconds, 1) + " S";
}

juce::String measuredText (double value, bool delta = false)
{
    if (! std::isfinite (value))
        return "---";
    return (delta && value >= 0.0 ? "+" : "") + juce::String (value, 1);
}

void paintPath (juce::Graphics& g,
                juce::Rectangle<float> plot,
                const std::vector<KirinMeterHistoryEntry>& history,
                const time_history::HistoryAxis& axis,
                bool shortTerm,
                bool delta,
                juce::Colour colour,
                float alpha,
                float width,
                double sampleRate)
{
    juce::Path path;
    bool open = false;
    bool haveEndpoint = false;
    std::uint64_t previousGeneration = 0;
    std::uint64_t previousRun = 0;
    juce::Point<float> endpoint;
    for (std::size_t index = 0; index < history.size(); ++index)
    {
        const auto& entry = history[index];
        const auto value = meanFor (entry, shortTerm);
        if (! std::isfinite (value))
        {
            open = false;
            continue;
        }
        const auto x = plot.getX()
                     + static_cast<float> (normalizedHistoryX (
                           history, axis, entry, index, sampleRate)) * plot.getWidth();
        const auto point = juce::Point<float> { x, yForLoudness (plot, value, delta) };
        const bool newRun = ! open || entry.generation != previousGeneration
                         || entry.run_id != previousRun;
        if (newRun)
            path.startNewSubPath (point);
        else
            path.lineTo (point);
        open = true;
        haveEndpoint = true;
        endpoint = point;
        previousGeneration = entry.generation;
        previousRun = entry.run_id;
    }

    g.setColour (colour.withAlpha (alpha * 0.14f));
    g.strokePath (path, juce::PathStrokeType (width * 4.0f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    g.setColour (colour.withAlpha (alpha));
    g.strokePath (path, juce::PathStrokeType (width,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
    if (haveEndpoint && ! shortTerm)
    {
        g.setColour (COL_LED_BLUE.withAlpha (0.18f));
        g.fillEllipse (endpoint.x - 5.0f, endpoint.y - 5.0f, 10.0f, 10.0f);
        g.setColour (COL_LED_BLUE);
        g.fillEllipse (endpoint.x - 1.6f, endpoint.y - 1.6f, 3.2f, 3.2f);
    }
}

void paintTruePeakEvents (juce::Graphics& g,
                          juce::Rectangle<float> rail,
                          const std::vector<KirinMeterHistoryEntry>& history,
                          const time_history::HistoryAxis& axis,
                          const TruePeakSummary& summary,
                          double sampleRate)
{
    const auto baseline = rail.getBottom() - 7.0f;
    g.setColour (COL_MUTED.withAlpha (0.28f));
    g.drawHorizontalLine (juce::roundToInt (baseline),
                          rail.getX(), rail.getRight());
    g.setFont (monoFont (5.7f));
    g.setColour (COL_MUTED.withAlpha (0.60f));
    const auto labels = juce::Rectangle<float> (
        rail.getX(), baseline, rail.getWidth(), rail.getBottom() - baseline).toNearestInt();
    g.drawText ("-60", labels.withWidth (24), juce::Justification::centredLeft);
    g.drawText ("-30", labels.withSizeKeepingCentre (30, labels.getHeight()),
                juce::Justification::centred);
    g.drawText ("NOW", labels.withLeft (labels.getRight() - 24),
                juce::Justification::centredRight);
    if (summary.available)
    {
        for (const auto index : summary.eventIndices)
        {
            if (index >= history.size())
                continue;
            const auto value = history[index].true_peak.max;
            if (! std::isfinite (value))
                continue;
            const auto x = rail.getX()
                         + static_cast<float> (normalizedHistoryX (
                               history, axis, history[index], index, sampleRate)) * rail.getWidth();
            const auto y = yForTruePeak (rail, value);
            const auto relative = juce::jlimit (
                0.0, 1.0, 1.0 - (summary.windowMaximumDbtp - value) / 12.0);
            const auto maximum = index == summary.windowMaximumIndex;
            const auto colour = maximum ? COL_FLORA_BR : COL_FLORA;
            const auto alpha = maximum ? 0.94f : static_cast<float> (0.20 + relative * 0.52);
            g.setColour (colour.withAlpha (maximum ? 0.16f : alpha * 0.10f));
            g.drawLine (x, y, x, baseline, maximum ? 4.0f : 2.0f);
            g.setColour (colour.withAlpha (alpha));
            g.drawLine (x, y, x, baseline, maximum ? 1.5f : 0.75f);
            if (maximum)
            {
                g.setColour (colour.withAlpha (0.24f));
                g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
                g.setColour (colour);
                g.fillEllipse (x - 1.7f, y - 1.7f, 3.4f, 3.4f);
            }
        }
    }
    const std::array<juce::Colour, 2> clipColours { COL_LED_BLUE, COL_SPECTRUM_POST };
    for (std::size_t index = 0; index < history.size(); ++index)
        for (std::size_t channel = 0; channel < 2; ++channel)
            if (history[index].clip_event_count[channel] > 0u)
            {
                const auto x = rail.getX()
                             + static_cast<float> (normalizedHistoryX (
                                   history, axis, history[index], index, sampleRate))
                               * rail.getWidth();
                const auto y = rail.getY() + 1.5f + static_cast<float> (channel) * 4.0f;
                g.setColour (clipColours[channel].withAlpha (0.24f));
                g.fillEllipse (x - 3.0f, y - 1.0f, 6.0f, 4.0f);
                g.setColour (clipColours[channel].withAlpha (0.94f));
                g.fillRoundedRectangle (x - 1.5f, y, 3.0f, 2.0f, 1.0f);
            }
}

void paintHover (juce::Graphics& g,
                 const Layout& layout,
                 const std::vector<KirinMeterHistoryEntry>& history,
                 const time_history::HistoryAxis& axis,
                 std::optional<std::size_t> hoveredIndex,
                 bool delta,
                 double sampleRate)
{
    if (! hoveredIndex.has_value() || *hoveredIndex >= history.size())
        return;
    const auto index = *hoveredIndex;
    const auto& entry = history[index];
    const auto x = layout.loudnessPlot.getX()
                 + static_cast<float> (normalizedHistoryX (
                       history, axis, entry, index, sampleRate)) * layout.loudnessPlot.getWidth();
    g.setColour (COL_LED_BLUE.withAlpha (0.34f));
    g.drawVerticalLine (juce::roundToInt (x), layout.loudnessPlot.getY(),
                        layout.truePeakRail.getBottom() - 7.0f);
    for (const auto fact : {
             std::pair { entry.lufs_s.mean, COL_NORMAL.withAlpha (0.58f) },
             std::pair { entry.lufs_m.mean, COL_SPECTRUM_POST } })
    {
        if (! std::isfinite (fact.first))
            continue;
        const auto y = yForLoudness (layout.loudnessPlot, fact.first, delta);
        g.setColour (fact.second);
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }
    if (! delta && std::isfinite (entry.true_peak.max))
    {
        const auto y = yForTruePeak (layout.truePeakRail, entry.true_peak.max);
        g.setColour (COL_FLORA_BR);
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }
}
}

void retainThrough (std::vector<KirinMeterHistoryEntry>& history,
                    std::uint64_t observedFrames)
{
    if (observedFrames == 0u)
    {
        history.clear();
        return;
    }
    history.erase (std::remove_if (history.begin(), history.end(), [observedFrames] (const auto& entry)
    {
        return entry.last_observed_frames > observedFrames;
    }), history.end());
}

TruePeakSummary analyseTruePeak (const std::vector<KirinMeterHistoryEntry>& history,
                                 double sampleRate)
{
    TruePeakSummary result;
    auto maximum = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < history.size(); ++index)
    {
        const auto value = history[index].true_peak.max;
        if (std::isfinite (value) && value > maximum)
        {
            maximum = value;
            result.available = true;
            result.windowMaximumDbtp = value;
            result.windowMaximumIndex = index;
        }
    }
    if (! result.available)
        return result;

    if (std::isfinite (sampleRate) && sampleRate > 0.0)
    {
        const auto bucketFrames = static_cast<std::uint64_t> (sampleRate * 2.0);
        std::optional<std::size_t> selected;
        std::uint64_t selectedBucket = 0;
        for (std::size_t index = 0; index < history.size(); ++index)
        {
            const auto value = history[index].true_peak.max;
            if (! std::isfinite (value))
                continue;
            const auto bucket = history[index].last_observed_frames
                              / juce::jmax (std::uint64_t { 1 }, bucketFrames);
            const bool newBucket = ! selected.has_value()
                                || ! sameRun (history[*selected], history[index])
                                || bucket != selectedBucket;
            if (newBucket)
            {
                if (selected.has_value())
                    result.eventIndices.push_back (*selected);
                selected = index;
                selectedBucket = bucket;
            }
            else if (value > history[*selected].true_peak.max)
                selected = index;
        }
        if (selected.has_value())
            result.eventIndices.push_back (*selected);
    }
    else
    {
        for (std::size_t index = 0; index < history.size(); ++index)
        {
            const auto value = history[index].true_peak.max;
            if (! std::isfinite (value))
                continue;
            const bool haveLeft = index > 0u && sameRun (history[index - 1u], history[index])
                               && std::isfinite (history[index - 1u].true_peak.max);
            const bool haveRight = index + 1u < history.size()
                                && sameRun (history[index], history[index + 1u])
                                && std::isfinite (history[index + 1u].true_peak.max);
            const auto left = haveLeft ? history[index - 1u].true_peak.max : value;
            const auto right = haveRight ? history[index + 1u].true_peak.max : value;
            const bool localMaximum = (haveLeft || haveRight) && value >= left && value >= right
                                   && ((! haveLeft || value > left) || (! haveRight || value > right));
            if (localMaximum || index == result.windowMaximumIndex)
                result.eventIndices.push_back (index);
        }
    }
    result.secondsBeforeEnd = secondsBeforeEnd (
        history, result.windowMaximumIndex, sampleRate);
    return result;
}

std::optional<std::size_t> hitTest (juce::Rectangle<int> area,
                                    const std::vector<KirinMeterHistoryEntry>& history,
                                    juce::Point<float> position,
                                    double sampleRate)
{
    if (history.empty())
        return std::nullopt;
    const auto layout = layoutFor (area);
    const auto interactive = layout.loudnessPlot.getUnion (layout.truePeakRail);
    if (! interactive.contains (position))
        return std::nullopt;
    const auto axis = time_history::selectAxis (history);
    auto nearest = std::size_t { 0 };
    auto distance = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < history.size(); ++index)
    {
        const auto x = layout.loudnessPlot.getX()
                     + static_cast<float> (normalizedHistoryX (
                           history, axis, history[index], index, sampleRate))
                       * layout.loudnessPlot.getWidth();
        const auto candidate = std::abs (x - position.x);
        if (candidate < distance)
        {
            distance = candidate;
            nearest = index;
        }
    }
    constexpr auto maximumHoverDistance = 8.0f;
    return distance <= maximumHoverDistance
        ? std::optional<std::size_t> { nearest }
        : std::nullopt;
}

void paint (juce::Graphics& g,
            juce::Rectangle<int> area,
            const std::vector<KirinMeterHistoryEntry>& history,
            bool delta,
            double sampleRate,
            std::optional<std::size_t> hoveredIndex,
            juce::String contextFact)
{
    g.setColour (BG.withAlpha (0.62f));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);
    const auto layout = layoutFor (area);
    const auto peakSummary = delta ? TruePeakSummary {} : analyseTruePeak (history, sampleRate);

    g.setFont (monoFont (8.0f));
    g.setColour (COL_MUTED.withAlpha (0.88f));
    g.drawText (delta ? "60 SECOND DELTA HISTORY" : "60 SECOND HISTORY",
                layout.legend, juce::Justification::centredLeft);
    juce::String detail;
    if (hoveredIndex.has_value() && *hoveredIndex < history.size())
    {
        const auto& entry = history[*hoveredIndex];
        detail = relativeTimeText (secondsBeforeEnd (history, *hoveredIndex, sampleRate))
               + "   M " + measuredText (entry.lufs_m.mean, delta)
               + "   S " + measuredText (entry.lufs_s.mean, delta);
        if (! delta)
            detail += "   TP " + measuredText (entry.true_peak.max);
        if (! delta && (entry.clip_event_count[0] > 0u || entry.clip_event_count[1] > 0u))
            detail += "   CLIP L" + juce::String (entry.clip_event_count[0])
                    + " R" + juce::String (entry.clip_event_count[1]);
    }
    else if (peakSummary.available)
    {
        detail = "WINDOW MAX TP " + measuredText (peakSummary.windowMaximumDbtp)
               + " @ " + relativeTimeText (peakSummary.secondsBeforeEnd);
    }
    else
        detail = delta ? juce::String ("M / S   |   60 S / 10 HZ")
                       : juce::String ("TP ") + emDash() + "   |   60 S / 10 HZ";
    if (contextFact.isNotEmpty())
        detail = contextFact + "   |   " + detail;
    g.drawFittedText (detail, layout.legend, juce::Justification::centredRight, 1, 0.70f);

    if (history.empty())
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (11.0f));
        g.drawText (juce::String ("HISTORY ") + emDash(), layout.loudnessPlot.getUnion (
                        layout.truePeakRail).toNearestInt(), juce::Justification::centred);
        return;
    }

    constexpr std::array<double, 5> absoluteTicks { 0.0, -12.0, -24.0, -36.0, -48.0 };
    constexpr std::array<double, 5> deltaTicks { 12.0, 6.0, 0.0, -6.0, -12.0 };
    const auto& ticks = delta ? deltaTicks : absoluteTicks;
    g.setFont (monoFont (6.5f));
    for (const auto tick : ticks)
    {
        const auto y = juce::roundToInt (yForLoudness (layout.loudnessPlot, tick, delta));
        const bool zero = delta && tick == 0.0;
        g.setColour ((zero ? COL_FLORA_BR : COL_MUTED).withAlpha (zero ? 0.38f : 0.18f));
        g.drawHorizontalLine (y, layout.loudnessPlot.getX(), layout.loudnessPlot.getRight());
        g.setColour (COL_MUTED.withAlpha (0.68f));
        const auto label = (delta && tick > 0.0 ? "+" : "") + juce::String (tick, 0);
        g.drawText (label, layout.labels.getX(), y - 4, layout.labels.getWidth() - 3, 8,
                    juce::Justification::centredRight);
    }
    if (! delta)
    {
        g.setColour (COL_MUTED.withAlpha (0.72f));
        g.drawText ("TP", layout.labels.getX(), juce::roundToInt (layout.truePeakRail.getY()),
                    layout.labels.getWidth() - 3,
                    juce::roundToInt (layout.truePeakRail.getHeight()),
                    juce::Justification::centredRight);
    }

    const auto axis = time_history::selectAxis (history);
    paintPath (g, layout.loudnessPlot, history, axis, true, delta,
               COL_NORMAL, 0.30f, 0.70f, sampleRate);
    paintPath (g, layout.loudnessPlot, history, axis, false, delta,
               COL_SPECTRUM_POST, 0.96f, 1.20f, sampleRate);
    if (! delta)
        paintTruePeakEvents (g, layout.truePeakRail, history, axis, peakSummary, sampleRate);
    paintHover (g, layout, history, axis, hoveredIndex, delta, sampleRate);
}
}
