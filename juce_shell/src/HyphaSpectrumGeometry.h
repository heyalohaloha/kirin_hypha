#pragma once

#include <cmath>

#include <juce_graphics/juce_graphics.h>

#include "HyphaSpectrumUiContract.h"
#include "HyphaUiContract.h"
#include "kirin_hypha_ffi.h"

namespace hypha::spectrum_geometry
{
    inline float bandCentreNormalisedX (size_t index) noexcept
    {
        return (static_cast<float> (index) + 0.5f)
             / static_cast<float> (KIRIN_SPECTRUM_BAND_COUNT);
    }

    inline float clampToBandCentreRange (float position) noexcept
    {
        return juce::jlimit (bandCentreNormalisedX (0u),
                             bandCentreNormalisedX (KIRIN_SPECTRUM_BAND_COUNT - 1u),
                             position);
    }

    inline float bandPositionForNormalisedX (float position) noexcept
    {
        return juce::jlimit (
            0.0f, static_cast<float> (KIRIN_SPECTRUM_BAND_COUNT - 1u),
            clampToBandCentreRange (position)
                * static_cast<float> (KIRIN_SPECTRUM_BAND_COUNT) - 0.5f);
    }

    inline float visualScaleFor (juce::Rectangle<float> bounds) noexcept
    {
        return ui_contract::spectrumVisualScale (juce::roundToInt (bounds.getWidth()));
    }

    // 100% is view-only. FREQ's channel modes, M/S, PSB and MARK, and SHARP's channel modes, are
    // chosen at 125% and above: at 100% they have no bounds, so nothing paints, hits or explains
    // them, and the plot takes their rows and FREQ's right-hand absolute axis.
    inline bool viewOnly (float visualScale) noexcept { return visualScale < 1.1f; }
    constexpr float viewOnlyRightInset = 6.0f;

    inline juce::Rectangle<float> plotBoundsFor (juce::Rectangle<float> bounds) noexcept
    {
        const float scale = visualScaleFor (bounds);
        const float rightInset = viewOnly (scale) ? viewOnlyRightInset
                                                  : (float) ui_contract::spectrumPlotRightInset;
        return bounds.withTrimmedLeft ((float) ui_contract::spectrumPlotLeftInset * scale)
                     .withTrimmedRight (rightInset * scale)
                     .withTrimmedTop ((float) ui_contract::spectrumPlotTopInset * scale)
                     .withTrimmedBottom ((float) ui_contract::spectrumPlotBottomInset * scale);
    }

    inline juce::Rectangle<float> dataPlotBoundsFor (juce::Rectangle<float> bounds,
                                                    bool reserveFocusTrail = false) noexcept
    {
        const float scale = visualScaleFor (bounds);
        auto plot = plotBoundsFor (bounds);
        if (! viewOnly (scale))
            plot.removeFromTop (juce::jmax (32.0f, 30.0f * scale));
        if (reserveFocusTrail && scale > 1.1f)
        {
            plot.removeFromBottom (
                ui_contract::spectrumFocusTrailHeight (scale)
                + ui_contract::spectrumFocusTrailAxisGap * scale);
        }
        return plot;
    }

    inline juce::Rectangle<float> focusTrailBoundsFor (
        juce::Rectangle<float> bounds) noexcept
    {
        const float scale = visualScaleFor (bounds);
        auto region = plotBoundsFor (bounds);
        if (scale > 1.1f)
        {
            region.removeFromTop (18.0f * scale);
            return region.removeFromBottom (ui_contract::spectrumFocusTrailHeight (scale));
        }
        const float inset = ui_contract::spectrumFocusTrailInset * scale;
        return region.removeFromBottom (ui_contract::spectrumFocusTrailHeight (scale))
                     .reduced (inset, 0.0f);
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
                 plot.getY() + 17.0f * scale,
                 (float) logicalWidth * scale,
                 (float) ui_contract::spectrumHoverReadoutHeight * scale };
    }

    inline juce::Rectangle<float> focusClearBoundsFor (juce::Rectangle<float> readout,
                                                        float scale) noexcept
    {
        return readout.removeFromRight ((float) ui_contract::spectrumFocusClearWidth * scale);
    }

    inline juce::Rectangle<float> midSideReadoutBoundsFor (juce::Rectangle<float> plot,
                                                            float scale,
                                                            bool expanded) noexcept
    {
        const int logicalWidth = expanded ? ui_contract::spectrumExpandedReadoutWidth
                                          : ui_contract::spectrumMidSideCompactReadoutWidth;
        return { plot.getRight() - (float) logicalWidth * scale,
                 plot.getY() + 17.0f * scale,
                 (float) logicalWidth * scale,
                 (float) ui_contract::spectrumHoverReadoutHeight * scale };
    }

    inline juce::Rectangle<float> channelModeBoundsFor (size_t index,
                                                         juce::Rectangle<float> outerPlot,
                                                         float scale) noexcept
    {
        if (viewOnly (scale))
            return {};
        float x = outerPlot.getX();
        for (size_t preceding = 0; preceding < index; ++preceding)
            x += (float) (ui_contract::spectrumChannelModeWidths[preceding]
                        + ui_contract::spectrumChannelModeGap) * scale;
        return { x,
                 outerPlot.getY() + (float) ui_contract::spectrumChannelModeTop * scale,
                 (float) ui_contract::spectrumChannelModeWidths[index] * scale,
                 (float) ui_contract::spectrumChannelModeHeight * scale };
    }

    inline juce::Rectangle<float> displayModeBoundsFor (size_t index,
                                                         juce::Rectangle<float> outerPlot,
                                                         float scale) noexcept
    {
        if (viewOnly (scale))
            return {};
        float x = outerPlot.getX();
        for (size_t preceding = 0; preceding < index; ++preceding)
            x += (float) (ui_contract::spectrumDisplayModeWidths[preceding]
                        + ui_contract::spectrumChannelModeGap) * scale;
        return { x,
                 outerPlot.getY() + (float) ui_contract::spectrumChannelModeTop * scale,
                 (float) ui_contract::spectrumDisplayModeWidths[index] * scale,
                 (float) ui_contract::spectrumChannelModeHeight * scale };
    }

    inline juce::Rectangle<float> markBoundsFor (juce::Rectangle<float> outerPlot,
                                                  float scale) noexcept
    {
        if (viewOnly (scale))
            return {};
        return { outerPlot.getRight()
                    - (float) ui_contract::spectrumMarkWidth * scale,
                 outerPlot.getY() + (float) ui_contract::spectrumChannelModeTop * scale,
                 (float) ui_contract::spectrumMarkWidth * scale,
                 (float) ui_contract::spectrumChannelModeHeight * scale };
    }

    inline juce::Rectangle<float> subviewBoundsFor (juce::Rectangle<float> outerPlot,
                                                     float scale) noexcept
    {
        if (viewOnly (scale))
            return {};
        return { outerPlot.getRight()
                    - (float) (ui_contract::spectrumMarkWidth
                             + ui_contract::spectrumSubviewGap
                             + ui_contract::spectrumSubviewWidth) * scale,
                 outerPlot.getY() + (float) ui_contract::spectrumChannelModeTop * scale,
                 (float) ui_contract::spectrumSubviewWidth * scale,
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

    inline float frequencyForProbeNormalisedX (float position, float minHz,
                                                 float maxHz) noexcept
    {
        return frequencyForNormalisedX (clampToBandCentreRange (position), minHz, maxHz);
    }
}
