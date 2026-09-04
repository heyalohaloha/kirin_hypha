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
        const float range = ui_contract::spectrumFocusTrailRangeDb;
        const float clipped = juce::jlimit (-range, range,
                                             value);
        return juce::jmap (clipped,
                           range,
                          -range,
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

void paintEmptyPrompt (juce::Graphics& g,
                       juce::Rectangle<float> bounds,
                       float visualScale)
{
    g.setColour (COL_MUTED.brighter (0.10f).withAlpha (0.64f));
    g.setFont (monoFont (7.5f * ui_contract::analysisTextScale (visualScale)));
    g.drawText ("FOCUS TRAIL  /  CLICK A BAND", bounds.toNearestInt(),
                juce::Justification::centred);
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
        g.setFont (monoFont (7.0f * ui_contract::analysisTextScale (visualScale)));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.68f));
        g.drawText (juce::String (juce::CharPointer_UTF8 (
                        "\xCE\x94 \xC2\xB7 6s \xC2\xB7 \xC2\xB1\x31\x32")),
                    plot.removeFromTop (6.5f * visualScale),
                    juce::Justification::centredLeft);
    }
    if (plot.getHeight() < 3.0f)
        return;

    const float zeroY = yForDelta (0.0f, plot);
    g.setColour (COL_SPECTRUM_DELTA.withAlpha (compact ? 0.22f : 0.27f));
    g.drawLine (plot.getX(), zeroY, plot.getRight(), zeroY,
                0.65f * strokeScale);
    g.setColour (COL_MUTED.withAlpha (compact ? 0.10f : 0.13f));
    for (float guide : { -6.0f, 6.0f })
    {
        const float guideY = yForDelta (guide, plot);
        g.drawLine (plot.getX(), guideY, plot.getRight(), guideY,
                    0.45f * strokeScale);
    }

    const auto displayY = [&] (size_t index)
    {
        auto value = history.valueAt (index, normalisedBand);
        if (index > 0u && index + 1u < history.size()
            && ! history.hasGapBetween (index - 1u, index)
            && ! history.hasGapBetween (index, index + 1u))
        {
            value = 0.25f * history.valueAt (index - 1u, normalisedBand)
                  + 0.50f * value
                  + 0.25f * history.valueAt (index + 1u, normalisedBand);
        }
        return yForDelta (value, plot);
    };
    const float firstX = xForAge (history.ageSecondsAt (0u), plot);
    const float firstY = displayY (0u);
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
        const float previousY = displayY (previousIndex);
        const float currentY = displayY (index);
        const bool gap = history.hasGapBetween (previousIndex, index);
        if (gap)
            stroke.startNewSubPath (currentX, currentY);
        else
            stroke.lineTo (currentX, currentY);
        if (previousAge <= 1.5)
        {
            if (! glowStarted || gap)
            {
                recentGlow.startNewSubPath (gap ? currentX : previousX,
                                            gap ? currentY : previousY);
                glowStarted = true;
            }
            if (! gap)
                recentGlow.lineTo (currentX, currentY);
        }
        previousIndex = index;
    }
    const size_t newest = history.size() - 1u;
    const float newestX = xForAge (history.ageSecondsAt (newest), plot);
    const float newestY = displayY (newest);
    if (previousIndex != newest)
    {
        const bool gap = history.hasGapBetween (previousIndex, newest);
        if (gap) stroke.startNewSubPath (newestX, newestY);
        else stroke.lineTo (newestX, newestY);
        if (history.ageSecondsAt (previousIndex) <= 1.5)
        {
            if (! glowStarted || gap)
                recentGlow.startNewSubPath (
                    gap ? newestX : xForAge (history.ageSecondsAt (previousIndex), plot),
                    gap ? newestY : displayY (previousIndex));
            if (! gap)
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
