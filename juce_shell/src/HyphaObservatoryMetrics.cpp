#include "HyphaObservatoryView.h"

#include "HyphaCaptureHistoryPainter.h"
#include "HyphaLevelMetricContract.h"

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

void drawPanel (juce::Graphics& g,
                juce::Rectangle<int> area,
                ExperienceFamily family,
                float opacityOverride = -1.0f)
{
    const auto opacity = opacityOverride >= 0.0f
        ? opacityOverride
        : family == ExperienceFamily::compactMeter ? 0.96f : 0.76f;
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
                 const juce::String& textOverride = {},
                 float panelOpacity = -1.0f,
                 const juce::String& auxiliaryText = {},
                 bool verticalStack = false)
{
    drawPanel (g, area, family, panelOpacity);
    if (verticalStack)
    {
        const auto labelHeight = juce::jlimit (11, 16, area.getHeight() / 4);
        const auto unitHeight = juce::jlimit (9, 13, area.getHeight() / 5);
        const auto labelArea = area.removeFromTop (labelHeight);
        const auto unitArea = area.removeFromBottom (unitHeight);
        g.setColour (COL_MUTED.brighter (0.08f));
        g.setFont (labelFont (juce::jlimit (8.0f, 11.0f, valueHeight * 0.25f)));
        g.drawText (label, labelArea.reduced (4, 0), juce::Justification::centred);
        g.setColour (std::isfinite (value) && textOverride.isEmpty()
                         ? COL_OBSERVATORY_VALUE : COL_MUTED);
        valueHeight = juce::jmin (valueHeight, (float) area.getHeight() * 0.84f);
        drawTabularText (g, monoFont (valueHeight),
                         textOverride.isNotEmpty() ? textOverride
                                                   : valueText (value, decimals, signedValue),
                         area.reduced (4, 0).toFloat(), juce::Justification::centred);
        g.setColour (COL_MUTED.brighter (0.04f));
        g.setFont (labelFont (juce::jlimit (7.0f, 9.5f, valueHeight * 0.23f)));
        g.drawText (unit, unitArea.reduced (3, 0), juce::Justification::centred);
        return;
    }
    const auto labelArea = area.removeFromTop (juce::jmax (14, area.getHeight() / 4));
    g.setColour (COL_MUTED);
    g.setFont (labelFont (juce::jlimit (9.0f, 12.0f, valueHeight * 0.32f)));
    g.drawText (label, labelArea.reduced (6, 1), juce::Justification::centredLeft);
    if (auxiliaryText.isNotEmpty())
    {
        const auto auxiliaryHeight = juce::jlimit (10, 14, area.getHeight() / 3);
        const auto auxiliaryArea = area.removeFromBottom (auxiliaryHeight);
        g.setColour (COL_MUTED.withAlpha (0.92f));
        g.setFont (labelFont (juce::jlimit (8.0f, 10.0f, valueHeight * 0.22f)));
        g.drawText (auxiliaryText, auxiliaryArea.reduced (6, 0),
                    juce::Justification::centredRight);
    }
    valueHeight = juce::jmin (valueHeight, (float) area.getHeight() * 0.92f);
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

void View::paintLevel (juce::Graphics& g, juce::Rectangle<int> area,
                       bool includeChannelStrips)
{
    const auto& meter = observatoryFrame.meter;
    const auto& delta = observatoryFrame.delta;
    const bool currentAvailable = currentFactsAvailable();
    const bool cumulativeAvailable = cumulativeFactsAvailable();
    const auto density = currentPreset().density;
    const auto family = experienceFamily();
    const auto compact = family == ExperienceFamily::compactMeter;
    juce::Rectangle<int> channelStrips;
    if (includeChannelStrips && target() == ObservationTarget::absolute
        && (density == Density::standard || isFullDensity (density)))
        channelStrips = area.removeFromRight (
            density == Density::inspection ? 156 : density == Density::observatory ? 112 : 62).reduced (2);
    if (compact)
        area.removeFromTop (20);
    if (target() == ObservationTarget::delta)
    {
        if (compact)
        {
            const std::array<double, 3> values {
                selectedShortTermLoudness ? delta.lufs_s : delta.lufs,
                delta.true_peak,
                delta.crest
            };
            const std::array<const char*, 3> labels {
                selectedShortTermLoudness ? "S" : "M", "TP", "CREST"
            };
            const std::array<const char*, 3> units { "LU", "dB", "dB" };
            for (int index = 0; index < 3; ++index)
                drawMetric (g, area.removeFromLeft (area.getWidth() / (3 - index)).reduced (2),
                            hypha::delta() + labels[(size_t) index],
                            optionValue (values[(size_t) index], deltaFactsAvailable()),
                            units[(size_t) index], 27.0f, family, true);
            return;
        }
        const std::array<double, 4> values {
            delta.lufs, delta.lufs_s, delta.true_peak, delta.crest
        };
        const std::array<const char*, 4> labels { "M", "S", "TP", "CREST" };
        const std::array<const char*, 4> units { "LU", "LU", "dB", "dB" };
        const auto valueHeight = density == Density::standard ? 30.0f : 36.0f;
        for (int index = 0; index < 4; ++index)
            drawMetric (g, area.removeFromLeft (area.getWidth() / (4 - index)).reduced (2),
                        hypha::delta() + labels[(size_t) index],
                        optionValue (values[(size_t) index], deltaFactsAvailable()),
                        units[(size_t) index], valueHeight, family, true);
        return;
    }

    const auto mainHeight = compact ? area.getHeight()
                                    : juce::roundToInt (area.getHeight() * 0.58f);
    auto main = area.removeFromTop (mainHeight);
    if (compact)
    {
        area = main;
        const auto& watch = compactShowsMaximum
            ? watchDisplay.maximum : watchDisplay.current;
        const auto compactFactsAvailable = watchDisplayAvailable && currentFactsAvailable();
        const bool trackStem = selectedMeterContext
                            == meter_context::MeterContext::trackStem;
        const std::array<double, 3> compactValues {
            watch.lufs_m,
            watch.lufs_s,
            trackStem ? watch.crest : meter.lufs_i
        };
        const std::array<bool, 3> compactAvailable {
            compactFactsAvailable,
            compactFactsAvailable,
            trackStem ? compactFactsAvailable : cumulativeAvailable
        };
        const std::array<const char*, 3> compactLabels {
            "M", "S", trackStem ? "CREST" : "I"
        };
        const std::array<const char*, 3> compactUnits {
            "LUFS", "LUFS", trackStem ? "dB" : "LUFS"
        };
        for (int index = 0; index < 3; ++index)
            drawMetric (g, area.removeFromLeft (area.getWidth() / (3 - index)).reduced (2),
                        compactLabels[(size_t) index],
                        optionValue (compactValues[(size_t) index],
                                     compactAvailable[(size_t) index]),
                        compactUnits[(size_t) index], 25.0f, family);
        return;
    }
    const bool trackStem = selectedMeterContext == meter_context::MeterContext::trackStem;
    const auto metricLayout = level_metrics::layoutFor (trackStem);
    const std::array<double, 3> mainValues {
        meter.lufs_m, meter.lufs_s,
        trackStem ? watchDisplay.current.crest : meter.lufs_i
    };
    const std::array<bool, 3> mainAvailable {
        currentAvailable, currentAvailable,
        trackStem ? currentAvailable && watchDisplayAvailable : cumulativeAvailable
    };
    const std::array<const char*, 3> mainUnits {
        "LUFS", "LUFS", trackStem ? "dB" : "LUFS"
    };
    const auto mainValueHeight = density == Density::standard ? 28.0f
                               : getWidth() >= 900 ? 58.0f : 42.0f;
    constexpr int mainCount = 3;
    if (isFullDensity (density))
        background.drawLevelCorners (g, main, worldState());
    for (int index = 0; index < mainCount; ++index)
    {
        drawMetric (g, main.removeFromLeft (main.getWidth() / (mainCount - index)).reduced (2),
                    level_metrics::label (metricLayout.main[(size_t) index]),
                    optionValue (mainValues[(size_t) index], mainAvailable[(size_t) index]),
                    mainUnits[(size_t) index], mainValueHeight, family,
                    false, 1, {}, isFullDensity (density) ? 0.42f : -1.0f,
                    {}, isFullDensity (density));
    }

    const std::array<double, 5> supportValues {
        trackStem ? watchDisplay.current.psr : meter.true_peak,
        trackStem ? meter.true_peak : meter.max_true_peak,
        trackStem ? meter.max_true_peak : meter.lra,
        trackStem ? meter.lufs_i : meter.plr,
        trackStem ? meter.lra : watchDisplay.current.crest
    };
    const std::array<bool, 5> supportAvailable {
        trackStem ? currentAvailable && watchDisplayAvailable : currentAvailable,
        trackStem ? currentAvailable : cumulativeAvailable,
        trackStem ? cumulativeAvailable
                  : cumulativeAvailable && observatoryFrame.lra_state == KIRIN_LRA_READY,
        cumulativeAvailable,
        trackStem ? cumulativeAvailable && observatoryFrame.lra_state == KIRIN_LRA_READY
                  : currentAvailable && watchDisplayAvailable
    };
    const std::array<const char*, 5> supportUnits {
        trackStem ? "dB" : "dBTP",
        "dBTP",
        trackStem ? "dBTP" : "LU",
        trackStem ? "LUFS" : "dB",
        trackStem ? "LU" : "dB"
    };
    constexpr int supportCount = 5;
    for (int index = 0; index < supportCount; ++index)
    {
        const auto lraIndex = trackStem ? 4 : 2;
        const auto warming = index == lraIndex && cumulativeAvailable
                          && observatoryFrame.lra_state == KIRIN_LRA_WARMING;
        const auto warmingText = warming
            ? "WARM " + juce::String ((int) std::floor (observatoryFrame.lra_elapsed_seconds)) + "S"
            : juce::String();
        drawMetric (g, area.removeFromLeft (
                        area.getWidth() / (supportCount - index)).reduced (2),
                    level_metrics::label (metricLayout.support[(size_t) index]),
                    optionValue (supportValues[(size_t) index], supportAvailable[(size_t) index]),
                    supportUnits[(size_t) index], getWidth() >= 900 ? 24.0f : 18.0f, family,
                    false, 1, warmingText,
                    isFullDensity (density) ? 0.54f : -1.0f, {},
                    isFullDensity (density));
    }
    if (! channelStrips.isEmpty())
        paintChannelStrips (g, channelStrips);
}

void View::paintLevelWithHistory (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto inspection = getWidth() >= 900;
    juce::Rectangle<int> channelStrips;
    if (target() == ObservationTarget::absolute)
        channelStrips = area.removeFromRight (inspection ? 110 : 76).reduced (2);

    const auto landscape = area.getWidth() > area.getHeight();
    const auto previousHistoryHeight = juce::jlimit (
        72, inspection ? 240 : 170,
        juce::roundToInt (area.getHeight()
                          * (inspection ? 0.46f : landscape ? 0.40f : 0.32f)));
    const auto previousMetricsHeight = juce::jmax (
        1, area.getHeight() - previousHistoryHeight - 4);
    const auto metricsHeight = compressedLevelMetricsHeight (previousMetricsHeight);
    auto metricsArea = area.removeFromTop (metricsHeight);
    area.removeFromTop (4);
    auto historyArea = area;
    paintLevel (g, metricsArea, false);
    levelHistoryArea = historyArea.reduced (2);
    const auto maximumMomentary = target() == ObservationTarget::absolute
                               && cumulativeFactsAvailable()
                               && std::isfinite (observatoryFrame.meter.max_lufs_m)
        ? juce::String ("MAX M ") + juce::String (observatoryFrame.meter.max_lufs_m, 1) + " LUFS"
        : juce::String();
    capture_history::paint (g, levelHistoryArea, history,
                            target() == ObservationTarget::delta,
                            static_cast<double> (observatoryFrame.meter.sample_rate),
                            captureFrame ? std::nullopt : hoveredLevelHistoryIndex,
                            maximumMomentary);
    if (! channelStrips.isEmpty())
        paintChannelStrips (g, channelStrips);
}
}
