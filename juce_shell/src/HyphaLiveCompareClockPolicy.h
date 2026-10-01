#pragma once

#include "live_compare/LiveCompareLoopEntry.h"

#include <cmath>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>

namespace hypha::live_compare_clock_policy
{
struct Certificate
{
    live_compare::ClockAuthority authority = live_compare::ClockAuthority::none;
    std::uint32_t maximumDelaySamples = 0;
};

inline std::uint32_t proToolsDelayBound (double rate) noexcept
{
    const auto equal = [rate] (double expected) { return std::abs (rate - expected) < 0.5; };
    if (equal (44100.0) || equal (48000.0)) return 16383;
    if (equal (88200.0) || equal (96000.0)) return 32767;
    if (equal (176400.0) || equal (192000.0)) return 65534;
    return 0;
}

inline Certificate classify (juce::AudioProcessor::WrapperType wrapper,
                             const juce::String& executableName,
                             const juce::String& executableVersion,
                             double rate) noexcept
{
    const bool studioPro812 = executableName == "Studio Pro"
        && (executableVersion == "8.1.2 Build 113407" || executableVersion == "8.1.2.113407");
    if (wrapper == juce::AudioProcessor::wrapperType_VST3 && studioPro812)
        return { live_compare::ClockAuthority::certifiedContent, 0 };
    // File::getVersion() exposes the executable's fixed/file bundle version, not Pro Tools'
    // marketing year. The measured Developer host reports 26.4.0.5 on both macOS and Windows.
    // Keep this exact: AddClock is useful only after the corresponding host build has qualified
    // the bounded-engine proof, so a nearby patch or the marketing string must fail closed.
    const bool proToolsDeveloper26405
        = (executableName == "Pro Tools" || executableName == "ProTools")
        && executableVersion == "26.4.0.5";
    if (wrapper == juce::AudioProcessor::wrapperType_AAX && proToolsDeveloper26405)
        return { live_compare::ClockAuthority::boundedAaxEngine, proToolsDelayBound (rate) };
    return {};
}

inline Certificate current (juce::AudioProcessor::WrapperType wrapper, double rate)
{
    const auto executable = juce::File::getSpecialLocation (juce::File::hostApplicationPath);
    return classify (wrapper, executable.getFileNameWithoutExtension(), executable.getVersion(), rate);
}
}
