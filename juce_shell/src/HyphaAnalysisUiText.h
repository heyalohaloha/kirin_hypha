#pragma once

#include <juce_core/juce_core.h>

namespace hypha::analysis_ui
{
inline juce::String switchViewTooltip (const char* viewName)
{
    return juce::String (viewName) + ". Click to switch view.";
}

inline juce::String slotsInUse (const juce::String& ownerNames)
{
    auto text = juce::String ("Both slots in use");
    if (ownerNames.isNotEmpty())
        text += " " + juce::String::charToString (0x2014) + " " + ownerNames;
    return text;
}

inline juce::String channelModeTooltip (uint8_t mode)
{
    if (mode == 1u) return "MID: analyze (L + R) / 2.";
    if (mode == 2u) return "SIDE: analyze (L - R) / 2. Stereo only.";
    return "LR: analyze L and R separately, then average power.";
}

inline juce::String spectrumPlotTooltip()
{
    return "Move to inspect frequency and Delta. Click to lock Focus Trail.";
}

inline juce::String approximateFrequencyTooltip()
{
    return "~ means approximate frequency. Very low tones need a longer window to locate exactly.";
}

inline juce::String deltaLegendTooltip()
{
    return "Delta: POST minus PRE on the left dB scale.";
}

inline juce::String preLegendTooltip()
{
    return "PRE: input spectrum on the right dBFS scale.";
}

inline juce::String postLegendTooltip()
{
    return "POST: output spectrum on the right dBFS scale.";
}

inline juce::String markTooltip (bool clear)
{
    return clear ? "Clear the MARK reference."
                 : "MARK: freeze the current full-band Delta curve.";
}

inline juce::String focusTrailTooltip (bool clear)
{
    return clear ? "Release the frequency lock."
                 : "Focus Trail: six seconds of Delta at the locked frequency.";
}

inline juce::String sharpnessDeltaTooltip()
{
    return "Sharpness Delta is POST minus PRE. Unit: acum (DIN 45692).";
}

inline juce::String liveOverviewTooltip()
{
    return "LIVE shows absolute POST facts on independent fixed scales.";
}

inline juce::String liveMetricTooltip (size_t index)
{
    if (index == 0u)
        return "LUFS-M: 400 ms momentary loudness. Absolute POST value.";
    if (index == 1u)
        return "True Peak: highest inter-sample peak in 400 ms. Unit: dBTP.";
    return "Sharpness: high-frequency weighting. Unit: acum (DIN 45692).";
}
}
