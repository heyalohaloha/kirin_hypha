#pragma once

#include <array>

#include <juce_graphics/juce_graphics.h>

#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

namespace hypha::spectrum_magnitude_chrome
{
inline int labelWidth (float scale, float range) noexcept
{ return juce::roundToInt ((range >= 120.0f && scale < 1.375f ? 35.0f : 21.0f) * scale); }
inline void paintAxis (juce::Graphics& g,
                       juce::Rectangle<float> plot,
                       float scale,
                       bool left,
                       bool grid,
                       presentation::Context presentation, float range = 96.0f)
{
    const std::array<juce::String, 3> labels { "0", juce::String (-range / 2, 0), juce::String (-range, 0) };
    constexpr std::array<float, 3> positions { 0.0f, 0.5f, 1.0f };
    const auto scaled = [scale] (int value) {
        return juce::roundToInt ((float) value * scale);
    };
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::visualization));
    g.setColour (COL_MUTED.withAlpha (0.86f));
    for (size_t index = 0u; index < labels.size(); ++index)
    {
        const int x = left ? 0 : juce::roundToInt (plot.getRight()) + scaled (3);
        text_style::drawText (g, labels[index], x,
                    juce::roundToInt (plot.getY() + positions[index] * plot.getHeight())
                        - scaled (5),
                    labelWidth (scale, range), scaled (10),
                    left ? juce::Justification::centredRight
                         : juce::Justification::centredLeft);
    }
    if (! grid)
        return;
    for (float proportion : { 0.25f, 0.5f, 0.75f })
    {
        g.setColour (COL_MUTED.withAlpha (0.18f));
        g.drawHorizontalLine (
            juce::roundToInt (plot.getY() + proportion * plot.getHeight()),
            plot.getX(), plot.getRight());
    }
}
}
