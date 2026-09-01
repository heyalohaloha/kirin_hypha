#include "HyphaCaptureHistoryPainter.h"

#include "HyphaTheme.h"
#include "HyphaTimeAxisContract.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace hypha::capture_history
{
namespace
{
float yFor (juce::Rectangle<float> plot, double value, bool delta) noexcept
{
    const auto minimum = delta ? -12.0 : -48.0;
    const auto maximum = delta ? 12.0 : 0.0;
    const auto normalized = juce::jlimit (0.0, 1.0, (value - minimum) / (maximum - minimum));
    return plot.getBottom() - static_cast<float> (normalized) * plot.getHeight();
}

double meanFor (const KirinMeterHistoryEntry& entry, bool shortTerm) noexcept
{
    return shortTerm ? entry.lufs_s.mean : entry.lufs_m.mean;
}

juce::String latestText (const std::vector<KirinMeterHistoryEntry>& history,
                         bool shortTerm,
                         bool delta)
{
    for (auto iterator = history.rbegin(); iterator != history.rend(); ++iterator)
    {
        const auto value = meanFor (*iterator, shortTerm);
        if (std::isfinite (value))
            return (delta && value >= 0.0 ? "+" : "") + juce::String (value, 1);
    }
    return "---";
}

void paintPath (juce::Graphics& g,
                juce::Rectangle<float> plot,
                const std::vector<KirinMeterHistoryEntry>& history,
                const time_history::HistoryAxis& axis,
                bool shortTerm,
                bool delta,
                juce::Colour colour,
                float alpha,
                float width)
{
    juce::Path path;
    bool open = false;
    bool haveEndpoint = false;
    std::uint64_t previousGeneration = 0;
    std::uint64_t previousRun = 0;
    juce::Point<float> endpoint;
    for (size_t index = 0; index < history.size(); ++index)
    {
        const auto& entry = history[index];
        const auto value = meanFor (entry, shortTerm);
        if (! std::isfinite (value))
        {
            open = false;
            continue;
        }
        const auto x = plot.getX()
                     + static_cast<float> (time_history::normalizedX (
                           axis, entry, index, history.size())) * plot.getWidth();
        const auto point = juce::Point<float> { x, yFor (plot, value, delta) };
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

void paint (juce::Graphics& g,
            juce::Rectangle<int> area,
            const std::vector<KirinMeterHistoryEntry>& history,
            bool delta,
            juce::String contextFact)
{
    g.setColour (BG.withAlpha (0.62f));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);
    area.reduce (7, 5);

    auto legend = area.removeFromTop (15);
    g.setFont (monoFont (8.0f));
    g.setColour (COL_MUTED.withAlpha (0.88f));
    g.drawText (delta ? "60 SECOND DELTA HISTORY" : "60 SECOND HISTORY",
                legend, juce::Justification::centredLeft);
    const auto latest = (contextFact.isNotEmpty() ? contextFact + "   |   " : "")
                      + "M " + latestText (history, false, delta)
                      + "   S " + latestText (history, true, delta)
                      + "   |   LAST 60 S / 10 HZ";
    g.drawFittedText (latest, legend, juce::Justification::centredRight, 1, 0.72f);
    if (history.empty())
    {
        g.setColour (COL_MUTED);
        g.setFont (monoFont (11.0f));
        g.drawText ("HISTORY —", area, juce::Justification::centred);
        return;
    }

    auto labels = area.removeFromLeft (25);
    auto plot = area.reduced (2, 2).toFloat();
    constexpr std::array<double, 5> absoluteTicks { 0.0, -12.0, -24.0, -36.0, -48.0 };
    constexpr std::array<double, 5> deltaTicks { 12.0, 6.0, 0.0, -6.0, -12.0 };
    const auto& ticks = delta ? deltaTicks : absoluteTicks;
    g.setFont (monoFont (6.5f));
    for (const auto tick : ticks)
    {
        const auto y = juce::roundToInt (yFor (plot, tick, delta));
        const bool zero = delta && tick == 0.0;
        g.setColour ((zero ? COL_FLORA_BR : COL_MUTED).withAlpha (zero ? 0.38f : 0.18f));
        g.drawHorizontalLine (y, plot.getX(), plot.getRight());
        g.setColour (COL_MUTED.withAlpha (0.68f));
        const auto label = (delta && tick > 0.0 ? "+" : "") + juce::String (tick, 0);
        g.drawText (label, labels.getX(), y - 4, labels.getWidth() - 3, 8,
                    juce::Justification::centredRight);
    }

    const auto axis = time_history::selectAxis (history);
    paintPath (g, plot, history, axis, true, delta, COL_NORMAL, 0.38f, 0.75f);
    paintPath (g, plot, history, axis, false, delta, COL_SPECTRUM_POST, 0.94f, 1.15f);
}
}
