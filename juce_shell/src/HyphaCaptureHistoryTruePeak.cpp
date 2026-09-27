#include "HyphaCaptureHistoryTruePeak.h"

#include "HyphaHistoryInspection.h"
#include "HyphaTextStyle.h"
#include "HyphaTheme.h"

#include <array>
#include <cmath>

namespace hypha::capture_history::true_peak
{
juce::Rectangle<float> overlayFor (juce::Rectangle<float> sharedPlot) noexcept
{
    const auto maximumHeight = juce::jmax (1.0f, sharedPlot.getHeight() - 8.0f);
    const auto height = juce::jlimit (juce::jmin (48.0f, maximumHeight), maximumHeight,
                                      sharedPlot.getHeight() * 0.42f);
    return sharedPlot.withTop (sharedPlot.getBottom() - height);
}

float yFor (juce::Rectangle<float> overlay, double value) noexcept
{
    constexpr double minimum = -24.0;
    constexpr double maximum = 6.0;
    const auto normalized = juce::jlimit (0.0, 1.0, (value - minimum) / (maximum - minimum));
    return overlay.getBottom() - static_cast<float> (normalized) * overlay.getHeight();
}

void paintEvents (juce::Graphics& g,
                  juce::Rectangle<float> sharedPlot,
                  const std::vector<KirinMeterHistoryEntry>& history,
                  const time_history::HistoryAxis& axis,
                  const TruePeakSummary& summary,
                  double sampleRate)
{
    if (! summary.available)
        return;
    const auto overlay = overlayFor (sharedPlot);
    const auto baseline = overlay.getBottom();
    for (const auto index : summary.eventIndices)
    {
        if (index >= history.size())
            continue;
        const auto value = history[index].true_peak.max;
        if (! std::isfinite (value))
            continue;
        const auto x = sharedPlot.getX()
                     + static_cast<float> (normalizedHistoryX (
                           history, axis, history[index], index, sampleRate))
                       * sharedPlot.getWidth();
        const auto y = yFor (overlay, value);
        const bool overZero = history_inspection::strongPeak (value);
        const auto colour = overZero ? COL_FLORA_BR : COL_FLORA;
        if (overZero)
        {
            g.setColour (colour.withAlpha (0.16f));
            g.drawLine (x, y, x, baseline, 4.0f);
        }
        g.setColour (colour.withAlpha (overZero ? 0.94f : 0.68f));
        g.drawLine (x, y, x, baseline, overZero ? 1.5f : 0.75f);
        if (overZero)
        {
            g.setColour (colour.withAlpha (0.24f));
            g.fillEllipse (x - 4.0f, y - 4.0f, 8.0f, 8.0f);
            g.setColour (colour);
            g.fillEllipse (x - 1.7f, y - 1.7f, 3.4f, 3.4f);
        }
    }
}

void paintAxis (juce::Graphics& g, const Layout& layout, presentation::Context presentation)
{
    constexpr std::array<double, 5> ticks { 6.0, 0.0, -6.0, -12.0, -24.0 };
    const auto overlay = overlayFor (layout.sharedPlot);
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::visualization));
    g.setColour (COL_FLORA.withAlpha (0.72f));
    text_style::drawText (g, "TP", layout.truePeakLabels.getX(),
                          juce::roundToInt (overlay.getY()) - 16,
                          layout.truePeakLabels.getWidth(), 14,
                          juce::Justification::centredLeft);
    for (const auto tick : ticks)
    {
        if (overlay.getHeight() < 100.0f && std::abs (tick) > 0.01 && tick > -23.99)
            continue;
        const auto y = juce::roundToInt (yFor (overlay, tick));
        g.setColour (COL_FLORA.withAlpha (tick == 0.0 ? 0.28f : 0.16f));
        g.drawHorizontalLine (y, layout.sharedPlot.getRight() - 5.0f,
                              layout.sharedPlot.getRight());
        g.setColour (COL_MUTED.withAlpha (0.72f));
        const auto label = tick > 0.0 ? "+" + juce::String (tick, 0) : juce::String (tick, 0);
        text_style::drawText (g, label, layout.truePeakLabels.getX(), y - 7,
                              layout.truePeakLabels.getWidth(), 14,
                              juce::Justification::centredLeft);
    }
}
}
