#include "HyphaAbsolutePainter.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaAnalysisUiText.h"
#include "HyphaTheme.h"

#include <array>
#include <cmath>

namespace hypha::absolute_painter
{
double displayValueOrFloor (double measuredValue, double displayMinimum) noexcept
{
    return std::isfinite (measuredValue) ? measuredValue : displayMinimum;
}

juce::String factValueText (double measuredValue, int decimals)
{
    // Keep the unavailable token ASCII-only. A raw em dash passed to juce::String's narrow-char
    // constructor follows the Windows system code page and becomes mojibake in Studio One.
    return std::isfinite (measuredValue) ? juce::String (measuredValue, decimals) : "--";
}

namespace
{
    constexpr double historySeconds = 6.0;
    constexpr double lufsMinimum = -42.0;
    constexpr double lufsMaximum = 0.0;
    constexpr double peakMinimum = -30.0;
    constexpr double peakMaximum = 6.0;
    constexpr double sharpnessMinimum = 0.0;
    constexpr double sharpnessMaximum = 3.0;

    juce::String statusText (uint8_t status, const juce::String& analysisOwnerNames)
    {
        if (status == KIRIN_SPECTRUM_WARMING_UP) return juce::CharPointer_UTF8 ("OBSERVE ◌");
        if (status == KIRIN_SPECTRUM_IN_USE)
            return analysis_ui::slotsInUse (analysisOwnerNames);
        if (status == KIRIN_SPECTRUM_UNAVAILABLE) return juce::CharPointer_UTF8 ("DATA —");
        return {};
    }

    float yFor (double value, double minimum, double maximum,
                juce::Rectangle<float> plot)
    {
        const auto clipped = juce::jlimit (minimum, maximum, value);
        return juce::jmap (static_cast<float> (clipped),
                           static_cast<float> (maximum),
                           static_cast<float> (minimum), plot.getY(), plot.getBottom());
    }

    juce::Rectangle<float> valueBand (juce::Rectangle<float> plot,
                                      float topProportion,
                                      float bottomProportion)
    {
        const float top = plot.getY() + topProportion * plot.getHeight();
        const float bottom = plot.getY() + bottomProportion * plot.getHeight();
        return { plot.getX(), top, plot.getWidth(), bottom - top };
    }

    juce::Rectangle<float> absoluteOuterPlot (juce::Rectangle<float> bounds, float scale)
    {
        return bounds
            .withTrimmedLeft ((float) ui_contract::spectrumPlotLeftInset * scale)
            .withTrimmedRight ((float) ui_contract::spectrumPlotRightInset * scale)
            .withTrimmedTop (ui_contract::absolutePlotTopInset * scale)
            .withTrimmedBottom (ui_contract::absolutePlotBottomInset * scale);
    }

    juce::String factText (const char* compact, const char* full,
                           double value, int decimals, float scale)
    {
        const auto label = scale > 1.1f ? full : compact;
        return juce::String (label) + " " + factValueText (value, decimals);
    }

    void paintHeader (juce::Graphics& g, juce::Rectangle<float> area,
                      float scale, const PaintState& state)
    {
        const auto third = area.getWidth() / 3.0f;
        g.setFont (monoFont (8.0f * ui_contract::analysisTextScale (scale)));
        const auto latest = state.haveNumericSnapshot ? state.numericSnapshot
                                                       : KirinAbsoluteView {};
        const std::array<juce::Colour, 3> colours {
            COL_SPECTRUM_DELTA, COL_SPECTRUM_POST, COL_FLORA
        };
        const std::array<juce::String, 3> text {
            factText ("M", "LUFS-M", latest.lufs_m, 1, scale),
            factText ("TP", "TRUE PEAK", latest.true_peak, 1, scale),
            factText ("SH", "SHARPNESS", latest.sharpness, 2, scale)
        };
        for (size_t index = 0u; index < text.size(); ++index)
        {
            g.setColour (colours[index].withAlpha (0.98f));
            g.drawText (text[index],
                        juce::Rectangle<float> (area.getX() + third * static_cast<float> (index),
                                                area.getY(), third, area.getHeight()),
                        juce::Justification::centred);
        }
    }

    void paintAxes (juce::Graphics& g, juce::Rectangle<float> plot, float scale)
    {
        for (float proportion : { 0.25f, 0.5f, 0.75f })
        {
            g.setColour (COL_MUTED.withAlpha (0.12f));
            g.drawHorizontalLine (juce::roundToInt (plot.getY() + proportion * plot.getHeight()),
                                  plot.getX(), plot.getRight());
        }
        for (double seconds : { 3.0, 6.0 })
        {
            const float x = plot.getRight()
                          - static_cast<float> (seconds / historySeconds) * plot.getWidth();
            g.setColour (COL_MUTED.withAlpha (0.14f));
            g.drawVerticalLine (juce::roundToInt (x), plot.getY(), plot.getBottom());
        }
        g.setFont (monoFont (8.0f * ui_contract::analysisTextScale (scale)));
        g.setColour (COL_MUTED.withAlpha (0.82f));
        const int y = juce::roundToInt (plot.getBottom());
        const int labelWidth = juce::roundToInt (30.0f * scale);
        const int labelHeight = juce::roundToInt (10.0f * scale);
        g.drawText ("-6s", juce::roundToInt (plot.getX()), y,
                    labelWidth, labelHeight,
                    juce::Justification::centredLeft);
        g.drawText ("-3", juce::roundToInt (plot.getCentreX()) - labelWidth / 2, y,
                    labelWidth, labelHeight,
                    juce::Justification::centred);
        g.drawText ("NOW", juce::roundToInt (plot.getRight()) - labelWidth, y,
                    labelWidth, labelHeight,
                    juce::Justification::centredRight);
    }

