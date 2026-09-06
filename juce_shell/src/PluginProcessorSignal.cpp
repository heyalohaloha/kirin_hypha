#include "PluginProcessor.h"
#include <cmath>

bool KirinHyphaProcessorBase::bufferIsSilent (const juce::AudioBuffer<float>& buffer)
{
    // B-107: silent iff peak < -140 dBFS. Parity with hypha_pre/hypha_post sample_is_silent:
    // linear threshold 10^(-140/20) = 1e-7, compared without log10 (RT-safe on the audio thread).
    static constexpr float kSilencePeakLinear = 1.0e-7f; // -140 dBFS
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const float* p = buffer.getReadPointer (ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (std::abs (p[i]) >= kSilencePeakLinear)
                return false;
    }
    return true;
}
