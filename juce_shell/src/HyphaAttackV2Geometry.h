#pragma once
#include "HyphaPresentationContext.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

namespace hypha::attack_v2
{
struct LaneGeometry { juce::Rectangle<int> metric, unit, scope, reason, axis, value; };
struct Geometry
{
    juce::Rectangle<int> controls, context, caption, live, evidence, previous, next, cluster;
    juce::Rectangle<int> history, head, tail, loupe, valueArea;
    std::array<juce::Rectangle<int>, 9> bands;
    juce::Rectangle<int> view;
    std::array<LaneGeometry, 4> lanes;
    bool cards = true;
    float primary = 14, metricFont = 11, scopeFont = 11, unitFont = 11, actionFont = 11;
};
inline Geometry geometry (int width, int height, const presentation::Context& c, bool all = false)
{
    Geometry g;
    const auto index = presentation::densityIndex (c.density);
    const std::array<float, 5> primary { 14, 15, 18, 22, 29 }, label { 11, 11, 11.5f, 13, 16 }, scope { 11, 11, 12, 14, 17 };
    g.primary = primary[static_cast<std::size_t> (index)]; g.metricFont = label[static_cast<std::size_t> (index)];
    g.scopeFont = scope[static_cast<std::size_t> (index)]; g.unitFont = index >= 3 ? index == 4 ? 14 : 12 : 11;
    g.actionFont = std::array<float, 5> { 11, 12, 13, 15, 18 }[static_cast<std::size_t> (index)];
    auto body = juce::Rectangle<int> (0, 0, width, height);
    const int controlHeight = std::array<int, 5> { 0, 24, 24, 22, 32 }[static_cast<std::size_t> (index)];
    g.controls = body.removeFromTop (std::min (controlHeight, height));
    if (controlHeight > 0)
    {
        g.view = g.controls.removeFromRight (index <= 2 ? 92 : index == 3 ? 124 : 166);
        const auto chipWidth = g.controls.getWidth() / 9;
        for (std::size_t i = 0; i < 9; ++i) g.bands[i] = g.controls.withX (static_cast<int> (i) * chipWidth).withWidth (chipWidth);
    }
    const auto diagramHeight = std::array<int, 5> { 34, 44, 40, all ? 82 : 114, 160 }[static_cast<std::size_t> (index)];
    auto diagram = body.removeFromTop (std::min (diagramHeight, body.getHeight()));
    g.context = diagram.removeFromTop (index == 0 ? 14 : index == 4 ? 26 : 18);
    auto actions = g.context;
    g.evidence = actions.removeFromRight (index <= 2 ? 34 : 50);
    g.live = actions.removeFromRight (index <= 2 ? 34 : 48);
    // Cluster controls replace the caption while cycling; their height and the plot never move.
    g.caption = actions;
    auto clusterActions = actions;
    g.next = clusterActions.removeFromRight (24); g.previous = clusterActions.removeFromRight (24);
    g.cluster = clusterActions;
    g.history = diagram.reduced (2, index <= 2 ? 2 : 0);
    if (index >= 3 && ! all)
    {
        auto envelopes = g.history;
        g.history = envelopes.removeFromBottom (20);
        envelopes.removeFromBottom (2);
        g.head = envelopes.withWidth ((envelopes.getWidth() - 8) * 2 / 5);
        g.tail = envelopes.withX (g.head.getRight() + 8).withWidth (envelopes.getRight() - g.head.getRight() - 8);
    }
    if (all && index == 4)
    {
        g.loupe = g.history.withTrimmedLeft (g.history.getWidth() - 300);
        g.history = g.history.withWidth (g.history.getWidth() - 308);
    }
    g.cards = index <= 2; g.valueArea = body;
    const int laneHeight = g.cards ? body.getHeight() / 2 : body.getHeight() / 4;
    for (std::size_t i = 0; i < 4; ++i)
    {
        auto& l = g.lanes[i];
        auto cell = g.cards ? juce::Rectangle<int> (static_cast<int> (i % 2) * ((width + 4) / 2),
            body.getY() + static_cast<int> (i / 2) * laneHeight, (width - 4) / 2, laneHeight)
            : body.withY (body.getY() + static_cast<int> (i) * laneHeight).withHeight (laneHeight);
        cell = cell.withTrimmedLeft (6).withTrimmedRight (6);
        if (g.cards)
        {
            const int metricHeight = 14, scopeHeight = index == 2 ? 14 : 13;
            auto metric = cell.removeFromTop (metricHeight);
            l.unit = metric.removeFromRight (all ? 48 : 84); l.metric = metric;
            l.scope = cell.removeFromTop (scopeHeight);
            l.value = cell; l.reason = l.value.withTrimmedLeft (30);
        }
        else
        {
            auto labelArea = cell.removeFromLeft (index == 3 ? 102 : 154);
            const auto labelHeight = index == 3 ? all ? 16 : 14 : 20;
            l.metric = labelArea.withHeight (labelHeight);
            l.unit = labelArea.withY (labelArea.getY() + labelHeight).withHeight (labelHeight);
            auto scopeArea = cell.removeFromLeft (index == 3 ? 128 : 188);
            l.scope = scopeArea.withHeight (labelHeight + (index == 3 && all ? 1 : 0));
            l.reason = scopeArea.withY (scopeArea.getY() + labelHeight).withHeight (labelHeight);
            l.axis = cell.removeFromLeft (std::max (0, cell.getWidth() - (index == 3 ? 218 : 300)));
            l.value = cell;
        }
    }
    return g;
}
inline float eventX (std::int64_t sample, std::int64_t viewport, std::uint32_t rate,
                     juce::Rectangle<int> plot) noexcept
{
    return rate == 0 ? -1 : plot.getRight() - 1 - static_cast<float> (
        (static_cast<long double> (viewport) - sample) / (6.0L * rate) * (plot.getWidth() - 1));
}
}
