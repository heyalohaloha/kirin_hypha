#pragma once

#include "live_compare/LiveCompareLoopEntry.h"
#include "HostExecutableIdentity.h"

#include <cmath>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>

namespace hypha::live_compare_clock_policy
{
struct Certificate
{
    live_compare::ClockAuthority authority = live_compare::ClockAuthority::none;
    std::uint32_t maximumDelaySamples = 0;
    bool measured = false; // the exact host build whose clock proof was measured, not inferred
};

struct StudioVersion { int major = 0, minor = 0, patch = 0; };
// One lower bound for both executable families; changing it does not mark more builds measured.
inline constexpr StudioVersion studioContentMinimum { 6, 0, 0 };

inline bool studioBuildCertified (const juce::String& name, const juce::String& version) noexcept
{
    auto number = version;
    const auto build = version.indexOf (" Build ");
    if (build >= 0)
    {
        const auto suffix = version.substring (build + 7);
        if (suffix.isEmpty() || ! suffix.containsOnly ("0123456789")) return false;
        number = version.substring (0, build);
    }
    juce::StringArray parts;
    parts.addTokens (number, ".", {});
    if (parts.size() < 3 || parts.size() > (build >= 0 ? 3 : 4)) return false;
    for (const auto& part : parts)
        if (part.isEmpty() || part.length() > 9 || ! part.containsOnly ("0123456789")) return false;
    const StudioVersion parsed { parts[0].getIntValue(), parts[1].getIntValue(), parts[2].getIntValue() };
    const bool family = (name == "Studio One" && parsed.major >= 5 && parsed.major <= 7)
        || (name == "Studio Pro" && parsed.major == 8);
    const auto floor = studioContentMinimum;
    return family && (parsed.major > floor.major || (parsed.major == floor.major
        && (parsed.minor > floor.minor || (parsed.minor == floor.minor && parsed.patch >= floor.patch))));
}

inline bool reaperBuildCertified (const juce::String& name, const juce::String& version) noexcept
{
    // All readable versions use the same compensated project coordinate as FREQ. This grants
    // no audition clock: repeated timeline positions do not identify a loop occurrence.
    return (name == "REAPER" || name == "reaper")
        && version.isNotEmpty() && version[0] >= '0' && version[0] <= '9';
}

// Pro Tools' documented maximum delay compensation per sample-rate family. The bound is the
// only loop-occurrence authority the engine clock has: a bounded initial-loop K is unique only
// while the chain's delay stays within it.
inline std::uint32_t proToolsDelayBound (double rate) noexcept
{
    const auto equal = [rate] (double expected) { return std::abs (rate - expected) < 0.5; };
    if (equal (44100.0) || equal (48000.0)) return 16383;
    if (equal (88200.0) || equal (96000.0)) return 32767;
    if (equal (176400.0) || equal (192000.0)) return 65534;
    return 0;
}

// Pro Tools reports its build as "YY.m.p.b" (26.4.1.179), as a marketing year "YYYY.m" or, before
// 2018, as "12.8.3". The engine clock proof needs two host facts that hold from 12.6 on: the
// delay-compensation bound above, and the delay-compensation-state notification (INV-LC8) without
// which a disabled compensation could not be told apart from a proven one. Older builds and
// anything that is not a dotted number fail closed.
inline bool proToolsBuildCertified (const juce::String& version) noexcept
{
    juce::StringArray parts;
    parts.addTokens (version, ".", {});
    if (parts.size() < 2) return false;
    for (const auto& part : parts)
        if (part.isEmpty() || ! part.containsOnly ("0123456789")) return false;
    auto major = parts[0].getIntValue();
    const auto minor = parts[1].getIntValue();
    if (major >= 2000) major -= 2000; // a marketing year: 2018 and later follow 12.8
    return major > 12 || (major == 12 && minor >= 6);
}

inline Certificate classify (juce::AudioProcessor::WrapperType wrapper,
                             const juce::String& executableName,
                             const juce::String& executableVersion,
                             double rate) noexcept
{
    const bool studioPro812 = executableName == "Studio Pro"
        && (executableVersion == "8.1.2 Build 113407" || executableVersion == "8.1.2.113407");
    if (wrapper == juce::AudioProcessor::wrapperType_VST3 && studioBuildCertified (executableName, executableVersion))
        return { live_compare::ClockAuthority::certifiedContent, 0, studioPro812 };
    if (wrapper == juce::AudioProcessor::wrapperType_VST3 && reaperBuildCertified (executableName, executableVersion))
        return { live_compare::ClockAuthority::certifiedProject, 0, false };
    // Pro Tools' executable is "ProTools" on Windows, "Pro Tools" in the macOS bundle and
    // "Pro Tools Developer" inside the Developer bundle. Every build from 12.6 on carries the
    // bounded engine clock; the measured build (26.4.0.5) is the one whose proof was verified.
    const bool proTools = executableName == "Pro Tools" || executableName == "ProTools"
        || executableName == "Pro Tools Developer";
    if (wrapper == juce::AudioProcessor::wrapperType_AAX && proTools
        && proToolsBuildCertified (executableVersion))
        return { live_compare::ClockAuthority::boundedAaxEngine, proToolsDelayBound (rate),
                 executableVersion == "26.4.0.5" };
    return {};
}

inline Certificate classify (juce::AudioProcessor::WrapperType wrapper,
                             const host_identity::Identity& host, double rate) noexcept
{
    return classify (wrapper, host.name,
        wrapper == juce::AudioProcessor::wrapperType_AAX ? host.fileVersion : host.version, rate);
}

inline Certificate current (juce::AudioProcessor::WrapperType wrapper, double rate)
{
    return classify (wrapper, host_identity::current(), rate);
}
}
