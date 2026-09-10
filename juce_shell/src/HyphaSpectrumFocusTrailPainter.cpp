#include "HyphaSpectrumFocusTrailPainter.h"

#include "HyphaSpectrumUiContract.h"
#include "HyphaTheme.h"
#include "HyphaPolylineGeometry.h"

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
                       presentation::Context presentation)
{
    g.setColour (COL_MUTED.brighter (0.10f).withAlpha (0.64f));
    g.setFont (monoFont (presentation, typography::TextRole::status,
                         typography::Composition::visualization));
    g.drawText ("FOCUS TRAIL  /  CLICK A BAND", bounds.toNearestInt(),
                juce::Justification::centred);
}

void paint (juce::Graphics& g,
            juce::Rectangle<float> bounds,
            float visualScale,
            const spectrum_focus::FocusTrailHistory& history,
            float normalisedBand,
            bool compact,
            presentation::Context presentation)
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
        g.setFont (monoFont (presentation, typography::TextRole::legend,
                             typography::Composition::visualization));
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.68f));
        g.drawText (juce::String (juce::CharPointer_UTF8 (
                        "\xCE\x94 \xC2\xB7 6s \xC2\xB7 \xC2\xB1\x31\x32")),
                    plot.removeFromTop (juce::jmax (14.0f, 7.0f * visualScale)),
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
    std::array<float, spectrum_focus::focusTrailCapacity> x {}, y {};
    std::array<double, spectrum_focus::focusTrailCapacity> age {};
    for (size_t i = 0; i < history.size(); ++i)
    {
        age[i] = history.ageSecondsAt (i);
        x[i] = xForAge (age[i], plot);
        y[i] = displayY (i);
    }
    juce::Path stroke, recentGlow;
    const auto appendRun = [&] (juce::Path& path, size_t first, size_t last)
    {
        const auto keep = polyline_geometry::retainedVertices (x, y, first, last);
        path.startNewSubPath (x[first], y[first]);
        for (size_t i = first + 1; i <= last; ++i)
            if (keep[i]) path.lineTo (x[i], y[i]);
    };
    // Simplify only within continuous runs, at a bounded subpixel error. Unlike a fixed stride,
    // this keeps narrow excursions and the two sides of every missing-data gap at every size.
    for (size_t first = 0; first < history.size();)
    {
        size_t last = first;
        while (last + 1 < history.size() && x[last + 1] > x[last]
               && ! history.hasGapBetween (last, last + 1))
            ++last;
        appendRun (stroke, first, last);
        if (! compact)
        {
            size_t recent = first;
            while (recent <= last && age[recent] > 1.5) ++recent;
            if (recent <= last) appendRun (recentGlow, recent, last);
        }
        first = last + 1;
    }
    const size_t newest = history.size() - 1u;
    const float newestX = x[newest], newestY = y[newest];
    if (! compact && ! recentGlow.isEmpty())
    {
        g.setColour (COL_SPECTRUM_DELTA.withAlpha (0.09f));
        g.strokePath (recentGlow, juce::PathStrokeType (
            3.0f * strokeScale, juce::PathStrokeType::curved,
            juce::PathStrokeType::rounded));
    }
    juce::ColourGradient strokeGradient (
        COL_SPECTRUM_DELTA.withAlpha (0.55f), plot.getX(), zeroY,
        COL_SPECTRUM_DELTA_BR.withAlpha (0.98f), plot.getRight(), zeroY, false);
    strokeGradient.addColour (0.42, COL_SPECTRUM_DELTA.withAlpha (0.62f));
    strokeGradient.addColour (0.68, COL_SPECTRUM_DELTA.withAlpha (0.70f));
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
