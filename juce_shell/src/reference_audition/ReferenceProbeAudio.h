#pragma once
#include <vector>
#include <juce_audio_formats/juce_audio_formats.h>
namespace hypha::reference_audition
{
    juce::String referenceProbePcmHash (const std::vector<float>&);
    bool readReferenceProbe (juce::AudioFormatReader&, std::int64_t sourceStart,
                             std::int64_t outputFrames, int outputRate, int channels,
                             std::vector<float>& interleaved);
}
