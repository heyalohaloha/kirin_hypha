#include "HyphaSpectrumProbePainter.h"

#include "HyphaSpectrumGeometry.h"
#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <algorithm>
#include <cmath>

namespace hypha::spectrum_chrome
{
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
    const bool deltaValid = state.readoutDeltaValid[lower] != 0u
        && (upper == lower || blend < 0.0001f
            || state.readoutDeltaValid[upper] != 0u);
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
    if (! state.midSideObservation && (state.absoluteObservation || deltaValid))
    {
        g.setColour (pointColour.withAlpha (0.18f));
        g.fillEllipse (hoverX - scaled (3.5f), pointY - scaled (3.5f),
                       scaled (7.0f), scaled (7.0f));
        g.setColour ((state.absoluteObservation ? COL_SPECTRUM_POST.brighter (0.35f)
                                                : COL_SPECTRUM_DELTA_BR).withAlpha (0.98f));
        g.fillEllipse (hoverX - scaled (1.65f), pointY - scaled (1.65f),
                       scaled (3.3f), scaled (3.3f));
    }

    const auto readout = state.midSideObservation
        ? spectrum_geometry::midSideReadoutBoundsFor (outerPlot, scale, expanded)
        : spectrum_geometry::readoutBoundsFor (outerPlot, scale, expanded, focusLocked);
    g.setColour (BG.brighter (0.10f).withAlpha (0.96f));
    g.fillRoundedRectangle (readout, scaled (ui_contract::spectrumHoverReadoutRadius));
    g.setColour (pointColour.withAlpha (0.38f));
    g.drawRoundedRectangle (readout, scaled (ui_contract::spectrumHoverReadoutRadius),
                            scaled (0.75f));

    const float frequency = spectrum_geometry::frequencyForProbeNormalisedX (
        effectiveNormalisedX, minimumHz, maximumHz);
    const auto deltaText = deltaValid
        ? juce::String (deltaDb >= 0.0f ? "+" : "") + juce::String (deltaDb, 1)
        : juce::String (juce::CharPointer_UTF8 ("—"));
    const juce::String deltaLabel = state.shapeObservation ? "S" :
        juce::String (juce::CharPointer_UTF8 ("Δ"));
    const int textY = juce::roundToInt (readout.getY());
    g.setFont (monoFont (state.presentation, typography::TextRole::readout,
                         typography::Composition::visualization));
    const auto drawText = [&] (const juce::String& text, juce::Colour colour,
                                int logicalX, int logicalWidth,
                                juce::Justification justification)
    {
        g.setColour (colour);
        text_style::drawText (g, text, juce::roundToInt (readout.getX()) + scaledInt (logicalX),
                    textY, scaledInt (logicalWidth),
                    scaledInt (ui_contract::spectrumHoverReadoutHeight), justification);
    };
    if (state.midSideObservation)
    {
        drawText (frequencyReadoutText (frequency, state.snapshot.approximate_below_hz),
                  COL_NORMAL.withAlpha (0.94f), 6, expanded ? 52 : 62,
                  juce::Justification::centredLeft);
        drawText ("M " + juce::String (preDbfs, 1), COL_SPECTRUM_MID.withAlpha (0.98f),
                  expanded ? 58 : 72, expanded ? 84 : 72,
                  juce::Justification::centredRight);
        drawText ("S " + juce::String (postDbfs, 1), COL_SPECTRUM_SIDE.withAlpha (0.98f),
                  expanded ? 144 : 148, expanded ? 84 : 70,
                  juce::Justification::centredRight);
        if (focusLocked)
        {
            g.setColour (COL_NORMAL.withAlpha (0.72f));
            text_style::drawText (g, juce::CharPointer_UTF8 ("×"),
                        spectrum_geometry::focusClearBoundsFor (readout, scale).toNearestInt(),
                        juce::Justification::centred);
        }
        return;
    }
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
        drawText ((expanded ? "POST " : "") + juce::String (postDbfs, 1),
                  COL_SPECTRUM_POST.withAlpha (0.98f),
                  expanded ? ui_contract::spectrumExpandedPostX
                           : ui_contract::spectrumHoverDeltaX,
                  expanded ? ui_contract::spectrumExpandedPostWidth
                           : ui_contract::spectrumHoverDeltaWidth,
                  juce::Justification::centredRight);
        if (focusLocked)
        {
            g.setColour (COL_NORMAL.withAlpha (0.72f));
            text_style::drawText (g, juce::CharPointer_UTF8 ("×"),
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
        drawText (deltaLabel + deltaText,
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
        drawText (deltaLabel + deltaText,
                  COL_SPECTRUM_DELTA_BR.withAlpha (0.98f),
                  ui_contract::spectrumHoverDeltaX,
                  ui_contract::spectrumHoverDeltaWidth,
                  juce::Justification::centredRight);
    }
    if (focusLocked)
    {
        g.setColour (COL_NORMAL.withAlpha (0.72f));
        text_style::drawText (g, juce::CharPointer_UTF8 ("×"),
                    spectrum_geometry::focusClearBoundsFor (
                        readout, scale).toNearestInt(),
                    juce::Justification::centred);
    }
}
}
