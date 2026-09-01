#include "HyphaSpectrumChromePainter.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumMagnitudeChrome.h"
#include "HyphaAnalysisUiText.h"
#include "HyphaSpectrumFocusTrailPainter.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"

#include <algorithm>
#include <cmath>

namespace hypha::spectrum_chrome
{
juce::String frequencyReadoutText (float hz, float approximateBelowHz)
{
    const juce::String prefix = hz < approximateBelowHz ? "~" : "";
    if (hz < 1'000.0f)
        return prefix + juce::String (juce::roundToInt (hz)) + " Hz";
    const int decimals = hz < 10'000.0f ? 2 : 1;
    return prefix + juce::String (hz / 1'000.0f, decimals) + " kHz";
}

namespace
{
    const char* channelModeText (uint8_t mode) noexcept
    {
        if (mode == KIRIN_SPECTRUM_CHANNEL_MID) return "MID";
        if (mode == KIRIN_SPECTRUM_CHANNEL_SIDE) return "SIDE";
        return "LR";
    }

    juce::String statusText (uint8_t status, const juce::String& analysisOwnerNames)
    {
        if (status == KIRIN_SPECTRUM_NO_PAIR) return juce::CharPointer_UTF8 ("PAIR —");
        if (status == KIRIN_SPECTRUM_WARMING_UP) return juce::CharPointer_UTF8 ("SYNC ◌");
        if (status == KIRIN_SPECTRUM_UNAVAILABLE) return juce::CharPointer_UTF8 ("DATA —");
        if (status == KIRIN_SPECTRUM_IN_USE)
            return analysis_ui::slotsInUse (analysisOwnerNames);
        return {};
    }

    juce::String axisFrequencyText (float hz)
    {
        if (hz < 1'000.0f)
            return juce::String (juce::roundToInt (hz));
        const float khz = hz / 1'000.0f;
        const float rounded = std::round (khz);
        return std::abs (khz - rounded) < 0.05f
                 ? juce::String (juce::roundToInt (rounded)) + "k"
                 : juce::String (khz, 1) + "k";
    }

    void paintAxes (juce::Graphics& g,
                    juce::Rectangle<float> plot,
                    float scale,
                    float minimumHz,
                    float maximumHz,
                    bool absoluteObservation)
    {
        const auto scaled = [scale] (float value) { return value * scale; };
        const auto scaledInt = [scale] (int value) {
            return juce::roundToInt ((float) value * scale);
        };
        g.setFont (monoFont (8.5f * ui_contract::analysisTextScale (scale)));
        g.setColour (COL_MUTED.withAlpha (0.86f));
        if (absoluteObservation)
            spectrum_magnitude_chrome::paintAxis (g, plot, scale, true, true);
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
            spectrum_magnitude_chrome::paintAxis (g, plot, scale, false, false);
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

    void paintModeAndLegend (juce::Graphics& g,
                             juce::Rectangle<float> outerPlot,
                             float scale,
                             float reservedReadoutWidth,
                             bool expandedReadout,
                             bool showProbe,
                             const PaintState& state)
    {
        const auto scaled = [scale] (float value) { return value * scale; };
        const auto scaledInt = [scale] (int value) {
            return juce::roundToInt ((float) value * scale);
        };
        g.setFont (monoFont (7.5f * ui_contract::analysisTextScale (scale)));
        if (state.actionNotice.isNotEmpty())
        {
            g.setColour (COL_MUTED.withAlpha (0.90f));
            g.drawText (state.actionNotice,
                        juce::Rectangle<float> (
                            outerPlot.getX(), outerPlot.getY(),
                            outerPlot.getWidth() - reservedReadoutWidth - scaled (4.0f),
                            scaled ((float) ui_contract::spectrumChannelModeHeight)),
                        juce::Justification::centredLeft);
            return;
        }
        if (expandedReadout)
        {
            g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.94f));
            g.drawText (channelModeText (state.channelMode),
                        juce::Rectangle<float> (
                            outerPlot.getX(), outerPlot.getY(),
                            outerPlot.getWidth() - reservedReadoutWidth - scaled (4.0f),
                            scaled ((float) ui_contract::spectrumChannelModeHeight)),
                        juce::Justification::centredLeft);
            return;
        }
        for (size_t index = 0; index < ui_contract::spectrumChannelModeWidths.size(); ++index)
        {
            const auto mode = static_cast<uint8_t> (index);
            const auto segment = spectrum_geometry::channelModeBoundsFor (
                index, outerPlot, scale);
            const bool selected = mode == state.channelMode;
            const bool unavailable = mode == KIRIN_SPECTRUM_CHANNEL_SIDE
                                  && state.inputChannels == 1u;
            if (selected)
            {
                g.setColour (BG.brighter (0.14f).withAlpha (0.92f));
                g.fillRoundedRectangle (segment, scaled (3.0f));
                g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.62f));
                g.drawRoundedRectangle (segment, scaled (3.0f), scaled (0.65f));
            }
            g.setColour (unavailable ? COL_MUTED.withAlpha (0.30f)
                                     : selected ? COL_SPECTRUM_DELTA_BR.withAlpha (0.98f)
                                                : COL_MUTED.withAlpha (0.78f));
            g.drawText (channelModeText (mode), segment.toNearestInt(),
                        juce::Justification::centred);
        }
        if (showProbe)
            return;

        const int legendOffset = scaledInt (ui_contract::spectrumLegendAfterChannelModes);
        const float legendTop = outerPlot.getY()
                              + scaled ((float) ui_contract::spectrumLegendTop);
        g.setFont (monoFont (ui_contract::spectrumLegendFontHeight
                             * ui_contract::analysisTextScale (scale)));
        if (state.absoluteObservation)
        {
            g.setColour (COL_SPECTRUM_POST.withAlpha (0.96f));
            g.drawText ("POST ABS", juce::roundToInt (outerPlot.getX()) + legendOffset,
                        juce::roundToInt (legendTop), scaledInt (48),
                        scaledInt (ui_contract::spectrumLegendHeight),
                        juce::Justification::centredLeft);
            g.setColour (COL_FLORA_BR.withAlpha (0.64f));
            g.drawText ("6 S FIELD  HOLD",
                        juce::roundToInt (outerPlot.getX()) + legendOffset + scaledInt (52),
                        juce::roundToInt (legendTop), scaledInt (92),
                        scaledInt (ui_contract::spectrumLegendHeight),
                        juce::Justification::centredLeft);
            return;
        }
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (ui_contract::spectrumDeltaLegendAlpha));
        g.drawText (juce::CharPointer_UTF8 ("\xCE\x94"),
                    juce::roundToInt (outerPlot.getX()) + legendOffset
                        + scaledInt (ui_contract::spectrumDeltaLegendLabelX),
                    juce::roundToInt (legendTop),
                    scaledInt (ui_contract::spectrumDeltaLegendLabelWidth),
                    scaledInt (ui_contract::spectrumLegendHeight),
                    juce::Justification::centredLeft);
        g.setColour (COL_SPECTRUM_PRE.withAlpha (ui_contract::spectrumPreLegendAlpha));
        g.drawText ("PRE", juce::roundToInt (outerPlot.getX()) + legendOffset
                             + scaledInt (ui_contract::spectrumPreLegendLabelX),
                    juce::roundToInt (legendTop),
                    scaledInt (ui_contract::spectrumPreLegendLabelWidth),
                    scaledInt (ui_contract::spectrumLegendHeight),
                    juce::Justification::centredLeft);
        g.setColour (COL_SPECTRUM_POST.withAlpha (ui_contract::spectrumPostLegendAlpha));
        g.drawText ("POST", juce::roundToInt (outerPlot.getX()) + legendOffset
                              + scaledInt (ui_contract::spectrumPostLegendLabelX),
                    juce::roundToInt (legendTop),
                    scaledInt (ui_contract::spectrumPostLegendLabelWidth),
                    scaledInt (ui_contract::spectrumLegendHeight),
                    juce::Justification::centredLeft);

        const auto mark = spectrum_geometry::markBoundsFor (outerPlot, scale);
        if (state.haveMark)
        {
            g.setColour (COL_FLORA.withAlpha (
                ui_contract::spectrumMarkButtonActiveFillAlpha));
            g.fillRoundedRectangle (mark, scaled (3.0f));
        }
        g.setColour ((state.haveMark ? COL_FLORA_BR : COL_FLORA).withAlpha (
            state.haveMark ? ui_contract::spectrumMarkButtonActiveBorderAlpha
                           : ui_contract::spectrumMarkButtonInactiveBorderAlpha));
        g.drawRoundedRectangle (mark, scaled (3.0f), scaled (0.75f));
        g.setFont (monoFont (7.0f * ui_contract::analysisTextScale (scale)));
        g.setColour (state.snapshotValid
                         ? (state.haveMark ? COL_FLORA_BR : COL_FLORA).withAlpha (
                               state.haveMark
                                   ? ui_contract::spectrumMarkButtonActiveAlpha
                                   : ui_contract::spectrumMarkButtonInactiveAlpha)
                                         : COL_MUTED.withAlpha (0.34f));
        auto labelBounds = mark;
        if (state.haveMark)
            labelBounds.removeFromRight (scaled ((float) ui_contract::spectrumMarkClearWidth));
        g.drawText ("MARK", labelBounds.toNearestInt(), juce::Justification::centred);
        if (state.haveMark)
        {
            g.setColour (COL_FLORA_BR.withAlpha (0.82f));
            g.drawText (juce::CharPointer_UTF8 ("×"),
                        spectrum_geometry::markClearBoundsFor (mark, scale).toNearestInt(),
                        juce::Justification::centred);
        }
    }

