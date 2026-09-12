#pragma once
#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumMagnitudeChrome.h"
#include "HyphaTheme.h"
#include <cmath>

namespace hypha::spectrum_axes
{
    inline juce::String axisFrequencyText (float hz)
    {
        if (hz < 1'000.0f)
            return juce::String (juce::roundToInt (hz));
        const float khz = hz / 1'000.0f;
        const float rounded = std::round (khz);
        return std::abs (khz - rounded) < 0.05f
                 ? juce::String (juce::roundToInt (rounded)) + "k"
                 : juce::String (khz, 1) + "k";
    }

    inline void paintAxes (juce::Graphics& g,
                    juce::Rectangle<float> plot,
                    float scale,
                    float minimumHz,
                    float maximumHz,
                    bool absoluteObservation,
                    presentation::Context presentation)
    {
        const auto scaled = [scale] (float value) { return value * scale; };
        const auto scaledInt = [scale] (int value) {
            return juce::roundToInt ((float) value * scale);
        };
        g.setFont (monoFont (presentation, typography::TextRole::axis,
                             typography::Composition::visualization));
        g.setColour (COL_MUTED.withAlpha (0.86f));
        if (absoluteObservation)
            spectrum_magnitude_chrome::paintAxis (g, plot, scale, true, true, presentation);
        else
        {
            const float zeroY = spectrum_geometry::yForDeltaDb (0.0f, plot);
            g.drawText ("+24", 0, juce::roundToInt (plot.getY()) - scaledInt (4),
                        scaledInt (21), scaledInt (10), juce::Justification::centredRight);
            g.drawText ("0", 0, juce::roundToInt (zeroY) - scaledInt (5),
                        scaledInt (21), scaledInt (10), juce::Justification::centredRight);
            g.drawText ("-24", 0, juce::roundToInt (plot.getBottom()) - scaledInt (6),
                        scaledInt (21), scaledInt (10), juce::Justification::centredRight);
            for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
            {
                const float y = spectrum_geometry::yForDeltaDb (db, plot);
                g.setColour (COL_MUTED.withAlpha (0.18f));
                g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
            }
            spectrum_magnitude_chrome::paintAxis (g, plot, scale, false, false, presentation);
        }
        for (float hz : { 100.0f, 1'000.0f, 10'000.0f })
        {
            if (hz <= minimumHz || hz >= maximumHz)
                continue;
            const float x = spectrum_geometry::xForFrequency (
                hz, minimumHz, maximumHz, plot);
            g.setColour (COL_MUTED.withAlpha (0.13f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }
        if (scale > 1.375f)
        {
            g.setColour (COL_MUTED.withAlpha (0.20f));
            const float tickLength = scaled (3.0f);
            for (float hz : { 20.0f, 50.0f, 200.0f, 500.0f,
                              2'000.0f, 5'000.0f, 20'000.0f })
            {
                if (hz <= minimumHz || hz >= maximumHz)
                    continue;
                const float x = spectrum_geometry::xForFrequency (
                    hz, minimumHz, maximumHz, plot);
                g.drawLine (x, plot.getY(), x, plot.getY() + tickLength, 1.0f);
                g.drawLine (x, plot.getBottom() - tickLength, x, plot.getBottom(), 1.0f);
            }
        }

        g.setColour (COL_MUTED.withAlpha (0.9f));
        g.drawText (axisFrequencyText (minimumHz), juce::roundToInt (plot.getX()),
                    juce::roundToInt (plot.getBottom()) + scaledInt (1),
                    scaledInt (30), scaledInt (10), juce::Justification::centredLeft);
        if (minimumHz < 1'000.0f && maximumHz > 1'000.0f)
        {
            const float oneKhzX = spectrum_geometry::xForFrequency (
                1'000.0f, minimumHz, maximumHz, plot);
            g.drawText ("1k", juce::roundToInt (oneKhzX) - scaledInt (15),
                        juce::roundToInt (plot.getBottom()) + scaledInt (1),
                        scaledInt (30), scaledInt (10), juce::Justification::centred);
        }
        g.drawText (axisFrequencyText (maximumHz),
                    juce::roundToInt (plot.getRight()) - scaledInt (30),
                    juce::roundToInt (plot.getBottom()) + scaledInt (1),
                    scaledInt (30), scaledInt (10), juce::Justification::centredRight);
        if (scale > 1.125f)
        {
            g.setColour (COL_MUTED.withAlpha (0.72f));
            for (float hz : { 100.0f, 10'000.0f })
            {
                if (hz <= minimumHz || hz >= maximumHz)
                    continue;
                const float x = spectrum_geometry::xForFrequency (
                    hz, minimumHz, maximumHz, plot);
                g.drawText (axisFrequencyText (hz),
                            juce::roundToInt (x) - scaledInt (15),
                            juce::roundToInt (plot.getBottom()) + scaledInt (1),
                            scaledInt (30), scaledInt (10), juce::Justification::centred);
            }
        }
    }
}
