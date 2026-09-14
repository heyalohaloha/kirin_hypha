#include "ReferenceProbeAudio.h"
#include <cmath>
#include <cstring>
#include <limits>
#include <juce_cryptography/juce_cryptography.h>
namespace hypha::reference_audition
{
    namespace { constexpr int sincHalfTaps = 24; }
        juce::String referenceProbePcmHash (const std::vector<float>& samples)
        {
            juce::MemoryBlock canonical (samples.size() * sizeof (float), true);
            auto* output = static_cast<std::uint8_t*> (canonical.getData());
            for (size_t index = 0; index < samples.size(); ++index)
            {
                const float normalized = samples[index] == 0.0f ? 0.0f : samples[index];
                std::uint32_t bits = 0;
                std::memcpy (&bits, &normalized, sizeof (bits));
                output[index * 4] = static_cast<std::uint8_t> (bits & 0xffu);
                output[index * 4 + 1] = static_cast<std::uint8_t> ((bits >> 8u) & 0xffu);
                output[index * 4 + 2] = static_cast<std::uint8_t> ((bits >> 16u) & 0xffu);
                output[index * 4 + 3] = static_cast<std::uint8_t> ((bits >> 24u) & 0xffu);
            }
            return juce::SHA256 (canonical).toHexString();
        }

        bool readReferenceProbe (juce::AudioFormatReader& reader, std::int64_t sourceStart,
                          std::int64_t outputFrames, int outputRate, int channels,
                          std::vector<float>& interleaved)
        {
            if (static_cast<int> (reader.numChannels) != channels || channels < 1 || channels > 2
                || outputFrames < 1 || outputFrames > 3072000 || sourceStart < 0
                || outputRate < 8000 || outputRate > 768000
                || !std::isfinite (reader.sampleRate) || reader.sampleRate < 8000
                || reader.sampleRate > 768000 || sourceStart >= reader.lengthInSamples
                || static_cast<long double> (outputFrames - 1) * reader.sampleRate / outputRate
                    >= static_cast<long double> (reader.lengthInSamples - sourceStart))
                return false;
            if (std::abs (reader.sampleRate - outputRate) <= 0.001)
            {
                if (sourceStart < 0 || sourceStart + outputFrames > reader.lengthInSamples
                    || outputFrames > std::numeric_limits<int>::max())
                    return false;
                juce::AudioBuffer<float> exact (channels, static_cast<int> (outputFrames));
                if (! reader.read (&exact, 0, static_cast<int> (outputFrames), sourceStart,
                                   true, channels > 1))
                    return false;
                interleaved.resize (static_cast<size_t> (outputFrames * channels));
                for (std::int64_t frame = 0; frame < outputFrames; ++frame)
                    for (int channel = 0; channel < channels; ++channel)
                        interleaved[static_cast<size_t> (frame * channels + channel)]
                            = exact.getSample (channel, static_cast<int> (frame));
                return true;
            }
            const auto ratio = reader.sampleRate / outputRate;
            const auto rawStart = sourceStart - sincHalfTaps;
            const auto rawEnd = static_cast<std::int64_t> (std::ceil (
                sourceStart + static_cast<double> (outputFrames - 1) * ratio))
                + sincHalfTaps + 1;
            const auto readStart = juce::jmax<std::int64_t> (0, rawStart);
            const auto readEnd = juce::jmin<std::int64_t> (reader.lengthInSamples, rawEnd);
            const auto readFrames64 = juce::jmax<std::int64_t> (0, readEnd - readStart);
            if (readFrames64 < 1 || readFrames64 > std::numeric_limits<int>::max())
                return false;
            const auto readFrames = static_cast<int> (readFrames64);
            juce::AudioBuffer<float> input (channels, readFrames);
            if (! reader.read (&input, 0, readFrames, readStart, true, channels > 1))
                return false;
            interleaved.assign (static_cast<size_t> (outputFrames * channels), 0.0f);
            const auto cutoff = juce::jmin (1.0, outputRate / reader.sampleRate) * 0.94;
            for (std::int64_t outputFrame = 0; outputFrame < outputFrames; ++outputFrame)
            {
                const auto position = sourceStart + static_cast<double> (outputFrame) * ratio;
                const auto center = static_cast<std::int64_t> (std::floor (position));
                for (int channel = 0; channel < channels; ++channel)
                {
                    double weighted = 0.0;
                    double weightSum = 0.0;
                    for (int tap = -sincHalfTaps + 1; tap <= sincHalfTaps; ++tap)
                    {
                        const auto sourceFrame = center + tap;
                        const auto inputOffset = sourceFrame - readStart;
                        if (inputOffset < 0 || inputOffset >= readFrames)
                            continue;
                        const auto distance = position - static_cast<double> (sourceFrame);
                        const auto scaled = juce::MathConstants<double>::pi * cutoff * distance;
                        const auto sinc = std::abs (scaled) < 1.0e-12
                            ? cutoff : cutoff * std::sin (scaled) / scaled;
                        const auto normalized = distance / sincHalfTaps;
                        const auto window = std::abs (normalized) >= 1.0 ? 0.0
                            : 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi * normalized)
                                   + 0.08 * std::cos (
                                       juce::MathConstants<double>::twoPi * normalized);
                        const auto weight = sinc * window;
                        weighted += input.getSample (channel, static_cast<int> (inputOffset)) * weight;
                        weightSum += weight;
                    }
                    if (weightSum == 0.0)
                        return false;
                    interleaved[static_cast<size_t> (outputFrame * channels + channel)]
                        = static_cast<float> (weighted / weightSum);
                }
            }
            return true;
        }
}