    void paintProbe (juce::Graphics& g,
                     juce::Rectangle<float> outerPlot,
                     juce::Rectangle<float> plot,
                     float scale,
                     float probeNormalisedX,
                     float minimumHz,
                     float maximumHz,
                     const PaintState& state)
    {
        const auto scaled = [scale] (float value) { return value * scale; };
        const auto scaledInt = [scale] (int value) {
            return juce::roundToInt ((float) value * scale);
        };
        const bool focusLocked = state.focusFrequencyHz > 0.0f;
        const bool expanded = scale > 1.1f;
        const float effectiveNormalisedX = spectrum_geometry::clampToBandCentreRange (
            probeNormalisedX);
        const float hoverX = juce::jmap (effectiveNormalisedX, plot.getX(), plot.getRight());
        const float bandPosition = spectrum_geometry::bandPositionForNormalisedX (
            effectiveNormalisedX);
        const size_t lower = static_cast<size_t> (std::floor (bandPosition));
        const size_t upper = std::min (
            lower + 1u, static_cast<size_t> (KIRIN_SPECTRUM_BAND_COUNT - 1u));
        const float blend = bandPosition - (float) lower;
        const float preDbfs = juce::jmap (
            blend, state.readoutPre[lower], state.readoutPre[upper]);
        const float postDbfs = juce::jmap (
            blend, state.readoutPost[lower], state.readoutPost[upper]);
        const float deltaDb = juce::jmap (
            blend, state.readoutDelta[lower], state.readoutDelta[upper]);
        const float pointY = state.absoluteObservation
            ? juce::jmap (juce::jlimit (-96.0f, 0.0f, postDbfs),
                          0.0f, -96.0f, plot.getY(), plot.getBottom())
            : spectrum_geometry::yForDeltaDb (deltaDb, plot);

        g.setColour (COL_NORMAL.withAlpha (focusLocked ? 0.48f : 0.30f));
        g.drawLine (hoverX, plot.getY(), hoverX, plot.getBottom(),
                    ui_contract::spectrumStrokeScale (scale)
                        * ui_contract::spectrumHoverLineWidth);
        const auto pointColour = state.absoluteObservation
            ? COL_SPECTRUM_POST : COL_SPECTRUM_DELTA;
        g.setColour (pointColour.withAlpha (0.18f));
        g.fillEllipse (hoverX - scaled (3.5f), pointY - scaled (3.5f),
                       scaled (7.0f), scaled (7.0f));
        g.setColour ((state.absoluteObservation ? COL_SPECTRUM_POST.brighter (0.35f)
                                                : COL_SPECTRUM_DELTA_BR).withAlpha (0.98f));
        g.fillEllipse (hoverX - scaled (1.65f), pointY - scaled (1.65f),
                       scaled (3.3f), scaled (3.3f));

        const auto readout = spectrum_geometry::readoutBoundsFor (
            outerPlot, scale, expanded, focusLocked);
        g.setColour (BG.brighter (0.10f).withAlpha (0.96f));
        g.fillRoundedRectangle (readout, scaled (ui_contract::spectrumHoverReadoutRadius));
        g.setColour (pointColour.withAlpha (0.38f));
        g.drawRoundedRectangle (readout, scaled (ui_contract::spectrumHoverReadoutRadius),
                                scaled (0.75f));

        const float frequency = spectrum_geometry::frequencyForProbeNormalisedX (
            effectiveNormalisedX, minimumHz, maximumHz);
        const auto deltaText = juce::String (deltaDb >= 0.0f ? "+" : "")
                             + juce::String (deltaDb, 1);
        const int textY = juce::roundToInt (readout.getY());
        g.setFont (monoFont ((expanded ? 8.0f : 8.5f)
                             * ui_contract::analysisTextScale (scale)));
        const auto drawText = [&] (const juce::String& text, juce::Colour colour,
                                    int logicalX, int logicalWidth,
                                    juce::Justification justification)
        {
            g.setColour (colour);
            g.drawText (text, juce::roundToInt (readout.getX()) + scaledInt (logicalX),
                        textY, scaledInt (logicalWidth),
                        scaledInt (ui_contract::spectrumHoverReadoutHeight), justification);
        };
        if (state.absoluteObservation)
        {
            drawText (frequencyReadoutText (
                          frequency, state.snapshot.approximate_below_hz),
                      COL_NORMAL.withAlpha (0.94f),
                      expanded ? ui_contract::spectrumExpandedFrequencyX
                               : ui_contract::spectrumHoverFrequencyX,
                      expanded ? ui_contract::spectrumExpandedFrequencyWidth
                               : ui_contract::spectrumHoverFrequencyWidth,
                      juce::Justification::centredLeft);
            drawText ("POST " + juce::String (postDbfs, 1),
                      COL_SPECTRUM_POST.withAlpha (0.98f),
                      expanded ? ui_contract::spectrumExpandedPostX
                               : ui_contract::spectrumHoverDeltaX,
                      expanded ? ui_contract::spectrumExpandedPostWidth
                               : ui_contract::spectrumHoverDeltaWidth,
                      juce::Justification::centredRight);
            if (focusLocked)
            {
                g.setColour (COL_NORMAL.withAlpha (0.72f));
                g.drawText (juce::CharPointer_UTF8 ("×"),
                            spectrum_geometry::focusClearBoundsFor (
                                readout, scale).toNearestInt(),
                            juce::Justification::centred);
            }
            return;
        }
        if (expanded)
        {
            drawText (frequencyReadoutText (
                          frequency, state.snapshot.approximate_below_hz),
                      COL_NORMAL.withAlpha (0.94f),
                      ui_contract::spectrumExpandedFrequencyX,
                      ui_contract::spectrumExpandedFrequencyWidth,
                      juce::Justification::centredLeft);
            drawText ("PRE " + juce::String (preDbfs, 1),
                      COL_SPECTRUM_PRE.withAlpha (0.98f),
                      ui_contract::spectrumExpandedPreX,
                      ui_contract::spectrumExpandedPreWidth,
                      juce::Justification::centredRight);
            drawText ("POST " + juce::String (postDbfs, 1),
                      COL_SPECTRUM_POST.withAlpha (0.98f),
                      ui_contract::spectrumExpandedPostX,
                      ui_contract::spectrumExpandedPostWidth,
                      juce::Justification::centredRight);
            drawText (juce::String (juce::CharPointer_UTF8 ("Δ")) + deltaText,
                      COL_SPECTRUM_DELTA_BR.withAlpha (0.98f),
                      ui_contract::spectrumExpandedDeltaX,
                      ui_contract::spectrumExpandedDeltaWidth,
                      juce::Justification::centredRight);
        }
        else
        {
            drawText (frequencyReadoutText (
                          frequency, state.snapshot.approximate_below_hz),
                      COL_NORMAL.withAlpha (0.94f),
                      ui_contract::spectrumHoverFrequencyX,
                      ui_contract::spectrumHoverFrequencyWidth,
                      juce::Justification::centredLeft);
            drawText (juce::String (juce::CharPointer_UTF8 ("Δ")) + deltaText,
                      COL_SPECTRUM_DELTA_BR.withAlpha (0.98f),
                      ui_contract::spectrumHoverDeltaX,
                      ui_contract::spectrumHoverDeltaWidth,
                      juce::Justification::centredRight);
        }
        if (focusLocked)
        {
            g.setColour (COL_NORMAL.withAlpha (0.72f));
            g.drawText (juce::CharPointer_UTF8 ("×"),
                        spectrum_geometry::focusClearBoundsFor (
                            readout, scale).toNearestInt(),
                        juce::Justification::centred);
        }
    }
}

