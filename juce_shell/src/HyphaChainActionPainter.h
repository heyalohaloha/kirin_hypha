#pragma once

#include <cstdint>
#include <cmath>
#include <optional>
#include <vector>

#include <juce_graphics/juce_graphics.h>

#include "HyphaCaptureHistoryPainter.h"
#include "HyphaTheme.h"
#include "kirin_hypha_chain_observation.h"

namespace hypha::chain_action
{
struct View
{
    const KirinChainSnapshot* snapshot = nullptr;
    const std::vector<KirinChainPoint>* points = nullptr;

    bool visible() const noexcept
    {
        if (snapshot == nullptr || points == nullptr
            || snapshot->version != KIRIN_CHAIN_VERSION
            || snapshot->sample_rate == 0u || snapshot->count == 0u
            || points->size() != snapshot->count)
            return false;
        return snapshot->status == KIRIN_CHAIN_ACTIVE
            || snapshot->status == KIRIN_CHAIN_HOLD;
    }
};

inline float loudnessY (juce::Rectangle<float> plot, double value) noexcept
{
    return plot.getBottom()
         - static_cast<float> (capture_history::normalizedLoudness (value, false))
             * plot.getHeight();
}

inline float truePeakY (juce::Rectangle<float> plot, double value) noexcept
{
    constexpr double minimum = -24.0;
    constexpr double maximum = 6.0;
    const auto normalized = juce::jlimit (0.0, 1.0, (value - minimum) / (maximum - minimum));
    return plot.getBottom() - static_cast<float> (normalized) * plot.getHeight();
}

inline std::optional<float> xFor (juce::Rectangle<float> plot,
                                  const KirinChainSnapshot& snapshot,
                                  const KirinChainPoint& point) noexcept
{
    if (point.post_observed > snapshot.post_observed)
        return std::nullopt;
    const auto window = static_cast<std::uint64_t> (snapshot.sample_rate) * 60u;
    const auto age = snapshot.post_observed - point.post_observed;
    if (age > window || window == 0u)
        return std::nullopt;
    const auto normalized = 1.0 - static_cast<double> (age) / static_cast<double> (window);
    return plot.getX() + static_cast<float> (normalized) * plot.getWidth();
}

// 300% LEVEL only: exact PRE and POST facts share one x coordinate. PRE is the thin blue
// trajectory; solid vertical strokes show the measured change to POST. TP strokes exist only
// when PRE or POST is strictly above -1 dBTP, and only > 0 dBTP receives the wider glow.
inline void paint (juce::Graphics& g,
                   juce::Rectangle<float> loudnessPlot,
                   juce::Rectangle<float> truePeakPlot,
                   View view)
{
    if (! view.visible())
        return;

    juce::Graphics::ScopedSaveState saved (g);
    g.reduceClipRegion (loudnessPlot.getUnion (truePeakPlot).toNearestInt());

    juce::Path preLoudness;
    juce::Path loudnessActions;
    const auto pathCoordinates = static_cast<int> (view.points->size() * 6u);
    preLoudness.preallocateSpace (pathCoordinates);
    loudnessActions.preallocateSpace (pathCoordinates);
    bool preOpen = false;
    std::uint64_t previousPreGeneration = 0u;
    std::uint64_t previousPostGeneration = 0u;
    std::uint64_t previousPreRun = 0u;
    std::uint64_t previousPostRun = 0u;
    for (const auto& point : *view.points)
    {
        const auto x = xFor (loudnessPlot, *view.snapshot, point);
        if (! x.has_value() || ! std::isfinite (point.pre_m) || ! std::isfinite (point.post_m))
        {
            preOpen = false;
            continue;
        }
        const auto pre = juce::Point<float> { *x, loudnessY (loudnessPlot, point.pre_m) };
        const auto post = juce::Point<float> { *x, loudnessY (loudnessPlot, point.post_m) };
        const bool newRun = ! preOpen || point.pre_generation != previousPreGeneration
                         || point.post_generation != previousPostGeneration
                         || point.pre_run != previousPreRun || point.post_run != previousPostRun;
        if (newRun)
            preLoudness.startNewSubPath (pre);
        else
            preLoudness.lineTo (pre);
        loudnessActions.startNewSubPath (pre);
        loudnessActions.lineTo (post);
        preOpen = true;
        previousPreGeneration = point.pre_generation;
        previousPostGeneration = point.post_generation;
        previousPreRun = point.pre_run;
        previousPostRun = point.post_run;
    }
    g.setColour (COL_LED_BLUE.withAlpha (0.10f));
    g.strokePath (loudnessActions, juce::PathStrokeType (0.65f));
    g.setColour (COL_LED_BLUE.withAlpha (0.46f));
    g.strokePath (preLoudness, juce::PathStrokeType (
        0.85f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    for (const auto& point : *view.points)
    {
        if (point.pre_severity < 2u && point.post_severity < 2u)
            continue;
        const auto x = xFor (truePeakPlot, *view.snapshot, point);
        if (! x.has_value() || ! std::isfinite (point.pre_tp) || ! std::isfinite (point.post_tp))
            continue;
        const auto preY = truePeakY (truePeakPlot, point.pre_tp);
        const auto postY = truePeakY (truePeakPlot, point.post_tp);
        const auto strong = point.pre_severity == 3u || point.post_severity == 3u;
        const auto colour = point.crossing == 2u ? COL_LED_BLUE
                          : point.crossing == 3u ? COL_FLORA_BR : COL_FLORA;
        if (strong)
        {
            g.setColour (colour.withAlpha (0.14f));
            g.drawLine (*x, preY, *x, postY, 4.0f);
        }
        g.setColour (colour.withAlpha (strong ? 0.96f : 0.72f));
        g.drawLine (*x, preY, *x, postY, strong ? 1.5f : 0.8f);
        g.drawEllipse (*x - 1.8f, preY - 1.8f, 3.6f, 3.6f, 0.75f);
        g.fillEllipse (*x - 1.6f, postY - 1.6f, 3.2f, 3.2f);
    }
}
}
