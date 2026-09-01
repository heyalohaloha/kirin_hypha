#include "HyphaObservatoryView.h"

#include <array>
#include <cmath>
#include <limits>

namespace hypha::observatory
{
namespace
{
juce::String valueText (double value, int decimals, bool signedValue)
{
    if (! std::isfinite (value))
        return "---";
    return (signedValue && value >= 0.0 ? "+" : "") + juce::String (value, decimals);
}

void drawPanel (juce::Graphics& g, juce::Rectangle<int> area, ExperienceFamily family)
{
    const auto opacity = family == ExperienceFamily::compactMeter ? 0.96f : 0.76f;
    g.setColour (BG.withAlpha (opacity));
    g.fillRoundedRectangle (area.toFloat(), 4.0f);
    g.setColour (COL_MUTED.withAlpha (0.34f));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), 4.0f, 1.0f);
}

void drawMetric (juce::Graphics& g,
                 juce::Rectangle<int> area,
                 const juce::String& label,
                 double value,
                 const juce::String& unit,
                 float valueHeight,
                 ExperienceFamily family,
                 bool signedValue = false,
                 int decimals = 1,
                 const juce::String& textOverride = {})
{
    drawPanel (g, area, family);
    const auto labelArea = area.removeFromTop (juce::jmax (14, area.getHeight() / 4));
    g.setColour (COL_MUTED);
    g.setFont (labelFont (juce::jlimit (9.0f, 12.0f, valueHeight * 0.32f)));
    g.drawText (label, labelArea.reduced (6, 1), juce::Justification::centredLeft);
    if (area.getWidth() < 180)
    {
        g.setFont (labelFont (juce::jlimit (7.0f, 10.0f, valueHeight * 0.23f)));
        g.drawText (unit, labelArea.reduced (6, 1), juce::Justification::centredRight);
        g.setColour (std::isfinite (value) && textOverride.isEmpty() ? COL_NORMAL : COL_MUTED);
        drawTabularText (g, monoFont (valueHeight),
                         textOverride.isNotEmpty() ? textOverride
                                                   : valueText (value, decimals, signedValue),
                         area.reduced (5, 0).toFloat(), juce::Justification::centred);
        return;
    }
    const auto unitWidth = juce::jmin (46, area.getWidth() / 3);
    const auto unitArea = area.removeFromRight (unitWidth);
    g.setColour (std::isfinite (value) && textOverride.isEmpty() ? COL_NORMAL : COL_MUTED);
    drawTabularText (g, monoFont (valueHeight),
                     textOverride.isNotEmpty() ? textOverride
                                               : valueText (value, decimals, signedValue),
                     area.reduced (5, 0).toFloat(), juce::Justification::centredRight);
    g.setColour (COL_MUTED);
    g.setFont (labelFont (juce::jlimit (8.0f, 11.0f, valueHeight * 0.28f)));
    g.drawText (unit, unitArea.reduced (2, 0), juce::Justification::centredLeft);
}

double optionValue (double value, bool available)
{
    return available && std::isfinite (value)
        ? value : std::numeric_limits<double>::quiet_NaN();
}
}

void View::paintLevel (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto& meter = observatoryFrame.meter;
    const auto& delta = observatoryFrame.delta;
    const bool currentAvailable = currentFactsAvailable();
    const bool cumulativeAvailable = cumulativeFactsAvailable();
    const auto density = currentPreset().density;
    const auto family = experienceFamily();
    const auto compact = family == ExperienceFamily::compactMeter;
    juce::Rectangle<int> channelStrips;
    if (target() == ObservationTarget::absolute
        && (density == Density::standard || density == Density::observatory))
        channelStrips = area.removeFromRight (
            density == Density::observatory ? 76 : 62).reduced (2);
    if (target() == ObservationTarget::delta)
    {
        const std::array<double, 3> values { delta.lufs, delta.lufs_s, delta.true_peak };
        const std::array<const char*, 3> labels { "M", "S", "TP" };
        for (int index = 0; index < 3; ++index)
            drawMetric (g, area.removeFromLeft (area.getWidth() / (3 - index)).reduced (2),
                        hypha::delta() + labels[(size_t) index],
                        optionValue (values[(size_t) index], deltaFactsAvailable()),
                        index == 2 ? "dB" : "LU", compact ? 27.0f : 36.0f,
                        family, true);
        return;
    }

    const auto mainHeight = compact ? area.getHeight()
                                    : juce::roundToInt (area.getHeight() * 0.58f);
    auto main = area.removeFromTop (mainHeight);
    if (compact)
    {
        const std::array<double, 3> compactValues {
            meter.lufs_m, meter.lufs_s, meter.true_peak
        };
        const std::array<const char*, 3> compactLabels { "M", "S", "TP" };
        const std::array<const char*, 3> compactUnits { "LUFS", "LUFS", "dBTP" };
        for (int index = 0; index < 3; ++index)
            drawMetric (g, main.removeFromLeft (main.getWidth() / (3 - index)).reduced (2),
                        compactLabels[(size_t) index],
                        optionValue (compactValues[(size_t) index], currentAvailable),
                        compactUnits[(size_t) index], 25.0f, family);
        return;
    }
    const std::array<double, 3> mainValues { meter.lufs_m, meter.lufs_s, meter.lufs_i };
    const std::array<const char*, 3> mainLabels { "M", "S", "I" };
    constexpr int mainCount = 3;
    for (int index = 0; index < mainCount; ++index)
        drawMetric (g, main.removeFromLeft (main.getWidth() / (mainCount - index)).reduced (2),
                    mainLabels[(size_t) index],
                    optionValue (mainValues[(size_t) index],
                                 index < 2 ? currentAvailable : cumulativeAvailable),
                    "LUFS", 42.0f, family);

    const std::array<double, 5> supportValues {
        meter.true_peak, meter.max_true_peak, meter.lra, meter.plr, meter.correlation
    };
    const std::array<bool, 5> supportAvailable {
        currentAvailable, cumulativeAvailable,
        cumulativeAvailable && observatoryFrame.lra_state == KIRIN_LRA_READY,
        cumulativeAvailable, currentAvailable
    };
    const std::array<const char*, 5> supportLabels { "TP", "MAX TP", "LRA", "PLR", "CORR" };
    const std::array<const char*, 5> supportUnits { "dBTP", "dBTP", "LU", "dB", "" };
    for (int index = 0; index < 5; ++index)
    {
        const auto warming = index == 2 && cumulativeAvailable
                          && observatoryFrame.lra_state == KIRIN_LRA_WARMING;
        const auto warmingText = warming
            ? "WARM " + juce::String ((int) std::floor (observatoryFrame.lra_elapsed_seconds)) + "S"
            : juce::String();
        drawMetric (g, area.removeFromLeft (area.getWidth() / (5 - index)).reduced (2),
                    supportLabels[(size_t) index],
                    optionValue (supportValues[(size_t) index], supportAvailable[(size_t) index]),
                    supportUnits[(size_t) index], 18.0f, family,
                    index == 4, index == 4 ? 2 : 1, warmingText);
    }
    if (! channelStrips.isEmpty())
        paintChannelStrips (g, channelStrips);
}
}
