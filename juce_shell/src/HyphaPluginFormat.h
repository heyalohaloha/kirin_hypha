#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace hypha::plugin_format
{
constexpr const char* name (juce::AudioProcessor::WrapperType type) noexcept
{
    switch (type)
    {
        case juce::AudioProcessor::wrapperType_VST3: return "VST3";
        case juce::AudioProcessor::wrapperType_AudioUnit: return "AU";
        case juce::AudioProcessor::wrapperType_AudioUnitv3: return "AUv3";
        case juce::AudioProcessor::wrapperType_Standalone: return "Standalone";
        case juce::AudioProcessor::wrapperType_VST: return "VST";
        case juce::AudioProcessor::wrapperType_AAX: return "AAX";
        case juce::AudioProcessor::wrapperType_Unity: return "Unity";
        case juce::AudioProcessor::wrapperType_LV2: return "LV2";
        case juce::AudioProcessor::wrapperType_Undefined: return "Format unconfirmed";
    }
    return "Format unconfirmed";
}

// Product availability is explicit per format. AAX entry was enabled by user direction
// on 2026-09-13; exact capture, clock/PDC continuity and admission checks still apply.
constexpr bool supportsLocalBlindProduct (juce::AudioProcessor::WrapperType type) noexcept
{
    return type == juce::AudioProcessor::wrapperType_VST3
        || type == juce::AudioProcessor::wrapperType_AudioUnit
        || type == juce::AudioProcessor::wrapperType_AAX;
}

static_assert (supportsLocalBlindProduct (juce::AudioProcessor::wrapperType_VST3));
static_assert (supportsLocalBlindProduct (juce::AudioProcessor::wrapperType_AudioUnit));
static_assert (supportsLocalBlindProduct (juce::AudioProcessor::wrapperType_AAX));
static_assert (! supportsLocalBlindProduct (juce::AudioProcessor::wrapperType_Undefined));
}
