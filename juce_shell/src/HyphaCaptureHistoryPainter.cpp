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
    juce::Rectangle<int> loudnessLabels;
    juce::Rectangle<int> truePeakLabels;
    juce::Rectangle<int> timeLabels;
    juce::Rectangle<float> sharedPlot;
};
Layout layoutFor (juce::Rectangle<int> area)
{
    area.reduce (7, 5);
    const auto inspection = area.getWidth() >= 700;
    Layout result;
    result.legend = area.removeFromTop (inspection ? 20 : 15);
    result.loudnessLabels = area.removeFromLeft (inspection ? 54 : 40);
    result.truePeakLabels = area.removeFromRight (inspection ? 38 : 31);
    result.timeLabels = area.removeFromBottom (inspection ? 12 : 10);
    result.sharedPlot = area.reduced (2, 2).toFloat();
    return result;
}
float yForLoudness (juce::Rectangle<float> plot, double value, bool delta) noexcept
{
    const auto normalized = normalizedLoudness (value, delta);
    return plot.getBottom() - static_cast<float> (normalized) * plot.getHeight();
}

void paintCurrentLoudness (juce::Graphics& g,
                           juce::Rectangle<float> plot,
                           const std::vector<KirinMeterHistoryEntry>& history,
                           bool delta)
{
    if (history.empty())
        return;
    const auto value = history.back().lufs_m.mean;
    if (! std::isfinite (value))
        return;
    const auto y = yForLoudness (plot, value, delta);
    const auto inspection = plot.getWidth() >= 650.0f;
    const auto belowFloor = ! delta && value < absoluteLoudnessMinimum;
    const auto valueText = belowFloor
        ? juce::String ("< -36")
        : (delta && value >= 0.0 ? "+" : "") + juce::String (value, 1);
    const auto text = juce::String ("NOW  ") + valueText;
    const auto labelHeight = inspection ? 18.0f : 15.0f;
    const auto labelWidth = inspection ? 88.0f : 72.0f;
    auto label = juce::Rectangle<float> (
        plot.getRight() - labelWidth - 4.0f,
        juce::jlimit (plot.getY() + 2.0f,
                      plot.getBottom() - labelHeight - 2.0f,
                      y - labelHeight * 0.5f),
        labelWidth, labelHeight);
    g.setColour (BG.withAlpha (0.88f));
    g.fillRoundedRectangle (label, 3.0f);
    g.setColour (COL_SPECTRUM_POST.withAlpha (0.48f));
    g.drawRoundedRectangle (label, 3.0f, 0.7f);
    g.setColour (COL_OBSERVATORY_VALUE);
    g.setFont (monoFont (inspection ? 10.0f : 8.2f));
    g.drawText (text, label.toNearestInt().reduced (3, 0),
                juce::Justification::centredRight);
}
float yForTruePeak (juce::Rectangle<float> plot, double value) noexcept
{
    constexpr double minimum = -24.0;
    constexpr double maximum = 6.0;
    const auto normalized = juce::jlimit (0.0, 1.0, (value - minimum) / (maximum - minimum));
    return plot.getBottom() - static_cast<float> (normalized) * plot.getHeight();
}

juce::Rectangle<float> truePeakOverlayFor (juce::Rectangle<float> sharedPlot) noexcept
{
    const auto maximumHeight = juce::jmax (1.0f, sharedPlot.getHeight() - 8.0f);
    const auto height = juce::jlimit (juce::jmin (48.0f, maximumHeight), maximumHeight,
                                      sharedPlot.getHeight() * 0.42f);
    return sharedPlot.withTop (sharedPlot.getBottom() - height);
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
        const auto value = entry.lufs_m.mean;
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
    if (haveEndpoint)
    {
        g.setColour (COL_LED_BLUE.withAlpha (0.18f));
        g.fillEllipse (endpoint.x - 5.0f, endpoint.y - 5.0f, 10.0f, 10.0f);
        g.setColour (COL_LED_BLUE);
        g.fillEllipse (endpoint.x - 1.6f, endpoint.y - 1.6f, 3.2f, 3.2f);
    }
}

