#include "HyphaSpectrumComponent.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

namespace hypha
{
namespace
{
    constexpr float kDeltaRangeDb = KIRIN_SPECTRUM_DISPLAY_RANGE_DB;
    constexpr float kMagnitudeFloorDbfs = -96.0f;

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

    juce::Path makeCurve (const std::array<float, KIRIN_SPECTRUM_BAND_COUNT>& x,
                          const std::array<float, KIRIN_SPECTRUM_BAND_COUNT>& y)
    {
        juce::Path curve;
        curve.preallocateSpace (static_cast<int> (KIRIN_SPECTRUM_BAND_COUNT * 3u));
        curve.startNewSubPath (x.front(), y.front());
        for (size_t index = 1; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
            curve.lineTo (x[index], y[index]);
        return curve;
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
    snapshot = next;
    haveSnapshot = true;
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
    const auto plot = getLocalBounds().toFloat()
                          .withTrimmedLeft ((float) ui_contract::spectrumPlotLeftInset)
                          .withTrimmedRight ((float) ui_contract::spectrumPlotRightInset)
                          .withTrimmedTop ((float) ui_contract::spectrumPlotTopInset)
                          .withTrimmedBottom ((float) ui_contract::spectrumPlotBottomInset);
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

float SpectrumComponent::yForDeltaDb (float db, juce::Rectangle<float> plot) noexcept
{
    const float clipped = juce::jlimit (-kDeltaRangeDb, kDeltaRangeDb, db);
    return juce::jmap (clipped, kDeltaRangeDb, -kDeltaRangeDb,
                       plot.getY(), plot.getBottom());
}

float SpectrumComponent::yForMagnitudeDbfs (float dbfs,
                                             juce::Rectangle<float> plot) noexcept
{
    const float clipped = juce::jlimit (kMagnitudeFloorDbfs, 0.0f, dbfs);
    return juce::jmap (clipped, 0.0f, kMagnitudeFloorDbfs,
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
    const auto plot = bounds.withTrimmedLeft ((float) ui_contract::spectrumPlotLeftInset)
                            .withTrimmedRight ((float) ui_contract::spectrumPlotRightInset)
                            .withTrimmedTop ((float) ui_contract::spectrumPlotTopInset)
                            .withTrimmedBottom ((float) ui_contract::spectrumPlotBottomInset);
    const float minimumHz = haveSnapshot && snapshot.min_hz > 0.0f ? snapshot.min_hz : 10.0f;
    const float maximumHz = haveSnapshot && snapshot.max_hz > minimumHz ? snapshot.max_hz : 22'000.0f;
    const float zeroY = yForDeltaDb (0.0f, plot);

    g.setFont (monoFont (8.5f));
    g.setColour (COL_MUTED.withAlpha (0.86f));
    g.drawText ("+18", 0, juce::roundToInt (plot.getY()) - 4, 21, 10,
                juce::Justification::centredRight);
    g.drawText ("0", 0, juce::roundToInt (zeroY) - 5, 21, 10,
                juce::Justification::centredRight);
    g.drawText ("-18", 0, juce::roundToInt (plot.getBottom()) - 6, 21, 10,
                juce::Justification::centredRight);
    g.drawText ("0", juce::roundToInt (plot.getRight()) + 3,
                juce::roundToInt (plot.getY()) - 4, 21, 10,
                juce::Justification::centredLeft);
    g.drawText ("-48", juce::roundToInt (plot.getRight()) + 3,
                juce::roundToInt (plot.getCentreY()) - 5, 21, 10,
                juce::Justification::centredLeft);
    g.drawText ("-96", juce::roundToInt (plot.getRight()) + 3,
                juce::roundToInt (plot.getBottom()) - 6, 21, 10,
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

    g.setColour (COL_MUTED.withAlpha (0.9f));
    g.drawText ("10", juce::roundToInt (plot.getX()), juce::roundToInt (plot.getBottom()) + 1,
                30, 10, juce::Justification::centredLeft);
    const float oneKhzX = xForFrequency (1'000.0f, minimumHz, maximumHz, plot);
    g.drawText ("1k", juce::roundToInt (oneKhzX) - 15,
                juce::roundToInt (plot.getBottom()) + 1, 30, 10,
                juce::Justification::centred);
    g.drawText ("22k", juce::roundToInt (plot.getRight()) - 30,
                juce::roundToInt (plot.getBottom()) + 1, 30, 10,
                juce::Justification::centredRight);

    if (! haveSnapshot || ! validSnapshot (snapshot))
    {
        const auto text = haveSnapshot ? statusText (snapshot.status)
                                       : juce::String ("SYNC");
        if (text.isNotEmpty())
        {
            g.setColour (COL_MUTED);
            g.setFont (monoFont (13.0f));
            g.drawText (text, plot, juce::Justification::centred);
        }
        return;
    }

    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> x {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> preY {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> postY {};
    std::array<float, KIRIN_SPECTRUM_BAND_COUNT> deltaY {};
    for (size_t index = 0; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        x[index] = juce::jmap (static_cast<float> (index), 0.0f,
                               static_cast<float> (KIRIN_SPECTRUM_BAND_COUNT - 1u),
                               plot.getX(), plot.getRight());
        preY[index] = yForMagnitudeDbfs (snapshot.pre_dbfs[index], plot);
        postY[index] = yForMagnitudeDbfs (snapshot.post_dbfs[index], plot);
        deltaY[index] = yForDeltaDb (snapshot.display_db[index], plot);
    }

    const juce::Path preCurve = makeCurve (x, preY);
    const juce::Path postCurve = makeCurve (x, postY);
    const juce::Path deltaCurve = makeCurve (x, deltaY);

    constexpr size_t intensityLevelCount = 7u;
    std::array<juce::Path, intensityLevelCount> intensityTips;
    std::array<juce::Path, intensityLevelCount> highlights;
    const auto innerTipY = [&plot] (float db) {
        const float magnitudeDb = std::abs (db);
        const float tipDepthDb = std::min (3.0f, magnitudeDb * 0.38f);
        const float innerDb = std::copysign (magnitudeDb - tipDepthDb, db);
        return yForDeltaDb (innerDb, plot);
    };
    for (size_t index = 1; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
    {
        const float magnitude = 0.5f * (std::abs (snapshot.display_db[index - 1])
                                      + std::abs (snapshot.display_db[index]));
        const size_t bucket = std::min (intensityLevelCount - 1u,
                                        static_cast<size_t> (magnitude / 3.0f));
        if (bucket > 0u)
        {
            intensityTips[bucket].startNewSubPath (x[index - 1], deltaY[index - 1]);
            intensityTips[bucket].lineTo (x[index], deltaY[index]);
            intensityTips[bucket].lineTo (x[index], innerTipY (snapshot.display_db[index]));
            intensityTips[bucket].lineTo (x[index - 1],
                                           innerTipY (snapshot.display_db[index - 1]));
            intensityTips[bucket].closeSubPath();
        }
        highlights[bucket].startNewSubPath (x[index - 1], deltaY[index - 1]);
        highlights[bucket].lineTo (x[index], deltaY[index]);
    }

    g.setColour (COL_SPECTRUM_PRE.withAlpha (ui_contract::spectrumPreCurveAlpha));
    g.strokePath (preCurve, juce::PathStrokeType (ui_contract::spectrumPreStrokeWidth,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));
    g.setColour (COL_SPECTRUM_POST.withAlpha (ui_contract::spectrumPostGlowAlpha));
    g.strokePath (postCurve, juce::PathStrokeType (ui_contract::spectrumPostGlowStrokeWidth,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    g.setColour (COL_SPECTRUM_POST.withAlpha (ui_contract::spectrumPostCurveAlpha));
    g.strokePath (postCurve, juce::PathStrokeType (ui_contract::spectrumPostStrokeWidth,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

    juce::Path deltaFill;
    deltaFill.setUsingNonZeroWinding (false);
    deltaFill.startNewSubPath (x.front(), zeroY);
    deltaFill.lineTo (x.front(), deltaY.front());
    for (size_t index = 1; index < KIRIN_SPECTRUM_BAND_COUNT; ++index)
        deltaFill.lineTo (x[index], deltaY[index]);
    deltaFill.lineTo (x.back(), zeroY);
    deltaFill.closeSubPath();
    juce::ColourGradient fillGradient (COL_SPECTRUM_DELTA.withAlpha (0.29f),
                                       plot.getX(), plot.getY(),
                                       COL_SPECTRUM_DELTA.withAlpha (0.29f),
                                       plot.getX(), plot.getBottom(),
                                       false);
    fillGradient.addColour (0.5, COL_SPECTRUM_DELTA.withAlpha (0.05f));
    g.setGradientFill (fillGradient);
    g.fillPath (deltaFill);

    // A fact-derived tip ribbon adds density beside the Δ edge, never across the whole body.
    // It has no hold state or animation: every filled segment belongs to this exact snapshot.
    for (size_t bucket = 1; bucket < intensityTips.size(); ++bucket)
    {
        g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (
            ui_contract::spectrumTipAlpha[bucket]));
        g.fillPath (intensityTips[bucket]);
    }

    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.21f));
    g.drawLine (plot.getX(), zeroY, plot.getRight(), zeroY, 3.2f);
    g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.76f));
    g.drawLine (plot.getX(), zeroY, plot.getRight(), zeroY, 1.0f);

    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.17f));
    g.strokePath (deltaCurve, juce::PathStrokeType (4.6f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));
    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.94f));
    g.strokePath (deltaCurve, juce::PathStrokeType (2.15f, juce::PathStrokeType::curved,
                                                    juce::PathStrokeType::rounded));

    constexpr std::array<float, intensityLevelCount> highlightAlpha {
        0.10f, 0.24f, 0.38f, 0.53f, 0.68f, 0.82f, 0.94f
    };
    for (size_t bucket = 0; bucket < highlights.size(); ++bucket)
    {
        g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (highlightAlpha[bucket]));
        g.strokePath (highlights[bucket], juce::PathStrokeType (1.15f,
                                                               juce::PathStrokeType::curved,
                                                               juce::PathStrokeType::rounded));
    }

    const float legendTop = plot.getY() + (float) ui_contract::spectrumLegendTop;
    g.setFont (monoFont (ui_contract::spectrumLegendFontHeight));
    g.setColour (COL_SPECTRUM_PRE.withAlpha (ui_contract::spectrumPreLegendAlpha));
    g.drawText ("PRE", juce::roundToInt (plot.getX()) + ui_contract::spectrumPreLegendLabelX,
                juce::roundToInt (legendTop), ui_contract::spectrumPreLegendLabelWidth,
                ui_contract::spectrumLegendHeight,
                juce::Justification::centredLeft);
    g.setColour (COL_SPECTRUM_POST.withAlpha (ui_contract::spectrumPostLegendAlpha));
    g.drawText ("POST", juce::roundToInt (plot.getX()) + ui_contract::spectrumPostLegendLabelX,
                juce::roundToInt (legendTop), ui_contract::spectrumPostLegendLabelWidth,
                ui_contract::spectrumLegendHeight,
                juce::Justification::centredLeft);

    if (hoverNormalisedX >= 0.0f)
    {
        const float hoverX = juce::jmap (hoverNormalisedX, plot.getX(), plot.getRight());
        const float bandPosition = hoverNormalisedX
                                 * (float) (KIRIN_SPECTRUM_BAND_COUNT - 1u);
        const size_t lower = static_cast<size_t> (std::floor (bandPosition));
        const size_t upper = std::min (
            lower + 1u, static_cast<size_t> (KIRIN_SPECTRUM_BAND_COUNT - 1u));
        const float blend = bandPosition - (float) lower;
        const float deltaDb = juce::jmap (blend, snapshot.display_db[lower],
                                         snapshot.display_db[upper]);
        const float pointY = yForDeltaDb (deltaDb, plot);

        g.setColour (COL_NORMAL.withAlpha (0.30f));
        g.drawLine (hoverX, plot.getY(), hoverX, plot.getBottom(),
                    ui_contract::spectrumHoverLineWidth);
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.18f));
        g.fillEllipse (hoverX - 3.5f, pointY - 3.5f, 7.0f, 7.0f);
        g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.98f));
        g.fillEllipse (hoverX - 1.65f, pointY - 1.65f, 3.3f, 3.3f);

