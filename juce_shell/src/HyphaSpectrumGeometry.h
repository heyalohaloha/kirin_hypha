#pragma once

#include <cmath>

#include <juce_graphics/juce_graphics.h>

#include "HyphaSpectrumUiContract.h"
#include "HyphaUiContract.h"
#include "kirin_hypha_ffi.h"

namespace hypha::spectrum_geometry
{
    inline float visualScaleFor (juce::Rectangle<float> bounds) noexcept
    {
        return ui_contract::spectrumVisualScale (juce::roundToInt (bounds.getWidth()));
    }

    inline juce::Rectangle<float> plotBoundsFor (juce::Rectangle<float> bounds) noexcept
    {
        const float scale = visualScaleFor (bounds);
        return bounds.withTrimmedLeft ((float) ui_contract::spectrumPlotLeftInset * scale)
                     .withTrimmedRight ((float) ui_contract::spectrumPlotRightInset * scale)
                     .withTrimmedTop ((float) ui_contract::spectrumPlotTopInset * scale)
                     .withTrimmedBottom ((float) ui_contract::spectrumPlotBottomInset * scale);
    }

    inline juce::Rectangle<float> dataPlotBoundsFor (juce::Rectangle<float> bounds) noexcept
    {
        const float scale = visualScaleFor (bounds);
        auto plot = plotBoundsFor (bounds);
        if (scale > 1.1f)
            plot.removeFromTop (18.0f * scale);
        return plot;
    }

    inline juce::Rectangle<float> readoutBoundsFor (juce::Rectangle<float> plot,
                                                     float scale,
                                                     bool expanded,
                                                     bool focusLocked) noexcept
    {
        const int logicalWidth = expanded ? ui_contract::spectrumExpandedReadoutWidth
                               : focusLocked ? ui_contract::spectrumFocusReadoutWidth
                                             : ui_contract::spectrumHoverReadoutWidth;
        return { plot.getRight() - (float) logicalWidth * scale,
                 plot.getY() + (float) ui_contract::spectrumHoverReadoutInset * scale,
                 (float) logicalWidth * scale,
                 (float) ui_contract::spectrumHoverReadoutHeight * scale };
    }

    inline juce::Rectangle<float> focusClearBoundsFor (juce::Rectangle<float> readout,
                                                        float scale) noexcept
    {
        return readout.removeFromRight ((float) ui_contract::spectrumFocusClearWidth * scale);
    }

    inline juce::Rectangle<float> channelModeBoundsFor (size_t index,
                                                         juce::Rectangle<float> outerPlot,
                                                         float scale) noexcept
    {
        float x = outerPlot.getX();
        for (size_t preceding = 0; preceding < index; ++preceding)
            x += (float) (ui_contract::spectrumChannelModeWidths[preceding]
                        + ui_contract::spectrumChannelModeGap) * scale;
        return { x,
                 outerPlot.getY() + (float) ui_contract::spectrumChannelModeTop * scale,
                 (float) ui_contract::spectrumChannelModeWidths[index] * scale,
                 (float) ui_contract::spectrumChannelModeHeight * scale };
    }

    inline juce::Rectangle<float> markBoundsFor (juce::Rectangle<float> outerPlot,
                                                  float scale) noexcept
    {
        return { outerPlot.getRight() - (float) ui_contract::spectrumMarkWidth * scale,
                 outerPlot.getY() + (float) ui_contract::spectrumChannelModeTop * scale,
                 (float) ui_contract::spectrumMarkWidth * scale,
                 (float) ui_contract::spectrumChannelModeHeight * scale };
    }

    inline juce::Rectangle<float> markClearBoundsFor (juce::Rectangle<float> mark,
                                                       float scale) noexcept
    {
        return mark.removeFromRight ((float) ui_contract::spectrumMarkClearWidth * scale);
    }

    inline float yForDeltaDb (float db, juce::Rectangle<float> plot) noexcept
    {
        const float range = KIRIN_SPECTRUM_DISPLAY_RANGE_DB;
        const float clipped = juce::jlimit (-range, range, db);
        return juce::jmap (clipped, range, -range, plot.getY(), plot.getBottom());
    }

    inline float xForFrequency (float hz, float minHz, float maxHz,
                                 juce::Rectangle<float> plot) noexcept
    {
        const float clipped = juce::jlimit (minHz, maxHz, hz);
        const float position = std::log (clipped / minHz) / std::log (maxHz / minHz);
        return juce::jmap (position, 0.0f, 1.0f, plot.getX(), plot.getRight());
    }

    inline float frequencyForNormalisedX (float position, float minHz,
                                           float maxHz) noexcept
    {
        return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, position));
    }

    inline float normalisedXForFrequency (float hz, float minHz,
                                           float maxHz) noexcept
    {
        const float clipped = juce::jlimit (minHz, maxHz, hz);
        return std::log (clipped / minHz) / std::log (maxHz / minHz);
    }
}
