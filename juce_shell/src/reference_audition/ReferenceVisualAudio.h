#pragma once
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
namespace hypha::reference_audition
{
// Identical absolute phase and edge treatment to the audible page converter.
bool readReferenceVisualAudio (juce::AudioFormatReader&, std::int64_t hostStart,
    int frames, int hostRate, int channels, juce::AudioBuffer<float>& output,
    juce::AudioBuffer<float>& scratch,
    const std::function<bool(const std::function<void()>&)>& conversionGuard = {});
}
