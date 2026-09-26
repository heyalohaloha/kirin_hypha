#include "HyphaObservatoryView.h"

#include "HyphaCaptureHistoryPainter.h"
#include "HyphaChannelReadoutLayout.h"
#include "HyphaComparisonPresentation.h"
#include "HyphaLevelMetricContract.h"
#include "HyphaSurfaceMaterial.h"
#include "HyphaTextStyle.h"

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
    surface_material::paintPanel (g, area.toFloat(), opacity);
}

void drawMetric (juce::Graphics& g,
                 juce::Rectangle<int> area,
                 const juce::String& label,
                 double value,
                 const juce::String& unit,
                 ExperienceFamily family,
                 presentation::Context presentation,
                 bool signedValue = false,
                 int decimals = 1,
                 const juce::String& textOverride = {},
                 float panelOpacity = -1.0f,
                 const juce::String& auxiliaryText = {},
                 bool verticalStack = false,
                 typography::TextRole valueRole = typography::TextRole::primaryValue)
{
    drawPanel (g, area, family, panelOpacity);
    if (verticalStack)
    {
        const auto labelHeight = juce::jlimit (14, 18, area.getHeight() / 4);
        const auto unitHeight = juce::jlimit (14, 16, area.getHeight() / 5);
        const auto labelArea = area.removeFromTop (labelHeight);
        const auto unitArea = area.removeFromBottom (textOverride.isEmpty() ? unitHeight : 0);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                              typography::Composition::facts));
        g.drawText (label, labelArea.reduced (4, 0), juce::Justification::centred);
        g.setColour (std::isfinite (value) && textOverride.isEmpty()
                         ? COL_OBSERVATORY_VALUE : COL_MUTED);
        drawTabularText (g, monoFont (presentation, valueRole,
                                      typography::Composition::facts),
                         textOverride.isNotEmpty() ? textOverride
                                                   : valueText (value, decimals, signedValue),
                         area.reduced (4, 0).toFloat(), juce::Justification::centred);
        g.setColour (COL_TEXT_TERTIARY);
        g.setFont (labelFont (presentation, typography::TextRole::unit,
                              typography::Composition::facts));
        if (textOverride.isEmpty())
            g.drawText (unit, unitArea.reduced (3, 0), juce::Justification::centred);
        return;
    }
    const auto labelArea = area.removeFromTop (juce::jmax (14, area.getHeight() / 4));
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentation, typography::TextRole::metricLabel,
                          typography::Composition::facts));
    g.drawText (label, labelArea.reduced (6, 1), juce::Justification::centredLeft);
    if (auxiliaryText.isNotEmpty())
    {
        const auto auxiliaryHeight = juce::jlimit (10, 14, area.getHeight() / 3);
        const auto auxiliaryArea = area.removeFromBottom (auxiliaryHeight);
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (labelFont (presentation, typography::TextRole::status,
                              typography::Composition::facts));
        g.drawText (auxiliaryText, auxiliaryArea.reduced (6, 0),
                    juce::Justification::centredRight);
    }
    if (area.getWidth() < 180)
    {
        g.setFont (labelFont (presentation, typography::TextRole::unit,
                              typography::Composition::facts));
        g.drawText (unit, labelArea.reduced (6, 1), juce::Justification::centredRight);
        g.setColour (std::isfinite (value) && textOverride.isEmpty() ? COL_NORMAL : COL_MUTED);
        drawTabularText (g, monoFont (presentation, valueRole,
                                      typography::Composition::facts),
                         textOverride.isNotEmpty() ? textOverride
                                                   : valueText (value, decimals, signedValue),
                         area.reduced (5, 0).toFloat(), juce::Justification::centred);
        return;
    }
    const auto unitWidth = juce::jmin (46, area.getWidth() / 3);
    const auto unitArea = area.removeFromRight (unitWidth);
    g.setColour (std::isfinite (value) && textOverride.isEmpty() ? COL_NORMAL : COL_MUTED);
    drawTabularText (g, monoFont (presentation, valueRole,
                                  typography::Composition::facts),
                     textOverride.isNotEmpty() ? textOverride
                                               : valueText (value, decimals, signedValue),
                     area.reduced (5, 0).toFloat(), juce::Justification::centredRight);
    g.setColour (COL_TEXT_TERTIARY);
    g.setFont (labelFont (presentation, typography::TextRole::unit,
                          typography::Composition::facts));
    g.drawText (unit, unitArea.reduced (2, 0), juce::Justification::centredLeft);
}

