#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <algorithm>

namespace hypha::reference_audition
{
// Both named audition and whole-song Blind return the complete captured live
// block during a page wait, keeping the prepared source and explicit choice.
template <class Channel> void restoreReferenceLiveInput (
    juce::AudioBuffer<float>& buffer, Channel channel) noexcept
{
    for (int c = 0; c < buffer.getNumChannels(); ++c)
        std::copy_n (channel (c), buffer.getNumSamples(), buffer.getWritePointer (c));
}
}
