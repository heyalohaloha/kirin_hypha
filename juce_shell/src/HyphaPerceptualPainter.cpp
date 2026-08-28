#include "HyphaPerceptualPainter.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaTheme.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace hypha::perceptual_painter
{
namespace
{
    constexpr double displayRangeAcum = 2.0;

    juce::String channelModeText (uint8_t mode)
    {
        if (mode == KIRIN_SPECTRUM_CHANNEL_MID) return "MID";
        if (mode == KIRIN_SPECTRUM_CHANNEL_SIDE) return "SIDE";
        return "LR";
    }

    juce::String statusText (uint8_t status)
    {
        if (status == KIRIN_SPECTRUM_NO_PAIR) return juce::CharPointer_UTF8 ("PAIR —");
        if (status == KIRIN_SPECTRUM_WARMING_UP) return juce::CharPointer_UTF8 ("SYNC ◌");
        if (status == KIRIN_SPECTRUM_UNAVAILABLE) return juce::CharPointer_UTF8 ("DATA —");
        if (status == KIRIN_SPECTRUM_IN_USE) return juce::CharPointer_UTF8 ("ANALYSIS — IN USE");
        return {};
    }

    juce::Rectangle<float> historyPlot (juce::Rectangle<float> outer, float scale)
    {
        outer.removeFromTop ((scale > 1.1f ? 27.0f : 17.0f) * scale);
        outer.removeFromBottom (10.0f * scale);
        return outer;
    }

    float yForValue (double value, juce::Rectangle<float> plot)
    {
        const auto clipped = juce::jlimit (-displayRangeAcum, displayRangeAcum, value);
        return juce::jmap (static_cast<float> (clipped),
                           static_cast<float> (displayRangeAcum),
                           static_cast<float> (-displayRangeAcum),
                           plot.getY(), plot.getBottom());
    }

    void paintMode (juce::Graphics& g,
                    juce::Rectangle<float> outer,
                    float scale,
                    const PaintState& state)
    {
        g.setFont (monoFont (7.5f * scale));
        if (state.actionNotice.isNotEmpty())
        {
            g.setColour (COL_MUTED.withAlpha (0.90f));
            g.drawText (state.actionNotice,
                        outer.removeFromTop (13.0f * scale),
                        juce::Justification::centredLeft);
            return;
        }
        for (size_t index = 0; index < ui_contract::spectrumChannelModeWidths.size(); ++index)
        {
            const auto mode = static_cast<uint8_t> (index);
            const auto segment = spectrum_geometry::channelModeBoundsFor (index, outer, scale);
            const bool selected = mode == state.channelMode;
            const bool unavailable = mode == KIRIN_SPECTRUM_CHANNEL_SIDE
                                  && state.inputChannels == 1u;
            if (selected)
            {
                g.setColour (BG.brighter (0.14f).withAlpha (0.92f));
                g.fillRoundedRectangle (segment, 3.0f * scale);
                g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.62f));
                g.drawRoundedRectangle (segment, 3.0f * scale, 0.65f * scale);
            }
            g.setColour (unavailable ? COL_MUTED.withAlpha (0.30f)
                                     : selected ? COL_SPECTRUM_DELTA_BR.withAlpha (0.98f)
                                                : COL_MUTED.withAlpha (0.78f));
            g.drawText (channelModeText (mode), segment.toNearestInt(),
                        juce::Justification::centred);
        }
    }

    void paintHeader (juce::Graphics& g,
                      juce::Rectangle<float> outer,
                      float scale,
                      const PaintState& state)
    {
        const float left = outer.getX() + 84.0f * scale;
        const float right = outer.getRight();
        g.setFont (monoFont (8.0f * scale));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.96f));
        g.drawText (juce::CharPointer_UTF8 ("Δ SHARPNESS"),
                    juce::Rectangle<float> (left, outer.getY(), 76.0f * scale, 13.0f * scale),
                    juce::Justification::centredLeft);
        if (! state.snapshotValid)
            return;

        const auto signedValue = juce::String (state.snapshot.delta_sharpness >= 0.0 ? "+" : "")
                               + juce::String (state.snapshot.delta_sharpness, 2);
        const float readoutWidth = 67.0f * scale;
        g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.98f));
        g.drawText (signedValue + " acum",
                    juce::Rectangle<float> (right - readoutWidth, outer.getY(),
                                            readoutWidth, 13.0f * scale),
                    juce::Justification::centredRight);

        if (scale > 1.1f)
        {
            g.setFont (monoFont (7.0f * scale));
            g.setColour (COL_SPECTRUM_PRE.withAlpha (0.88f));
            const auto text = "PRE " + juce::String (state.snapshot.pre_sharpness, 2)
                            + "   POST " + juce::String (state.snapshot.post_sharpness, 2);
            g.drawText (text,
                        juce::Rectangle<float> (left, outer.getY() + 12.0f * scale,
                                                right - left, 11.0f * scale),
                        juce::Justification::centredLeft);
        }
    }

    void paintAxes (juce::Graphics& g, juce::Rectangle<float> plot, float scale)
    {
        const float zeroY = yForValue (0.0, plot);
        g.setFont (monoFont (8.0f * scale));
        g.setColour (COL_MUTED.withAlpha (0.86f));
        const int labelWidth = juce::roundToInt (21.0f * scale);
        const int labelHeight = juce::roundToInt (10.0f * scale);
        g.drawText ("+2", 0, juce::roundToInt (plot.getY()) - 4,
                    labelWidth, labelHeight, juce::Justification::centredRight);
        g.drawText ("0", 0, juce::roundToInt (zeroY) - labelHeight / 2,
                    labelWidth, labelHeight, juce::Justification::centredRight);
        g.drawText ("-2", 0, juce::roundToInt (plot.getBottom()) - labelHeight + 3,
                    labelWidth, labelHeight, juce::Justification::centredRight);

        for (double value : { -1.0, 1.0 })
        {
            g.setColour (COL_MUTED.withAlpha (0.16f));
            const float y = yForValue (value, plot);
            g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
        }
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.48f));
        g.drawHorizontalLine (juce::roundToInt (zeroY), plot.getX(), plot.getRight());
        for (double seconds : { 3.0, 6.0 })
        {
            const float x = plot.getRight() - static_cast<float> (
                seconds / perceptual_history::historySeconds) * plot.getWidth();
            g.setColour (COL_MUTED.withAlpha (0.12f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }
        g.setColour (COL_MUTED.withAlpha (0.86f));
        g.drawText ("-6s", juce::roundToInt (plot.getX()),
                    juce::roundToInt (plot.getBottom()), 28, labelHeight,
                    juce::Justification::centredLeft);
        g.drawText ("-3", juce::roundToInt (plot.getCentreX()) - 14,
                    juce::roundToInt (plot.getBottom()), 28, labelHeight,
                    juce::Justification::centred);
        g.drawText ("NOW", juce::roundToInt (plot.getRight()) - 30,
                    juce::roundToInt (plot.getBottom()), 30, labelHeight,
                    juce::Justification::centredRight);
    }

    struct PlotPoints
    {
        std::array<juce::Point<float>, perceptual_history::historyCapacity> values {};
        size_t count = 0u;
    };

    PlotPoints pointsFor (
        const perceptual_history::History& history,
        juce::Rectangle<float> plot)
    {
        PlotPoints points;
        points.count = history.size();
        for (size_t index = 0; index < history.size(); ++index)
        {
            const float age = static_cast<float> (history.ageSecondsAt (index));
            const float x = plot.getRight()
                          - age / static_cast<float> (perceptual_history::historySeconds)
                                * plot.getWidth();
            points.values[index] = { x, yForValue (history.sampleAt (index).delta, plot) };
        }
        return points;
    }

    juce::Path smoothPath (const PlotPoints& points, size_t first, size_t end)
    {
        juce::Path path;
        if (first >= end || end > points.count)
            return path;
        path.startNewSubPath (points.values[first]);
        for (size_t index = first + 1u; index < end; ++index)
        {
            const auto previous = points.values[index - 1u];
            const auto current = points.values[index];
            const auto midpoint = (previous + current) * 0.5f;
            path.quadraticTo (previous, midpoint);
        }
        if (end - first > 1u)
            path.lineTo (points.values[end - 1u]);
        return path;
    }

    void paintRun (juce::Graphics& g,
                   const PlotPoints& points,
                   size_t first,
                   size_t end,
                   juce::Rectangle<float> plot,
                   float zeroY,
                   float scale)
    {
        auto curve = smoothPath (points, first, end);
        if (end - first > 1u)
        {
            auto fill = curve;
            fill.lineTo (points.values[end - 1u].x, zeroY);
            fill.lineTo (points.values[first].x, zeroY);
            fill.closeSubPath();

            // Fill density is a fact about distance from zero, never about the age or value of
            // the first history point. Anchoring this gradient to points[first] made the whole
            // fill fade or collapse as that point moved out of the six-second window. Clip two
            // fixed plot-space gradients to the factual curve instead: quiet at zero, denser
            // toward either display edge, and identical at every point in time.
            const auto distanceGradient = [] (float edgeY, float zeroLineY)
            {
                juce::ColourGradient gradient (
                    COL_SPECTRUM_DELTA.withAlpha (0.46f), 0.0f, edgeY,
                    COL_SPECTRUM_DELTA.withAlpha (0.045f), 0.0f, zeroLineY, false);
                gradient.addColour (0.25, COL_SPECTRUM_DELTA.withAlpha (0.34f));
                gradient.addColour (0.50, COL_SPECTRUM_DELTA.withAlpha (0.22f));
                gradient.addColour (0.75, COL_SPECTRUM_DELTA.withAlpha (0.12f));
                return gradient;
            };

            juce::Graphics::ScopedSaveState clippedFill (g);
            g.reduceClipRegion (fill);

            auto positiveGradient = distanceGradient (plot.getY(), zeroY);
            g.setGradientFill (positiveGradient);
            g.fillRect (juce::Rectangle<float> (
                plot.getX(), plot.getY(), plot.getWidth(), zeroY - plot.getY()));

            auto negativeGradient = distanceGradient (plot.getBottom(), zeroY);
            g.setGradientFill (negativeGradient);
            g.fillRect (juce::Rectangle<float> (
                plot.getX(), zeroY, plot.getWidth(), plot.getBottom() - zeroY));
        }
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.16f));
        g.strokePath (curve, juce::PathStrokeType (4.2f * scale,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.90f));
        g.strokePath (curve, juce::PathStrokeType (1.45f * scale,
                      juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void paintHistory (juce::Graphics& g,
                       juce::Rectangle<float> plot,
                       float scale,
                       const perceptual_history::History& history)
    {
        const auto points = pointsFor (history, plot);
        if (points.count == 0u)
            return;
        const float zeroY = yForValue (0.0, plot);
        size_t runStart = 0u;
        for (size_t index = 1u; index <= points.count; ++index)
        {
            if (index == points.count || ! history.sampleAt (index).continuesPrevious)
            {
                paintRun (g, points, runStart, index, plot, zeroY, scale);
                runStart = index;
            }
        }

        // Sharpness is a six-second history, so its ink must not age or acquire a moving
        // high-emphasis tail. The curve and fill above remain uniform across the factual run;
        // only this point identifies the newest exact observation.
        const auto newest = points.values[points.count - 1u];
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.20f));
        g.fillEllipse (newest.x - 4.0f * scale, newest.y - 4.0f * scale,
                       8.0f * scale, 8.0f * scale);
        g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.98f));
        g.fillEllipse (newest.x - 1.8f * scale, newest.y - 1.8f * scale,
                       3.6f * scale, 3.6f * scale);
    }
}

void paint (juce::Graphics& g,
            juce::Rectangle<float> bounds,
            const PaintState& state)
{
    const float scale = spectrum_geometry::visualScaleFor (bounds);
    const auto outer = spectrum_geometry::plotBoundsFor (bounds);
    const auto plot = historyPlot (outer, scale);
    paintMode (g, outer, scale, state);
    paintHeader (g, outer, scale, state);
    paintAxes (g, plot, scale);

    if (! state.snapshotValid || state.history.empty())
    {
        const auto text = state.haveSnapshot ? statusText (state.snapshot.status)
                                             : juce::String ("SYNC");
        if (text.isNotEmpty())
        {
            g.setColour (COL_MUTED);
            g.setFont (monoFont (13.0f * scale));
            g.drawText (text, plot, juce::Justification::centred);
        }
        return;
    }
    paintHistory (g, plot, scale, state.history);
}
}
