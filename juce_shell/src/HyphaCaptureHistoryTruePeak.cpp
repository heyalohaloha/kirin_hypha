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
    const auto normalized = juce::jlimit (0.0, 1.0, (value - axisBottom) / (axisTop - axisBottom));
    return overlay.getBottom() - static_cast<float> (normalized) * overlay.getHeight();
}

void paintEvents (juce::Graphics& g,
                  juce::Rectangle<float> sharedPlot,
                  const std::vector<KirinMeterHistoryEntry>& history,
                  const time_history::HistoryAxis& axis,
                  const TruePeakSummary& summary,
                  double sampleRate)
{
    // A window whose TP stays at or below -1 dBTP adds nothing, not even the reference line.
    if (! summary.available || summary.eventIndices.empty())
        return;
    const auto overlay = overlayFor (sharedPlot);
    const auto baseline = overlay.getBottom();
    // 0 dBTP: the one reference line, so a stem reads as below or above it at a glance.
    g.setColour (COL_TRUE_PEAK.withAlpha (0.20f));
    g.drawHorizontalLine (juce::roundToInt (yFor (overlay, 0.0)), overlay.getX(), overlay.getRight());
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
        const auto colour = overZero ? COL_TRUE_PEAK_BR : COL_TRUE_PEAK;
        if (overZero)
        {
            g.setColour (colour.withAlpha (0.16f));
            g.drawLine (x, y, x, baseline, 4.0f);
        }
        g.setColour (colour.withAlpha (overZero ? 0.94f : 0.78f));
        g.drawLine (x, y, x, baseline, overZero ? 1.5f : 1.0f);
        // A short head marks the measured height, so a peak just above -1 is still seen.
        g.fillRect (x - 2.5f, y - 0.5f, 5.0f, 1.0f);
        if (value > axisTop)
        {
            // Beyond the axis: the stem rests on the top, capped, rather than leaving the plot.
            g.setColour (COL_TRUE_PEAK_BR);
            g.fillRect (x - 3.5f, overlay.getY(), 7.0f, 1.5f);
        }
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
    constexpr std::array<double, 5> ticks { 3.0, 2.0, 1.0, 0.0, -1.0 };
    const auto overlay = overlayFor (layout.sharedPlot);
    g.setFont (monoFont (presentation, typography::TextRole::axis,
                         typography::Composition::visualization));
    g.setColour (COL_TRUE_PEAK.withAlpha (0.86f));
    // Above the +3 label, which is centred on the overlay's top edge.
    text_style::drawText (g, "TP", layout.truePeakLabels.getX(),
                          juce::roundToInt (overlay.getY()) - 22,
                          layout.truePeakLabels.getWidth(), 14,
                          juce::Justification::centredLeft);
    for (const auto tick : ticks)
    {
        // A short overlay keeps the ends and 0; a tall one names every dB.
        if (overlay.getHeight() < 70.0f && (tick == 1.0 || tick == 2.0))
            continue;
        const auto y = juce::roundToInt (yFor (overlay, tick));
        g.setColour (COL_TRUE_PEAK.withAlpha (tick == 0.0 ? 0.42f : 0.20f));
        g.drawHorizontalLine (y, layout.sharedPlot.getRight() - 5.0f,
                              layout.sharedPlot.getRight());
        g.setColour (tick == 0.0 ? COL_TRUE_PEAK.withAlpha (0.86f) : COL_MUTED.withAlpha (0.72f));
        const auto label = tick > 0.0 ? "+" + juce::String (tick, 0) : juce::String (tick, 0);
        text_style::drawText (g, label, layout.truePeakLabels.getX(), y - 7,
                              layout.truePeakLabels.getWidth(), 14,
                              juce::Justification::centredLeft);
    }
}
}
