#include "HyphaSpectrumFocusTrailPainter.h"

#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"

#include <algorithm>

namespace hypha::spectrum_focus_painter
{
namespace
{
    float yForDelta (float value, juce::Rectangle<float> plot) noexcept
    {
        const float clipped = juce::jlimit (-KIRIN_SPECTRUM_DISPLAY_RANGE_DB,
                                             KIRIN_SPECTRUM_DISPLAY_RANGE_DB,
                                             value);
        return juce::jmap (clipped,
                           KIRIN_SPECTRUM_DISPLAY_RANGE_DB,
                          -KIRIN_SPECTRUM_DISPLAY_RANGE_DB,
                           plot.getY(), plot.getBottom());
    }

    float xForAge (double ageSeconds, juce::Rectangle<float> plot) noexcept
    {
        const double position = 1.0 - std::clamp (
            ageSeconds / static_cast<double> (spectrum_focus::focusTrailSeconds),
            0.0, 1.0);
        return juce::jmap (static_cast<float> (position),
                           plot.getX(), plot.getRight());
    }
}

void paint (juce::Graphics& g,
            juce::Rectangle<float> bounds,
            float visualScale,
            const spectrum_focus::FocusTrailHistory& history,
            float normalisedBand,
            bool compact)
{
    if (history.empty() || bounds.isEmpty())
        return;

    const float strokeScale = ui_contract::spectrumStrokeScale (visualScale);
    const float radius = ui_contract::spectrumFocusTrailRadius * strokeScale;
    g.setColour (BG.darker (0.18f).withAlpha (compact ? 0.84f : 0.72f));
    g.fillRoundedRectangle (bounds, radius);
    g.setColour (COL_SPECTRUM_DELTA.withAlpha (compact ? 0.13f : 0.17f));
    g.drawRoundedRectangle (bounds, radius, 0.65f * strokeScale);

    auto plot = bounds.reduced (3.0f * strokeScale, 2.0f * strokeScale);
    if (! compact)
    {
        g.setFont (monoFont (7.0f * visualScale));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.68f));
        g.drawText (juce::String (juce::CharPointer_UTF8 ("\xCE\x94 \xC2\xB7 6s")),
                    plot.removeFromTop (6.5f * visualScale),
                    juce::Justification::centredLeft);
    }
    if (plot.getHeight() < 3.0f)
        return;

    const float zeroY = yForDelta (0.0f, plot);
    g.setColour (COL_SPECTRUM_DELTA.withAlpha (compact ? 0.22f : 0.27f));
    g.drawLine (plot.getX(), zeroY, plot.getRight(), zeroY,
                0.65f * strokeScale);

    const float firstX = xForAge (history.ageSecondsAt (0u), plot);
    const float firstY = yForDelta (history.valueAt (0u, normalisedBand), plot);
    juce::Path stroke;
    stroke.startNewSubPath (firstX, firstY);
    juce::Path recentGlow;
    bool glowStarted = false;
    // At 100% the lane is narrower than the 180 retained points. Painting every third exact
    // observation preserves more points than there are useful horizontal pixels while avoiding
    // redundant anti-aliased segments on Windows' software renderer. Storage and endpoints stay
    // exact; this is pixel-aware presentation decimation, not smoothing or interpolation.
    const size_t paintStride = compact ? 3u : 1u;
    size_t previousIndex = 0u;
    for (size_t index = paintStride; index < history.size(); index += paintStride)
    {
        const double previousAge = history.ageSecondsAt (previousIndex);
        const double currentAge = history.ageSecondsAt (index);
        const float previousX = xForAge (previousAge, plot);
        const float currentX = xForAge (currentAge, plot);
        if (currentX <= previousX)
            continue;
        const float previousY = yForDelta (
            history.valueAt (previousIndex, normalisedBand), plot);
        const float currentY = yForDelta (
            history.valueAt (index, normalisedBand), plot);
        const bool observationGap = history.hasGapBetween (previousIndex, index);
        if (observationGap)
        {
            stroke.startNewSubPath (currentX, currentY);
            if (currentAge <= 1.5)
            {
                recentGlow.startNewSubPath (currentX, currentY);
                glowStarted = true;
            }
        }
        else
            stroke.lineTo (currentX, currentY);
        if (! observationGap && previousAge <= 1.5)
        {
            if (! glowStarted)
            {
                recentGlow.startNewSubPath (previousX, previousY);
                glowStarted = true;
            }
            recentGlow.lineTo (currentX, currentY);
        }
        previousIndex = index;
    }
    const size_t newest = history.size() - 1u;
    const float newestX = xForAge (history.ageSecondsAt (newest), plot);
    const float newestY = yForDelta (history.valueAt (newest, normalisedBand), plot);
    if (previousIndex != newest)
    {
        const bool observationGap = history.hasGapBetween (previousIndex, newest);
        if (observationGap)
        {
            stroke.startNewSubPath (newestX, newestY);
            recentGlow.startNewSubPath (newestX, newestY);
            glowStarted = true;
        }
        else
            stroke.lineTo (newestX, newestY);
        if (! observationGap && history.ageSecondsAt (previousIndex) <= 1.5)
        {
            if (! glowStarted)
                recentGlow.startNewSubPath (
                    xForAge (history.ageSecondsAt (previousIndex), plot),
                    yForDelta (history.valueAt (previousIndex, normalisedBand), plot));
            recentGlow.lineTo (newestX, newestY);
        }
    }
    if (! compact && ! recentGlow.isEmpty())
    {
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.09f));
        g.strokePath (recentGlow, juce::PathStrokeType (
            3.0f * strokeScale, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }
    juce::ColourGradient strokeGradient (
        COL_SPECTRUM_DELTA.withAlpha (0.10f), plot.getX(), zeroY,
        COL_SPECTRUM_DELTA_BR.withAlpha (0.98f), plot.getRight(), zeroY, false);
    strokeGradient.addColour (0.42, COL_SPECTRUM_DELTA.withAlpha (0.22f));
    strokeGradient.addColour (0.68, COL_SPECTRUM_DELTA.withAlpha (0.48f));
    strokeGradient.addColour (0.86, COL_SPECTRUM_DELTA_BR.withAlpha (0.74f));
    strokeGradient.addColour (0.95, COL_SPECTRUM_DELTA_BR.withAlpha (0.90f));
    g.setGradientFill (strokeGradient);
    g.strokePath (stroke, juce::PathStrokeType (
        ui_contract::spectrumFocusTrailStrokeWidth * strokeScale,
        juce::PathStrokeType::curved,
        juce::PathStrokeType::rounded));

    g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.18f));
    g.fillEllipse (newestX - 2.8f * strokeScale, newestY - 2.8f * strokeScale,
                   5.6f * strokeScale, 5.6f * strokeScale);
    g.setColour (COL_SPECTRUM_DELTA_BR.withAlpha (0.98f));
    g.fillEllipse (newestX - 1.25f * strokeScale, newestY - 1.25f * strokeScale,
                   2.5f * strokeScale, 2.5f * strokeScale);
}
}