void paintTruePeakEvents (juce::Graphics& g,
                          juce::Rectangle<float> sharedPlot,
                          const std::vector<KirinMeterHistoryEntry>& history,
                          const time_history::HistoryAxis& axis,
                          const TruePeakSummary& summary,
                          double sampleRate)
{
    const auto overlay = truePeakOverlayFor (sharedPlot);
    const auto baseline = overlay.getBottom();
    if (summary.available)
    {
        for (const auto index : summary.eventIndices)
        {
            if (index >= history.size())
                continue;
            const auto value = history[index].true_peak.max;
            if (! std::isfinite (value))
                continue;
            const auto x = sharedPlot.getX()
                         + static_cast<float> (normalizedHistoryX (
                               history, axis, history[index], index, sampleRate))
                           * sharedPlot.getWidth();
            const auto y = yForTruePeak (overlay, value);
            const auto relative = juce::jlimit (
                0.0, 1.0, 1.0 - (summary.windowMaximumDbtp - value) / 12.0);
            const auto maximum = index == summary.windowMaximumIndex;
            const auto colour = maximum ? COL_FLORA_BR : COL_FLORA;
            const auto alpha = maximum ? 0.94f : static_cast<float> (0.20 + relative * 0.52);
            g.setColour (colour.withAlpha (maximum ? 0.16f : alpha * 0.10f));
            g.drawLine (x, y, x, baseline,
                        maximum ? 4.0f : 2.0f);
            g.setColour (colour.withAlpha (alpha));
            g.drawLine (x, y, x, baseline,
                        maximum ? 1.5f : 0.75f);
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
                const auto x = sharedPlot.getX()
                             + static_cast<float> (normalizedHistoryX (
                                   history, axis, history[index], index, sampleRate))
                               * sharedPlot.getWidth();
                const auto y = sharedPlot.getBottom() - 2.0f
                             - static_cast<float> (channel) * 4.0f;
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
    const auto x = layout.sharedPlot.getX()
                 + static_cast<float> (normalizedHistoryX (
                       history, axis, entry, index, sampleRate)) * layout.sharedPlot.getWidth();
    g.setColour (COL_LED_BLUE.withAlpha (0.34f));
    g.drawVerticalLine (juce::roundToInt (x), layout.sharedPlot.getY(),
                        layout.sharedPlot.getBottom());
    if (std::isfinite (entry.lufs_m.mean))
    {
        const auto y = yForLoudness (layout.sharedPlot, entry.lufs_m.mean, delta);
        g.setColour (COL_SPECTRUM_POST);
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }
    if (! delta && std::isfinite (entry.true_peak.max))
    {
        const auto y = yForTruePeak (truePeakOverlayFor (layout.sharedPlot),
                                     entry.true_peak.max);
        g.setColour (COL_FLORA_BR);
        g.fillEllipse (x - 2.0f, y - 2.0f, 4.0f, 4.0f);
    }
}
}

std::optional<std::size_t> hitTest (juce::Rectangle<int> area,
                                    const std::vector<KirinMeterHistoryEntry>& history,
                                    juce::Point<float> position,
                                    double sampleRate)
{
    if (history.empty())
        return std::nullopt;
    const auto layout = layoutFor (area);
    if (! layout.sharedPlot.contains (position))
        return std::nullopt;
    const auto axis = time_history::selectAxis (history);
    auto nearest = std::size_t { 0 };
    auto distance = std::numeric_limits<float>::max();
    for (std::size_t index = 0; index < history.size(); ++index)
    {
        const auto x = layout.sharedPlot.getX()
                     + static_cast<float> (normalizedHistoryX (
                           history, axis, history[index], index, sampleRate))
                       * layout.sharedPlot.getWidth();
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
               + "   M " + measuredText (entry.lufs_m.mean, delta);
        if (! delta)
            detail += "   TP " + measuredText (entry.true_peak.max) + " dBTP";
        if (! delta && (entry.clip_event_count[0] > 0u || entry.clip_event_count[1] > 0u))
            detail += "   CLIP L" + juce::String (entry.clip_event_count[0])
                    + " R" + juce::String (entry.clip_event_count[1]);
    }
    else if (peakSummary.available)
    {
        detail = "60 S MAX TP " + measuredText (peakSummary.windowMaximumDbtp) + " dBTP"
               + " @ " + relativeTimeText (peakSummary.secondsBeforeEnd);
    }
    else
        detail = delta ? juce::String ("M   |   60 S / 10 HZ")
                       : juce::String ("TP ") + emDash() + "   |   60 S / 10 HZ";
    if (contextFact.isNotEmpty())
        detail = contextFact + "   |   " + detail;
    g.drawFittedText (detail, layout.legend, juce::Justification::centredRight, 1, 0.70f);

    if (history.empty())
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (11.0f));
        g.drawText (juce::String ("HISTORY ") + emDash(),
                    layout.sharedPlot.toNearestInt(), juce::Justification::centred);
        return;
    }

    constexpr std::array<double, 7> absoluteTicks {
        0.0, -6.0, -12.0, -18.0, -24.0, -30.0, -36.0
    };
    constexpr std::array<double, 5> deltaTicks { 12.0, 6.0, 0.0, -6.0, -12.0 };
    g.setFont (monoFont (layout.loudnessLabels.getWidth() >= 50 ? 10.0f : 8.2f));
    const auto paintLoudnessTicks = [&] (const auto& ticks)
    {
        for (const auto tick : ticks)
        {
            const auto y = juce::roundToInt (yForLoudness (layout.sharedPlot, tick, delta));
            const bool zero = delta && tick == 0.0;
            g.setColour ((zero ? COL_FLORA_BR : COL_MUTED).withAlpha (
                zero ? 0.42f : 0.25f));
            g.drawHorizontalLine (y, layout.sharedPlot.getX(), layout.sharedPlot.getRight());
            g.setColour (COL_MUTED.brighter (0.20f).withAlpha (0.86f));
            const auto label = (delta && tick > 0.0 ? "+" : "")
                             + juce::String (tick, 0);
            g.drawText (label, layout.loudnessLabels.getX(), y - 4,
                        layout.loudnessLabels.getWidth() - 3, 8,
                        juce::Justification::centredRight);
        }
    };
    if (delta)
        paintLoudnessTicks (deltaTicks);
    else
        paintLoudnessTicks (absoluteTicks);
    if (! delta)
    {
        constexpr std::array<double, 5> truePeakTicks { 6.0, 0.0, -6.0, -12.0, -24.0 };
        const auto overlay = truePeakOverlayFor (layout.sharedPlot);
        g.setFont (monoFont (6.2f));
        g.setColour (COL_FLORA.withAlpha (0.72f));
        g.drawText ("TP", layout.truePeakLabels.getX(),
                    juce::roundToInt (overlay.getY()) - 10,
                    layout.truePeakLabels.getWidth(), 8,
                    juce::Justification::centredLeft);
        for (const auto tick : truePeakTicks)
        {
            const auto y = juce::roundToInt (yForTruePeak (overlay, tick));
            g.setColour (COL_FLORA.withAlpha (tick == 0.0 ? 0.28f : 0.16f));
            g.drawHorizontalLine (y, layout.sharedPlot.getRight() - 5.0f,
                                  layout.sharedPlot.getRight());
            g.setColour (COL_MUTED.withAlpha (0.72f));
            const auto label = tick > 0.0 ? "+" + juce::String (tick, 0)
                                         : juce::String (tick, 0);
            g.drawText (label, layout.truePeakLabels.getX(), y - 4,
                        layout.truePeakLabels.getWidth(), 8,
                        juce::Justification::centredLeft);
        }
    }

    const auto axis = time_history::selectAxis (history);
    paintPath (g, layout.sharedPlot, history, axis, delta,
               COL_SPECTRUM_POST, 0.96f, 1.20f, sampleRate);
    if (! delta)
        paintTruePeakEvents (g, layout.sharedPlot, history, axis, peakSummary, sampleRate);
    paintCurrentLoudness (g, layout.sharedPlot, history, delta);
    g.setColour (COL_MUTED.withAlpha (0.64f));
    g.setFont (monoFont (5.8f));
    g.drawText ("-60", layout.timeLabels.withWidth (24), juce::Justification::centredLeft);
    g.drawText ("-30", layout.timeLabels.withSizeKeepingCentre (30, layout.timeLabels.getHeight()),
                juce::Justification::centred);
    g.drawText ("NOW", layout.timeLabels.withLeft (layout.timeLabels.getRight() - 24),
                juce::Justification::centredRight);
    paintHover (g, layout, history, axis, hoveredIndex, delta, sampleRate);
}
}