double optionValue (double value, bool available)
{
    return available && std::isfinite (value)
        ? value : std::numeric_limits<double>::quiet_NaN();
}
}

void View::paintRecordDisplay (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto family = experienceFamily();
    const auto context = presentationContext();
    const bool hasMeasure = recordDisplay.has_measure != 0u;
    const bool hasSession = recordDisplay.has_session != 0u;
    const bool hasDelta = role == Role::post
        && target() == ObservationTarget::delta
        && recordDisplay.has_delta != 0u
        && recordDisplay.pair_matches_current != 0u
        && recordDisplay.delta.mode == KIRIN_DELTA_MODE_ACTIVE;
    const bool shortTerm = selectedShortTermLoudness;

    auto statusArea = area.removeFromTop (juce::jlimit (20, 28, area.getHeight() / 5));
    area.removeFromTop (3);
    drawPanel (g, statusArea, family);
    const auto phaseText = recordDisplay.phase == KIRIN_RECORD_DISPLAY_FINALIZING
        ? juce::String ("RECORD FINALIZING")
        : recordDisplay.phase == KIRIN_RECORD_DISPLAY_UNAVAILABLE
            ? juce::String ("RECORD UNAVAILABLE")
            : juce::String ("RECORD RESULT");
    const auto sourceText = hasDelta ? juce::String (juce::CharPointer_UTF8 (" · POST − PRE"))
                                     : juce::String (" · ABSOLUTE");
    g.setColour (recordDisplay.phase == KIRIN_RECORD_DISPLAY_UNAVAILABLE
                     ? COL_MUTED : COL_NORMAL);
    g.setFont (monoFont (context, typography::TextRole::status));
    text_style::drawEllipsized (g, phaseText + sourceText, statusArea.reduced (5, 1),
                                juce::Justification::centred);

    const auto& measure = recordDisplay.measure;
    const auto& session = recordDisplay.session;
    const auto& delta = recordDisplay.delta;
    const std::array<juce::String, 6> labels {
        hasDelta ? hypha::delta() + (shortTerm ? "S" : "M") : (shortTerm ? "S" : "M"),
        hasDelta ? hypha::delta() + "PSR" : "PSR",
        "MAX TP", "I",
        hasDelta ? hypha::delta() + "CREST" : "CREST",
        hasDelta ? hypha::delta() + "SHARP" : "SHARP"
    };
    const std::array<double, 6> values {
        hasDelta ? (shortTerm ? delta.lufs_s : delta.lufs)
                 : (shortTerm ? measure.lufs_s : measure.lufs_m),
        hasDelta ? delta.psr : measure.psr,
        session.max_true_peak,
        session.lufs_i,
        hasDelta ? delta.crest : measure.crest,
        hasDelta ? delta.sharpness : measure.sharpness
    };
    const std::array<bool, 6> available {
        hasDelta || hasMeasure,
        hasDelta || hasMeasure,
        hasSession,
        hasSession,
        hasDelta || hasMeasure,
        hasDelta || hasMeasure
    };
    const std::array<const char*, 6> units {
        hasDelta ? "LU" : "LUFS", "dB", "dBTP", "LUFS", "dB", "acum"
    };

    auto top = area.removeFromTop ((area.getHeight() - 3) / 2);
    area.removeFromTop (3);
    for (int index = 0; index < 6; ++index)
    {
        auto& row = index < 3 ? top : area;
        const auto remaining = 3 - (index % 3);
        auto cell = row.removeFromLeft (row.getWidth() / remaining).reduced (2);
        drawMetric (g, cell, labels[(size_t) index],
                    optionValue (values[(size_t) index], available[(size_t) index]),
                    units[(size_t) index], family, context, hasDelta,
                    1, {}, -1.0f, {}, true,
                    index < 2 ? typography::TextRole::primaryValue
                              : typography::TextRole::secondaryValue);
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
    const auto context = presentationContext();
    const auto family = experienceFamily();
    const auto compact = family == ExperienceFamily::compactMeter;
    juce::Rectangle<int> channelStrips;
    if (includeChannelStrips && target() == ObservationTarget::absolute
        && (measurementOnlySurround || density == Density::standard || isFullDensity (density)))
        channelStrips = area.removeFromRight (
            measurementOnlySurround
                ? channelStripWidth (context,
                    density == Density::compact ? 126
                    : density == Density::focused ? 142
                    : density == Density::inspection ? 230 : 190)
                : isFullDensity (density)
                    ? channelStripWidth (context, density == Density::inspection ? 164 : 120)
                    : 62).reduced (2);
    const bool unavailableComparison = target() == ObservationTarget::delta
        && ! deltaFactsAvailable();
    if (unavailableComparison)
    {
        const auto statusArea = area.removeFromTop (compact ? 20 : 24);
        g.setColour (COL_TEXT_SECONDARY);
        g.setFont (labelFont (context, typography::TextRole::status,
                              typography::Composition::facts));
        text_style::drawEllipsized (
            g,
            comparison_presentation::statusText (observatoryFrame.comparison_state,
                                                  observatoryFrame.comparison_reason),
            statusArea.reduced (4, 1), juce::Justification::centred);
    }
    else if (compact && density != Density::compact)
        area.removeFromTop (20); // CURRENT / MAX; 100% is view-only
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
                drawMetric (g, metricHelpArea (area.removeFromLeft (area.getWidth() / (3 - index)).reduced (2),
                            index == 0 ? (selectedShortTermLoudness ? level_metrics::Metric::shortTerm : level_metrics::Metric::momentary)
                                : index == 1 ? level_metrics::Metric::truePeak : level_metrics::Metric::crest),
                            hypha::delta() + labels[(size_t) index],
                            optionValue (values[(size_t) index], deltaFactsAvailable()),
                            units[(size_t) index], family, context, true);
            return;
        }
        const std::array<double, 4> values {
            delta.lufs, delta.lufs_s, delta.true_peak, delta.crest
        };
        const std::array<const char*, 4> labels { "M", "S", "TP", "CREST" };
        const std::array<const char*, 4> units { "LU", "LU", "dB", "dB" };
        for (int index = 0; index < 4; ++index)
            drawMetric (g, metricHelpArea (area.removeFromLeft (area.getWidth() / (4 - index)).reduced (2),
                        std::array { level_metrics::Metric::momentary, level_metrics::Metric::shortTerm,
                            level_metrics::Metric::truePeak, level_metrics::Metric::crest }[(size_t) index]),
                        hypha::delta() + labels[(size_t) index],
                        optionValue (values[(size_t) index], deltaFactsAvailable()),
                        units[(size_t) index], family, context, true);
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
        const auto compactFactsAvailable = watchDisplayAvailable
            && (compactShowsMaximum ? cumulativeFactsAvailable() : currentFactsAvailable());
        const bool trackStem = selectedMeterContext
                            == meter_context::MeterContext::trackStem;
        // The loudness now (S), the programme so far (I; Crest for a track or stem) and the
        // highest true peak of the Meter Session. MAX switches S and Crest to their Watch
        // maxima; MAX TP is a maximum already.
        const std::array<double, 3> compactValues {
            watch.lufs_s,
            trackStem ? watch.crest : meter.lufs_i,
            meter.max_true_peak
        };
        const std::array<bool, 3> compactAvailable {
            compactFactsAvailable,
            trackStem ? compactFactsAvailable : cumulativeAvailable,
            cumulativeAvailable
        };
        const std::array<const char*, 3> compactLabels {
            compactShowsMaximum ? "MAX S" : "S",
            trackStem ? (compactShowsMaximum ? "MAX CREST" : "CREST") : "I",
            "MAX TP"
        };
        const std::array<const char*, 3> compactUnits {
            "LUFS", trackStem ? "dB" : "LUFS", "dBTP"
        };
        const std::array<level_metrics::Metric, 3> compactMetrics {
            level_metrics::Metric::shortTerm,
            trackStem ? level_metrics::Metric::crest : level_metrics::Metric::integrated,
            level_metrics::Metric::maximumTruePeak
        };
        for (int index = 0; index < 3; ++index)
            drawMetric (g, metricHelpArea (area.removeFromLeft (area.getWidth() / (3 - index)).reduced (2),
                        compactMetrics[(size_t) index]),
                        compactLabels[(size_t) index],
                        optionValue (compactValues[(size_t) index],
                                     compactAvailable[(size_t) index]),
                        compactUnits[(size_t) index], family, context);
        if (! channelStrips.isEmpty())
            paintChannelStrips (g, channelStrips);
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
    constexpr int mainCount = 3;
    for (int index = 0; index < mainCount; ++index)
    {
        drawMetric (g, metricHelpArea (main.removeFromLeft (main.getWidth() / (mainCount - index)).reduced (2),
                    metricLayout.main[(size_t) index]),
                    level_metrics::label (metricLayout.main[(size_t) index]),
                    optionValue (mainValues[(size_t) index], mainAvailable[(size_t) index]),
                    mainUnits[(size_t) index], family, context,
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
            ? "WARMING " + juce::String ((int) std::floor (observatoryFrame.lra_elapsed_seconds)) + " S"
            : juce::String();
        drawMetric (g, metricHelpArea (area.removeFromLeft (
                        area.getWidth() / (supportCount - index)).reduced (2),
                        metricLayout.support[(size_t) index]),
                    level_metrics::label (metricLayout.support[(size_t) index]),
                    optionValue (supportValues[(size_t) index], supportAvailable[(size_t) index]),
                    supportUnits[(size_t) index], family, context,
                    false, 1, warmingText,
                    isFullDensity (density) ? 0.54f : -1.0f, {},
                    true, typography::TextRole::secondaryValue);
    }
    if (! channelStrips.isEmpty())
        paintChannelStrips (g, channelStrips);
}

void View::paintLevelWithHistory (juce::Graphics& g, juce::Rectangle<int> area)
{
    const auto inspection = getWidth() >= 900;
    juce::Rectangle<int> channelStrips;
    if (target() == ObservationTarget::absolute)
        channelStrips = area.removeFromRight (channelStripWidth (
            presentationContext(), measurementOnlySurround ? (inspection ? 230 : 190)
                                                            : (inspection ? 126 : 116))).reduced (2);

    const auto landscape = area.getWidth() > area.getHeight();
    const auto previousHistoryHeight = juce::jlimit (
        72, inspection ? 240 : 170,
        juce::roundToInt (area.getHeight()
                          * (inspection ? 0.46f : landscape ? 0.40f : 0.32f)));
    const auto previousMetricsHeight = juce::jmax (
        1, area.getHeight() - previousHistoryHeight - 4);
    const auto metricsHeight = juce::jmin (area.getHeight() - 92,
        juce::jmax (inspection ? 162 : 134, compressedLevelMetricsHeight (previousMetricsHeight)));
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
                            presentationContext(),
                            captureFrame ? std::nullopt : hoveredLevelHistoryIndex,
                            maximumMomentary,
                            frameAvailable ? &observatoryFrame.meter : nullptr);
    if (! channelStrips.isEmpty())
        paintChannelStrips (g, channelStrips);
}
}
