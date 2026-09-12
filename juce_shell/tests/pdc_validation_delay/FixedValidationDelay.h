#pragma once

#include <algorithm>
#include <cstddef>
#include <vector>

namespace hypha::pdc_validation
{
class FixedValidationDelay final
{
public:
    static constexpr int latencySamples = 4'096;

    void prepare (int channels)
    {
        configuredChannels = channels == 1 || channels == 2 ? channels : 0;
        samples.assign (static_cast<std::size_t> (configuredChannels * latencySamples), 0.0f);
        writePosition = 0;
    }

    bool process (float* const* channelSamples, int channels, int frames) noexcept
    {
        if (channelSamples == nullptr || channels != configuredChannels || frames < 0
            || samples.size() != static_cast<std::size_t> (channels * latencySamples))
            return false;
        for (int channel = 0; channel < channels; ++channel)
            if (channelSamples[channel] == nullptr)
                return false;

        for (int frame = 0; frame < frames; ++frame)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto index = static_cast<std::size_t> (
                    channel * latencySamples + writePosition);
                const auto input = channelSamples[channel][frame];
                channelSamples[channel][frame] = samples[index];
                samples[index] = input;
            }
            writePosition = (writePosition + 1) % latencySamples;
        }
        return true;
    }

    void reset() noexcept
    {
        std::fill (samples.begin(), samples.end(), 0.0f);
        writePosition = 0;
    }

private:
    std::vector<float> samples;
    int configuredChannels = 0;
    int writePosition = 0;
};
}
