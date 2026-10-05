#pragma once

#include "HyphaTypographyContract.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <cmath>

namespace hypha::reference_ui::comparison_layout
{
inline constexpr float contentInset = 5.0f, toolbarHeight = 18.0f;
inline constexpr float minimumWaveform = 24.0f, tabsHeight = 20.0f;
inline constexpr float graphInset = 3.0f, readoutHeight = 18.0f;
inline constexpr float minimumDetailWidth = 380.0f;
inline constexpr float fixedDetails = tabsHeight + graphInset * 2.0f + readoutHeight;

struct AxisLabel
{
    juce::Rectangle<float> bounds;
    float fraction = 0.5f;
};

struct Layout
{
    juce::Rectangle<float> toolbar, waveform, tabs, graph, plot, readout;
    std::array<AxisLabel, 3> axisLabels {};
    int axisLabelCount = 0;
    bool detailed() const noexcept { return !graph.isEmpty(); }
};

inline float axisLineHeight (presentation::Context context) noexcept
{
    return std::ceil (typography::resolve (context, typography::TextRole::captureMetadata,
                                         typography::Composition::visualization).lineHeight);
}

inline float minimumPlotHeight (presentation::Context context, int labelCount) noexcept
{
    return axisLineHeight (context) * (labelCount >= 3 ? 4.0f : labelCount >= 2 ? 2.0f : 1.0f);
}

inline float fullPlotHeight (presentation::Context context) noexcept
{
    return minimumPlotHeight (context, 3);
}

inline float minimumPaneHeight (presentation::Context context, int labelCount) noexcept
{
    return contentInset * 2.0f + toolbarHeight + minimumWaveform + fixedDetails
        + minimumPlotHeight (context, labelCount);
}

inline Layout forPane (juce::Rectangle<float> bounds, presentation::Context context)
{
    Layout result;
    auto area = bounds.reduced (contentInset);
    result.toolbar = area.removeFromTop (bounds.getHeight() >= 65.0f ? toolbarHeight : 0.0f);
    const float line = axisLineHeight (context);
    const bool detail = bounds.getWidth() >= minimumDetailWidth
        && bounds.getHeight() >= minimumPaneHeight (context, 1);
    float waveformHeight = juce::jmax (8.0f, area.getHeight() - 17.0f);
    if (detail)
    {
        // Reserve a real plot and its readout before accepting a proportional waveform.
        // Where three axis values fit, all three stay. A smaller pane keeps an honest A/B
        // waveform and fewer quarter-grid values, rather than squeezing overlapping text.
        const auto fullRoom = area.getHeight() - fixedDetails - fullPlotHeight (context);
        waveformHeight = fullRoom >= minimumWaveform
            ? juce::jlimit (minimumWaveform, fullRoom, area.getHeight() * 0.46f) : minimumWaveform;
    }
    result.waveform = area.removeFromTop (waveformHeight).withTrimmedLeft (15.0f);
    result.tabs = area.removeFromTop (tabsHeight);
    if (detail)
    {
        result.graph = area.reduced (15.0f, graphInset);
        result.plot = result.graph;
        result.readout = result.plot.removeFromBottom (readoutHeight);
        result.axisLabelCount = result.plot.getHeight() >= line * 4.0f ? 3
            : result.plot.getHeight() >= line * 2.0f ? 2 : 1;
        const auto width = juce::jmax (32.0f, std::ceil (typography::resolve (
            context, typography::TextRole::captureMetadata).fontHeight * 3.2f));
        for (int index = 0; index < result.axisLabelCount; ++index)
        {
            const int quarter = result.axisLabelCount == 3 ? index + 1
                : result.axisLabelCount == 2 ? index * 2 + 1 : 2;
            const auto fraction = float (quarter) * 0.25f;
            result.axisLabels[size_t (index)] = {
                { result.plot.getX(), result.plot.getY() + result.plot.getHeight() * fraction - line * 0.5f,
                  width, line }, fraction };
        }
    }
    if (bounds.getHeight() < 42.0f) result.waveform = bounds.reduced (5.0f, 1.0f);
    return result;
}
}