        const auto readout = juce::Rectangle<float> (
            plot.getRight() - (float) ui_contract::spectrumHoverReadoutWidth,
            plot.getY() + (float) ui_contract::spectrumHoverReadoutInset,
            (float) ui_contract::spectrumHoverReadoutWidth,
            (float) ui_contract::spectrumHoverReadoutHeight);
        g.setColour (BG.brighter (0.10f).withAlpha (0.96f));
        g.fillRoundedRectangle (readout, ui_contract::spectrumHoverReadoutRadius);
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.38f));
        g.drawRoundedRectangle (readout, ui_contract::spectrumHoverReadoutRadius, 0.75f);

        const float frequency = frequencyForNormalisedX (
            hoverNormalisedX, minimumHz, maximumHz);
        const auto frequencyText = hoverFrequencyText (frequency);
        const auto deltaText = juce::String (deltaDb >= 0.0f ? "+" : "")
                             + juce::String (deltaDb, 1);
        const int textY = juce::roundToInt (readout.getY());
        g.setFont (monoFont (8.5f));
        g.setColour (COL_NORMAL.withAlpha (0.94f));
        g.drawText (frequencyText,
                    juce::roundToInt (readout.getX()) + ui_contract::spectrumHoverFrequencyX,
                    textY, ui_contract::spectrumHoverFrequencyWidth,
                    ui_contract::spectrumHoverReadoutHeight,
                    juce::Justification::centredLeft);
        g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.98f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("Δ")) + deltaText,
                    juce::roundToInt (readout.getX()) + ui_contract::spectrumHoverDeltaX,
                    textY, ui_contract::spectrumHoverDeltaWidth,
                    ui_contract::spectrumHoverReadoutHeight,
                    juce::Justification::centredRight);
    }
}
}