    template <typename ValueFn>
    void paintSeries (juce::Graphics& g, const KirinAbsoluteBatch& batch,
                      juce::Rectangle<float> plot, juce::Colour colour,
                      double minimum, double maximum,
                      float bandTop, float bandBottom,
                      float scale, ValueFn valueFor)
    {
        if (batch.count == 0u)
            return;
        const auto band = valueBand (plot, bandTop, bandBottom);
        juce::Path path;
        juce::Point<float> newestPoint;
        bool pathStarted = false;
        const auto newest = batch.latest.presentation_end_samples;
        const auto rate = static_cast<double> (batch.latest.sample_rate);
        for (uint32_t index = 0u; index < batch.count; ++index)
        {
            const auto& frame = batch.frames[index];
            const auto value = displayValueOrFloor (valueFor (frame), minimum);
            const auto age = static_cast<double> (newest - frame.presentation_end_samples) / rate;
            const float x = plot.getRight()
                          - static_cast<float> (age / historySeconds) * plot.getWidth();
            const juce::Point<float> point { x, yFor (value, minimum, maximum, band) };
            newestPoint = point;
            // JUCE deliberately reports a move-only Path as empty because it has no drawable
            // segment. Using Path::isEmpty() as the start sentinel therefore emits another
            // moveTo for every history point and the complete multi-point LIVE curve vanishes.
            // Track the first point explicitly so every later verified point becomes a line.
            if (pathStarted)
                path.lineTo (point);
            else
            {
                path.startNewSubPath (point);
                pathStarted = true;
            }
        }
        if (batch.count == 1u)
        {
            const float glowDiameter = 5.2f * scale;
            const float coreDiameter = 2.4f * scale;
            g.setColour (colour.withAlpha (0.20f));
            g.fillEllipse (newestPoint.x - glowDiameter * 0.5f,
                           newestPoint.y - glowDiameter * 0.5f,
                           glowDiameter, glowDiameter);
            g.setColour (colour.withAlpha (0.96f));
            g.fillEllipse (newestPoint.x - coreDiameter * 0.5f,
                           newestPoint.y - coreDiameter * 0.5f,
                           coreDiameter, coreDiameter);
            return;
        }
        g.setColour (colour.withAlpha (0.94f));
        g.strokePath (path, juce::PathStrokeType (1.15f * scale,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    }
}

void paint (juce::Graphics& g, juce::Rectangle<float> bounds, const PaintState& state)
{
    const float scale = spectrum_geometry::visualScaleFor (bounds);
    auto outer = absoluteOuterPlot (bounds, scale);
    auto header = outer.removeFromTop ((scale > 1.1f ? 23.0f : 17.0f) * scale);
    paintHeader (g, header, scale, state);
    auto plot = outer;
    plot.removeFromBottom (10.0f * scale);
    paintAxes (g, plot, scale);

    if (! state.haveBatch || state.batch.count == 0u)
    {
        const auto status = state.haveBatch
                              ? statusText (state.batch.latest.status,
                                            state.analysisOwnerNames)
                                            : juce::String ("OBSERVE --");
        g.setFont (monoFont (10.0f * ui_contract::analysisTextScale (scale)));
        g.setColour (COL_MUTED.withAlpha (0.84f));
        g.drawFittedText (status, plot.toNearestInt(), juce::Justification::centred,
                          2, 0.72f);
        return;
    }

    paintSeries (g, state.batch, plot, COL_SPECTRUM_DELTA,
                 lufsMinimum, lufsMaximum,
                 ui_contract::absoluteLufsBandTop,
                 ui_contract::absoluteLufsBandBottom, scale,
                 [] (const KirinAbsoluteView& frame) { return frame.lufs_m; });
    paintSeries (g, state.batch, plot, COL_SPECTRUM_POST,
                 peakMinimum, peakMaximum,
                 ui_contract::absolutePeakBandTop,
                 ui_contract::absolutePeakBandBottom, scale,
                 [] (const KirinAbsoluteView& frame) { return frame.true_peak; });
    paintSeries (g, state.batch, plot, COL_FLORA,
                 sharpnessMinimum, sharpnessMaximum,
                 ui_contract::absoluteSharpnessBandTop,
                 ui_contract::absoluteSharpnessBandBottom, scale,
                 [] (const KirinAbsoluteView& frame) { return frame.sharpness; });
}
}
