#include "HyphaSpectrumComponent.h"
#include "HyphaSpectrumPainter.h"
#include "HyphaSpectrumPresentation.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace hypha
{
namespace
{
    constexpr float kDeltaRangeDb = KIRIN_SPECTRUM_DISPLAY_RANGE_DB;

    template <size_t Size>
    bool finiteBins (const float (&values)[Size]) noexcept
    {
        return std::all_of (std::begin (values), std::end (values),
                            [] (float value) { return std::isfinite (value); });
    }

    bool validSnapshot (const KirinSpectrumView& view) noexcept
    {
        return view.has_data != 0
            && view.status == KIRIN_SPECTRUM_ACTIVE
            && view.sample_rate >= 8000u
            && std::isfinite (view.min_hz)
            && std::isfinite (view.max_hz)
            && view.min_hz > 0.0f
            && view.max_hz > view.min_hz
            && finiteBins (view.pre_dbfs)
            && finiteBins (view.post_dbfs)
            && finiteBins (view.display_db);
    }

    bool sameFloatBits (float left, float right) noexcept
    {
        return std::memcmp (&left, &right, sizeof (float)) == 0;
    }

    float visualScaleFor (juce::Rectangle<float> bounds) noexcept
    {
        return ui_contract::spectrumVisualScale (juce::roundToInt (bounds.getWidth()));
    }

    juce::Rectangle<float> plotBoundsFor (juce::Rectangle<float> bounds) noexcept
    {
        const float scale = visualScaleFor (bounds);
        return bounds.withTrimmedLeft ((float) ui_contract::spectrumPlotLeftInset * scale)
                     .withTrimmedRight ((float) ui_contract::spectrumPlotRightInset * scale)
                     .withTrimmedTop ((float) ui_contract::spectrumPlotTopInset * scale)
                     .withTrimmedBottom ((float) ui_contract::spectrumPlotBottomInset * scale);
    }

    juce::Rectangle<float> dataPlotBoundsFor (juce::Rectangle<float> bounds) noexcept
    {
        const float scale = visualScaleFor (bounds);
        auto plot = plotBoundsFor (bounds);
        if (scale > 1.1f)
            plot.removeFromTop (18.0f * scale);
        return plot;
    }

    juce::Rectangle<float> readoutBoundsFor (juce::Rectangle<float> plot,
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

    juce::Rectangle<float> focusClearBoundsFor (juce::Rectangle<float> readout,
                                                float scale) noexcept
    {
        return readout.removeFromRight ((float) ui_contract::spectrumFocusClearWidth * scale);
    }
}

SpectrumComponent::SpectrumComponent()
{
    setInterceptsMouseClicks (true, false);
    setAccessible (false);
}

void SpectrumComponent::setSnapshot (const KirinSpectrumView& next)
{
    if (haveSnapshot && std::memcmp (&snapshot, &next, sizeof (snapshot)) == 0)
        return;
    const bool previousValid = haveSnapshot && validSnapshot (snapshot);
    const bool nextValid = validSnapshot (next);
    const bool layoutChanged = previousValid && nextValid
        && (snapshot.sample_rate != next.sample_rate
            || ! sameFloatBits (snapshot.min_hz, next.min_hz)
            || ! sameFloatBits (snapshot.max_hz, next.max_hz));
    if (! nextValid || layoutChanged)
        focusFrequencyHz = -1.0f;
    snapshot = next;
    haveSnapshot = true;
    if (validSnapshot (snapshot))
    {
        const auto calmWeights = spectrum_presentation::lowFrequencyCalmWeights<
            KIRIN_SPECTRUM_BAND_COUNT> (snapshot.min_hz, snapshot.max_hz);
        displayedPre = spectrum_presentation::calmLowFrequencies (
            snapshot.pre_dbfs, calmWeights);
        displayedPost = spectrum_presentation::calmLowFrequencies (
            snapshot.post_dbfs, calmWeights);
        displayedDelta = spectrum_presentation::calmLowFrequencies (
            snapshot.display_db, calmWeights);
    }
    else
    {
        displayedPre.fill (0.0f);
        displayedPost.fill (0.0f);
        displayedDelta.fill (0.0f);
    }
    repaint();
}

void SpectrumComponent::clearSnapshot()
{
    snapshot = {};
    displayedPre.fill (0.0f);
    displayedPost.fill (0.0f);
    displayedDelta.fill (0.0f);
    haveSnapshot = false;
    hoverNormalisedX = -1.0f;
    focusFrequencyHz = -1.0f;
    hoverNeedsRepaint = false;
    repaint();
}

void SpectrumComponent::presentationTick()
{
    if (! hoverNeedsRepaint)
        return;
    hoverNeedsRepaint = false;
    repaint();
}

void SpectrumComponent::mouseMove (const juce::MouseEvent& event)
{
    const auto plot = dataPlotBoundsFor (getLocalBounds().toFloat());
    const auto position = event.position;
    const float next = plot.contains (position)
                         ? juce::jlimit (0.0f, 1.0f,
                                        (position.x - plot.getX()) / plot.getWidth())
                         : -1.0f;
    if (juce::approximatelyEqual (next, hoverNormalisedX))
        return;
    hoverNormalisedX = next;
    hoverNeedsRepaint = true;
}

void SpectrumComponent::mouseExit (const juce::MouseEvent&)
{
    if (hoverNormalisedX < 0.0f)
        return;
    hoverNormalisedX = -1.0f;
    hoverNeedsRepaint = true;
}

void SpectrumComponent::mouseDown (const juce::MouseEvent& event)
{
    if (! haveSnapshot || ! validSnapshot (snapshot))
        return;
    const auto bounds = getLocalBounds().toFloat();
    const float scale = visualScaleFor (bounds);
    const auto outerPlot = plotBoundsFor (bounds);
    const auto plot = dataPlotBoundsFor (bounds);
    const bool expanded = scale > 1.1f;
    if (focusFrequencyHz > 0.0f)
    {
        auto readout = readoutBoundsFor (outerPlot, scale, expanded, true);
        if (focusClearBoundsFor (readout, scale).contains (event.position))
        {
            focusFrequencyHz = -1.0f;
            hoverNormalisedX = -1.0f;
            repaint();
            return;
        }
    }
    if (! plot.contains (event.position))
        return;
    const float normalised = juce::jlimit (0.0f, 1.0f,
        (event.position.x - plot.getX()) / plot.getWidth());
    focusFrequencyHz = frequencyForNormalisedX (
        normalised, snapshot.min_hz, snapshot.max_hz);
    hoverNormalisedX = normalised;
    repaint();
}

float SpectrumComponent::yForDeltaDb (float db, juce::Rectangle<float> plot) noexcept
{
    const float clipped = juce::jlimit (-kDeltaRangeDb, kDeltaRangeDb, db);
    return juce::jmap (clipped, kDeltaRangeDb, -kDeltaRangeDb,
                       plot.getY(), plot.getBottom());
}

float SpectrumComponent::xForFrequency (float hz, float minHz, float maxHz,
                                         juce::Rectangle<float> plot) noexcept
{
    const float clipped = juce::jlimit (minHz, maxHz, hz);
    const float position = std::log (clipped / minHz) / std::log (maxHz / minHz);
    return juce::jmap (position, 0.0f, 1.0f, plot.getX(), plot.getRight());
}

float SpectrumComponent::frequencyForNormalisedX (float position, float minHz,
                                                   float maxHz) noexcept
{
    return minHz * std::pow (maxHz / minHz, juce::jlimit (0.0f, 1.0f, position));
}

float SpectrumComponent::normalisedXForFrequency (float hz, float minHz,
                                                   float maxHz) noexcept
{
    const float clipped = juce::jlimit (minHz, maxHz, hz);
    return std::log (clipped / minHz) / std::log (maxHz / minHz);
}

juce::String SpectrumComponent::hoverFrequencyText (float hz)
{
    if (hz < 1'000.0f)
        return juce::String (juce::roundToInt (hz)) + " Hz";
    const int decimals = hz < 10'000.0f ? 2 : 1;
    return juce::String (hz / 1'000.0f, decimals) + " kHz";
}

juce::String SpectrumComponent::statusText (uint8_t status)
{
    if (status == KIRIN_SPECTRUM_NO_PAIR) return juce::CharPointer_UTF8 ("PAIR —");
    if (status == KIRIN_SPECTRUM_WARMING_UP) return juce::CharPointer_UTF8 ("SYNC ◌");
    if (status == KIRIN_SPECTRUM_UNAVAILABLE) return juce::CharPointer_UTF8 ("DATA —");
    return {};
}

void SpectrumComponent::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const float scale = visualScaleFor (bounds);
    const auto scaled = [scale] (float value) { return value * scale; };
    const auto scaledInt = [scale] (int value) { return juce::roundToInt ((float) value * scale); };
    const auto outerPlot = plotBoundsFor (bounds);
    const auto plot = dataPlotBoundsFor (bounds);
    const float minimumHz = haveSnapshot && snapshot.min_hz > 0.0f ? snapshot.min_hz : 10.0f;
    const float maximumHz = haveSnapshot && snapshot.max_hz > minimumHz ? snapshot.max_hz : 22'000.0f;
    const float zeroY = yForDeltaDb (0.0f, plot);

    g.setFont (monoFont (scaled (8.5f)));
    g.setColour (COL_MUTED.withAlpha (0.86f));
    g.drawText ("+18", 0, juce::roundToInt (plot.getY()) - scaledInt (4),
                scaledInt (21), scaledInt (10),
                juce::Justification::centredRight);
    g.drawText ("0", 0, juce::roundToInt (zeroY) - scaledInt (5),
                scaledInt (21), scaledInt (10),
                juce::Justification::centredRight);
    g.drawText ("-18", 0, juce::roundToInt (plot.getBottom()) - scaledInt (6),
                scaledInt (21), scaledInt (10),
                juce::Justification::centredRight);
    g.drawText ("0", juce::roundToInt (plot.getRight()) + scaledInt (3),
                juce::roundToInt (plot.getY()) - scaledInt (4),
                scaledInt (21), scaledInt (10),
                juce::Justification::centredLeft);
    g.drawText ("-48", juce::roundToInt (plot.getRight()) + scaledInt (3),
                juce::roundToInt (plot.getCentreY()) - scaledInt (5),
                scaledInt (21), scaledInt (10),
                juce::Justification::centredLeft);
    g.drawText ("-96", juce::roundToInt (plot.getRight()) + scaledInt (3),
                juce::roundToInt (plot.getBottom()) - scaledInt (6),
                scaledInt (21), scaledInt (10),
                juce::Justification::centredLeft);

    for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
    {
        const float y = yForDeltaDb (db, plot);
        g.setColour (COL_MUTED.withAlpha (0.18f));
        g.drawHorizontalLine (juce::roundToInt (y), plot.getX(), plot.getRight());
    }

    for (float hz : { 100.0f, 1'000.0f, 10'000.0f })
    {
        const float x = xForFrequency (hz, minimumHz, maximumHz, plot);
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
            const float x = xForFrequency (hz, minimumHz, maximumHz, plot);
            g.drawLine (x, plot.getY(), x, plot.getY() + tickLength, 1.0f);
            g.drawLine (x, plot.getBottom() - tickLength, x, plot.getBottom(), 1.0f);
        }
    }

    g.setColour (COL_MUTED.withAlpha (0.9f));
    g.drawText ("10", juce::roundToInt (plot.getX()),
                juce::roundToInt (plot.getBottom()) + scaledInt (1),
                scaledInt (30), scaledInt (10), juce::Justification::centredLeft);
    const float oneKhzX = xForFrequency (1'000.0f, minimumHz, maximumHz, plot);
    g.drawText ("1k", juce::roundToInt (oneKhzX) - scaledInt (15),
                juce::roundToInt (plot.getBottom()) + scaledInt (1),
                scaledInt (30), scaledInt (10),
                juce::Justification::centred);
    g.drawText ("22k", juce::roundToInt (plot.getRight()) - scaledInt (30),
                juce::roundToInt (plot.getBottom()) + scaledInt (1),
                scaledInt (30), scaledInt (10),
                juce::Justification::centredRight);
    if (scale > 1.125f)
    {
        const float hundredX = xForFrequency (100.0f, minimumHz, maximumHz, plot);
        const float tenKhzX = xForFrequency (10'000.0f, minimumHz, maximumHz, plot);
        g.setColour (COL_MUTED.withAlpha (0.72f));
        g.drawText ("100", juce::roundToInt (hundredX) - scaledInt (15),
                    juce::roundToInt (plot.getBottom()) + scaledInt (1),
                    scaledInt (30), scaledInt (10), juce::Justification::centred);
        g.drawText ("10k", juce::roundToInt (tenKhzX) - scaledInt (15),
                    juce::roundToInt (plot.getBottom()) + scaledInt (1),
                    scaledInt (30), scaledInt (10), juce::Justification::centred);
    }

    if (! haveSnapshot || ! validSnapshot (snapshot))
    {
        const auto text = haveSnapshot ? statusText (snapshot.status)
                                       : juce::String ("SYNC");
        if (text.isNotEmpty())
        {
            g.setColour (COL_MUTED);
            g.setFont (monoFont (scaled (13.0f)));
            g.drawText (text, plot, juce::Justification::centred);
        }
        return;
    }

    spectrum_painter::paintCurves (g, plot, scale,
                                   displayedPre, displayedPost, displayedDelta);

    const float probeNormalisedX = hoverNormalisedX >= 0.0f
        ? hoverNormalisedX
        : focusFrequencyHz > 0.0f
            ? normalisedXForFrequency (focusFrequencyHz, minimumHz, maximumHz)
            : -1.0f;
    if (! (scale > 1.1f && probeNormalisedX >= 0.0f))
    {
        const float legendTop = outerPlot.getY()
                              + scaled ((float) ui_contract::spectrumLegendTop);
        g.setFont (monoFont (scaled (ui_contract::spectrumLegendFontHeight)));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (ui_contract::spectrumDeltaLegendAlpha));
        g.drawText (juce::CharPointer_UTF8 ("\xCE\x94"),
                    juce::roundToInt (outerPlot.getX())
                        + scaledInt (ui_contract::spectrumDeltaLegendLabelX),
                    juce::roundToInt (legendTop),
                    scaledInt (ui_contract::spectrumDeltaLegendLabelWidth),
                    scaledInt (ui_contract::spectrumLegendHeight),
                    juce::Justification::centredLeft);
        g.setColour (COL_SPECTRUM_PRE.withAlpha (ui_contract::spectrumPreLegendAlpha));
        g.drawText ("PRE", juce::roundToInt (outerPlot.getX())
                             + scaledInt (ui_contract::spectrumPreLegendLabelX),
                    juce::roundToInt (legendTop),
                    scaledInt (ui_contract::spectrumPreLegendLabelWidth),
                    scaledInt (ui_contract::spectrumLegendHeight),
                    juce::Justification::centredLeft);
        g.setColour (COL_SPECTRUM_POST.withAlpha (ui_contract::spectrumPostLegendAlpha));
        g.drawText ("POST", juce::roundToInt (outerPlot.getX())
                              + scaledInt (ui_contract::spectrumPostLegendLabelX),
                    juce::roundToInt (legendTop),
                    scaledInt (ui_contract::spectrumPostLegendLabelWidth),
                    scaledInt (ui_contract::spectrumLegendHeight),
                    juce::Justification::centredLeft);
    }
    if (probeNormalisedX >= 0.0f)
    {
        const bool focusLocked = focusFrequencyHz > 0.0f;
        const bool expanded = scale > 1.1f;
        const float hoverX = juce::jmap (probeNormalisedX, plot.getX(), plot.getRight());
        const float bandPosition = probeNormalisedX
                                 * (float) (KIRIN_SPECTRUM_BAND_COUNT - 1u);
        const size_t lower = static_cast<size_t> (std::floor (bandPosition));
        const size_t upper = std::min (
            lower + 1u, static_cast<size_t> (KIRIN_SPECTRUM_BAND_COUNT - 1u));
        const float blend = bandPosition - (float) lower;
        const float preDbfs = juce::jmap (blend, displayedPre[lower],
                                         displayedPre[upper]);
        const float postDbfs = juce::jmap (blend, displayedPost[lower],
                                          displayedPost[upper]);
        const float deltaDb = juce::jmap (blend, displayedDelta[lower],
                                         displayedDelta[upper]);
        const float pointY = yForDeltaDb (deltaDb, plot);

        g.setColour (COL_NORMAL.withAlpha (focusLocked ? 0.48f : 0.30f));
        g.drawLine (hoverX, plot.getY(), hoverX, plot.getBottom(),
                    ui_contract::spectrumStrokeScale (scale)
                        * ui_contract::spectrumHoverLineWidth);
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.18f));
        g.fillEllipse (hoverX - scaled (3.5f), pointY - scaled (3.5f),
                       scaled (7.0f), scaled (7.0f));
        g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.98f));
        g.fillEllipse (hoverX - scaled (1.65f), pointY - scaled (1.65f),
                       scaled (3.3f), scaled (3.3f));

        const auto readout = readoutBoundsFor (outerPlot, scale, expanded, focusLocked);
        g.setColour (BG.brighter (0.10f).withAlpha (0.96f));
        g.fillRoundedRectangle (readout, scaled (ui_contract::spectrumHoverReadoutRadius));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.38f));
        g.drawRoundedRectangle (readout, scaled (ui_contract::spectrumHoverReadoutRadius),
                                scaled (0.75f));

        const float frequency = frequencyForNormalisedX (
            probeNormalisedX, minimumHz, maximumHz);
        const auto frequencyText = hoverFrequencyText (frequency);
        const auto deltaText = juce::String (deltaDb >= 0.0f ? "+" : "")
                             + juce::String (deltaDb, 1);
        const int textY = juce::roundToInt (readout.getY());
        g.setFont (monoFont (scaled (8.5f)));
        const auto drawReadoutText = [&] (const juce::String& text, juce::Colour colour,
                                          int logicalX, int logicalWidth,
                                          juce::Justification justification)
        {
            g.setColour (colour);
            g.drawText (text, juce::roundToInt (readout.getX()) + scaledInt (logicalX),
                        textY, scaledInt (logicalWidth),
                        scaledInt (ui_contract::spectrumHoverReadoutHeight), justification);
        };
        if (expanded)
        {
            drawReadoutText (frequencyText, COL_NORMAL.withAlpha (0.94f),
                             ui_contract::spectrumExpandedFrequencyX,
                             ui_contract::spectrumExpandedFrequencyWidth,
                             juce::Justification::centredLeft);
            drawReadoutText ("PRE " + juce::String (preDbfs, 1),
                             COL_SPECTRUM_PRE.withAlpha (0.98f),
                             ui_contract::spectrumExpandedPreX,
                             ui_contract::spectrumExpandedPreWidth,
                             juce::Justification::centredRight);
            drawReadoutText ("POST " + juce::String (postDbfs, 1),
                             COL_SPECTRUM_POST.withAlpha (0.98f),
                             ui_contract::spectrumExpandedPostX,
                             ui_contract::spectrumExpandedPostWidth,
                             juce::Justification::centredRight);
            drawReadoutText (juce::String (juce::CharPointer_UTF8 ("Δ")) + deltaText,
                             COL_SPECTRUM_DELTA_BR.withAlpha (0.98f),
                             ui_contract::spectrumExpandedDeltaX,
                             ui_contract::spectrumExpandedDeltaWidth,
                             juce::Justification::centredRight);
        }
        else
        {
            drawReadoutText (frequencyText, COL_NORMAL.withAlpha (0.94f),
                             ui_contract::spectrumHoverFrequencyX,
                             ui_contract::spectrumHoverFrequencyWidth,
                             juce::Justification::centredLeft);
            drawReadoutText (juce::String (juce::CharPointer_UTF8 ("Δ")) + deltaText,
                             COL_SPECTRUM_DELTA_BR.withAlpha (0.98f),
                             ui_contract::spectrumHoverDeltaX,
                             ui_contract::spectrumHoverDeltaWidth,
                             juce::Justification::centredRight);
        }
        if (focusLocked)
        {
            auto clearBounds = focusClearBoundsFor (readout, scale);
            g.setColour (COL_NORMAL.withAlpha (0.72f));
            g.drawText (juce::CharPointer_UTF8 ("×"), clearBounds.toNearestInt(),
                        juce::Justification::centred);
        }
    }
}
}
