#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "HyphaPresentationContext.h"
#include "HyphaTheme.h"
#include "HyphaTextStyle.h"

#include <cmath>
#include <vector>

// 2026-10-04（Daisuke「どこが何 Hz なのか多少表示した方が親切」）：Reference のスペクトルの図（B の Balance、
// C の SPECTRUM／LOW FREQUENCY、V の Check のタブ）の周波数の目盛り（薄い縦線と数字）。横軸はどの図も
// minimumHz〜maximumHz の対数。曲線より先に描く（線の下に敷く）。
namespace hypha::reference_ui
{
inline void paintFrequencyTicks (juce::Graphics& g, juce::Rectangle<float> chart, double minimumHz, double maximumHz,
                                 const presentation::Context& context)
{
    if (minimumHz <= 0.0 || maximumHz <= minimumHz || chart.getWidth() < 80.0f || chart.getHeight() < 40.0f) return;
    const std::vector<double> ticks = maximumHz <= 1'000.0
        ? std::vector<double> { 30.0, 50.0, 100.0, 200.0 }
        : std::vector<double> { 50.0, 100.0, 200.0, 500.0, 1'000.0, 2'000.0, 5'000.0, 10'000.0 };
    g.setFont (labelFont (context, typography::TextRole::unit, typography::Composition::visualization));
    for (const auto hz : ticks)
    {
        if (hz <= minimumHz || hz >= maximumHz) continue;
        const auto x = chart.getX() + static_cast<float> (std::log (hz / minimumHz) / std::log (maximumHz / minimumHz)) * chart.getWidth();
        g.setColour (COL_MUTED.withAlpha (0.10f));
        g.drawVerticalLine (juce::roundToInt (x), chart.getY(), chart.getBottom() - 14.0f);
        g.setColour (COL_TEXT_TERTIARY.withAlpha (0.85f));
        const auto label = hz >= 1'000.0 ? juce::String (juce::roundToInt (hz / 1'000.0)) + "k" : juce::String (juce::roundToInt (hz));
        text_style::drawText (g, label, juce::Rectangle<float> (x - 18.0f, chart.getBottom() - 13.0f, 36.0f, 13.0f),
                              juce::Justification::centred, false);
    }
}
}