void paint (juce::Graphics& g,
            juce::Rectangle<float> bounds,
            const PaintState& state)
{
    const float scale = spectrum_geometry::visualScaleFor (bounds);
    const auto outerPlot = spectrum_geometry::plotBoundsFor (bounds);
    const auto plot = spectrum_geometry::dataPlotBoundsFor (bounds);
    const float minimumHz = state.haveSnapshot && state.snapshot.min_hz > 0.0f
                          ? state.snapshot.min_hz : 10.0f;
    const float maximumHz = state.haveSnapshot && state.snapshot.max_hz > minimumHz
                          ? state.snapshot.max_hz : 22'000.0f;
    const float probeNormalisedX = state.snapshotValid && state.hoverNormalisedX >= 0.0f
        ? spectrum_geometry::clampToBandCentreRange (state.hoverNormalisedX)
        : state.snapshotValid && state.focusFrequencyHz > 0.0f
            ? spectrum_geometry::clampToBandCentreRange (
                spectrum_geometry::normalisedXForFrequency (
                    state.focusFrequencyHz, minimumHz, maximumHz))
            : -1.0f;
    const float focusNormalisedX = state.snapshotValid && state.focusFrequencyHz > 0.0f
        ? spectrum_geometry::clampToBandCentreRange (
            spectrum_geometry::normalisedXForFrequency (
                state.focusFrequencyHz, minimumHz, maximumHz))
        : -1.0f;

    paintAxes (g, plot, scale, minimumHz, maximumHz, state.absoluteObservation);
    const bool expandedReadout = scale > 1.1f && probeNormalisedX >= 0.0f;
    const float reservedReadoutWidth = probeNormalisedX >= 0.0f
        ? (float) (scale > 1.1f ? ui_contract::spectrumExpandedReadoutWidth
                               : state.focusFrequencyHz > 0.0f
                                   ? ui_contract::spectrumFocusReadoutWidth
                                   : ui_contract::spectrumHoverReadoutWidth) * scale
        : 0.0f;
    paintModeAndLegend (g, outerPlot, scale, reservedReadoutWidth,
                        expandedReadout, probeNormalisedX >= 0.0f, state);

    if (! state.snapshotValid)
    {
        const auto text = state.haveSnapshot
                            ? statusText (state.snapshot.status, state.analysisOwnerNames)
                                             : juce::String ("SYNC");
        if (text.isNotEmpty())
        {
            g.setColour (COL_MUTED);
            g.setFont (monoFont (13.0f * scale));
            g.drawFittedText (text, plot.toNearestInt(), juce::Justification::centred,
                              2, 0.72f);
        }
        return;
    }

    if (state.guideOverlay.visible())
        guide_frequency::paint (g, plot, scale, state.guideOverlay,
                                minimumHz, maximumHz);
    if (state.absoluteObservation && state.absoluteHistory != nullptr)
        spectrum_painter::paintAbsolute (g, plot, scale, state.post,
                                         state.absolutePeakHold,
                                         *state.absoluteHistory);
    else
        spectrum_painter::paintCurves (g, plot, scale, state.pre, state.post,
                                       state.delta, state.haveMark ? &state.mark : nullptr);
    if (! state.absoluteObservation && focusNormalisedX >= 0.0f
        && state.focusTrail != nullptr && ! state.focusTrail->empty())
    {
        spectrum_focus_painter::paint (
            g, spectrum_geometry::focusTrailBoundsFor (bounds), scale,
            *state.focusTrail, focusNormalisedX, scale <= 1.1f);
    }
    else if (! state.absoluteObservation && scale > 1.1f && focusNormalisedX < 0.0f)
        spectrum_focus_painter::paintEmptyPrompt (
            g, spectrum_geometry::focusTrailBoundsFor (bounds), scale);
    if (probeNormalisedX >= 0.0f)
        paintProbe (g, outerPlot, plot, scale, probeNormalisedX,
                    minimumHz, maximumHz, state);
}
}
