#pragma once

#include <array>

#include <juce_graphics/juce_graphics.h>

#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"

namespace hypha::spectrum_magnitude_chrome
{
inline void paintAxis (juce::Graphics& g,
                       juce::Rectangle<float> plot,
                       float scale,
                       bool left,
                       bool grid)
{
    constexpr std::array<const char*, 3> labels { "0", "-48", "-96" };
    constexpr std::array<float, 3> positions { 0.0f, 0.5f, 1.0f };
    const auto scaled = [scale] (int value) {
        return juce::roundToInt ((float) value * scale);
    };
    g.setFont (monoFont (8.5f * ui_contract::analysisTextScale (scale)));
    g.setColour (COL_MUTED.withAlpha (0.86f));
    for (size_t index = 0u; index < labels.size(); ++index)
    {
        const int x = left ? 0 : juce::roundToInt (plot.getRight()) + scaled (3);
        g.drawText (labels[index], x,
                    juce::roundToInt (plot.getY() + positions[index] * plot.getHeight())
                        - scaled (5),
                    scaled (21), scaled (10),
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
