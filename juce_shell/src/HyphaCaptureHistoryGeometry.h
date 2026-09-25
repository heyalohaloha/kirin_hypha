#pragma once

#include "HyphaTimeAxisContract.h"

#include <cmath>
#include <cstddef>
#include <vector>
#include <juce_graphics/juce_graphics.h>

namespace hypha::capture_history
{
struct Layout
{
    juce::Rectangle<int> legend;
    juce::Rectangle<int> loudnessLabels;
    juce::Rectangle<int> timeLabels;
    juce::Rectangle<float> sharedPlot;
};

inline Layout layoutFor (juce::Rectangle<int> area)
{
    area.reduce (7, 5);
    const auto inspection = area.getWidth() >= 700;
    Layout result;
    result.legend = area.removeFromTop (inspection ? 36 : 30);
    result.loudnessLabels = area.removeFromLeft (inspection ? 54 : 40);
    area.removeFromRight (6);
    result.timeLabels = area.removeFromBottom (inspection ? 16 : 14);
    result.sharedPlot = area.reduced (2, 2).toFloat();
    return result;
}

inline bool inChainBand (juce::Rectangle<int> area, juce::Point<float> point)
{
    const auto plot = layoutFor (area).sharedPlot;
    return plot.withTop (plot.getBottom() - 30.0f).contains (point);
}

inline double normalizedHistoryX (const std::vector<KirinMeterHistoryEntry>& history,
                                   const time_history::HistoryAxis& fallbackAxis,
                                   const KirinMeterHistoryEntry& entry,
                                   std::size_t index,
                                   double sampleRate) noexcept
{
    if (! history.empty() && std::isfinite (sampleRate) && sampleRate > 0.0)
    {
        const auto latest = history.back().last_observed_frames;
        if (entry.last_observed_frames <= latest)
        {
            const auto ageFrames = latest - entry.last_observed_frames;
            return juce::jlimit (
                0.0, 1.0, 1.0 - static_cast<double> (ageFrames)
                                     / (sampleRate * 60.0));
        }
    }
    return time_history::normalizedX (fallbackAxis, entry, index, history.size());
}
}
