#pragma once

#include "kirin_hypha_chain_observation.h"
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>

namespace hypha::chain_clock_policy
{
// Exact-host allowlist for the one VST3 clock mapping observed with a controlled WAV and
// 4096-sample delay. This is a runtime identity check, not a substitute for product-host tests.
inline std::uint8_t classify (bool windows, juce::AudioProcessor::WrapperType wrapper,
                              const juce::String& executableName,
                              const juce::String& executableVersion) noexcept
{
    return windows
        && wrapper == juce::AudioProcessor::wrapperType_VST3
        && executableName == "Studio Pro"
        && executableVersion == "8.1.2.113407"
            ? KIRIN_CHAIN_CLOCK_POLICY_STUDIO_PRO_812_WINDOWS_VST3
            : KIRIN_CHAIN_CLOCK_POLICY_UNKNOWN;
}

// CHAIN ACTION is a Debug-only diagnostic until more hosts are certified: a release build never
// reports a certified host, so the chain join stays unavailable and LEVEL draws no chain band or
// summary. The Rust observation and join ship unchanged.
inline std::uint8_t current (juce::AudioProcessor::WrapperType wrapper)
{
   #if JUCE_WINDOWS && JUCE_DEBUG
    // currentExecutableFile resolves the plugin DLL in JUCE on Windows. Only the main
    // process executable identifies the DAW whose clock semantics were measured.
    const auto executable = juce::File::getSpecialLocation (juce::File::hostApplicationPath);
    return classify (true, wrapper, executable.getFileNameWithoutExtension(),
                     executable.getVersion());
   #else
    juce::ignoreUnused (wrapper);
    return KIRIN_CHAIN_CLOCK_POLICY_UNKNOWN;
   #endif
}
}
