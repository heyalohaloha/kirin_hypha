#include "ReferenceVisualAudio.h"
#include <cmath>
namespace hypha::reference_audition
{
bool readReferenceVisualAudio (juce::AudioFormatReader& reader, std::int64_t pageStart,
    int framesPerPage, int hostRate, int channels, juce::AudioBuffer<float>& output,
    juce::AudioBuffer<float>& conversionInput,
    const std::function<bool(const std::function<void()>&)>& conversionGuard)
{
    if (pageStart < 0 || framesPerPage < 1 || hostRate < 8000 || hostRate > 768000
        || !std::isfinite (reader.sampleRate) || reader.sampleRate < 8000 || reader.sampleRate > 768000
        || channels < 1 || channels > 2 || int (reader.numChannels) != channels) return false;
    const auto sourceSampleRate = reader.sampleRate;
    const auto outputSampleRate = double (hostRate);
    output.setSize (channels, framesPerPage, false, false, true);
    if (std::abs (sourceSampleRate - outputSampleRate) <= 0.001)
        return reader.read (&output, 0, framesPerPage, pageStart, true, channels > 1);
        constexpr int halfTaps = 24;
        const double ratio = sourceSampleRate / outputSampleRate;
        const double firstPosition = static_cast<double> (pageStart) * ratio;
        const double lastPosition = static_cast<double> (pageStart + framesPerPage - 1) * ratio;
        const auto rawStart = static_cast<std::int64_t> (std::floor (firstPosition)) - halfTaps;
        const auto rawEnd = static_cast<std::int64_t> (std::ceil (lastPosition)) + halfTaps + 1;
        const auto readStart = juce::jmax<std::int64_t> (0, rawStart);
        const auto readEnd = juce::jmin<std::int64_t> (reader.lengthInSamples, rawEnd);
        const int readFrames = static_cast<int> (juce::jmax<std::int64_t> (0, readEnd - readStart));
        conversionInput.setSize (channels, juce::jmax (1, readFrames), false, false, true);
        conversionInput.clear();
        if (readFrames > 0 && ! reader.read (&conversionInput, 0, readFrames,
                                              readStart, true, channels > 1))
            return false;

    const auto convert = [&] {
        const double cutoff = juce::jmin (1.0, outputSampleRate / sourceSampleRate) * 0.94;
        for (int outputFrame = 0; outputFrame < framesPerPage; ++outputFrame)
        {
            const double position = static_cast<double> (pageStart + outputFrame) * ratio;
            const auto center = static_cast<std::int64_t> (std::floor (position));
            for (int channel = 0; channel < channels; ++channel)
            {
                double weighted = 0.0;
                double weightSum = 0.0;
                for (int tap = -halfTaps + 1; tap <= halfTaps; ++tap)
                {
                    const auto sourceFrame = center + tap;
                    if (sourceFrame < 0 || sourceFrame >= reader.lengthInSamples)
                        continue;
                    const double distance = position - static_cast<double> (sourceFrame);
                    const double scaled = juce::MathConstants<double>::pi * cutoff * distance;
                    const double sinc = std::abs (scaled) < 1.0e-12
                        ? cutoff : cutoff * std::sin (scaled) / scaled;
                    const double normalizedDistance = distance / static_cast<double> (halfTaps);
                    const double window = std::abs (normalizedDistance) >= 1.0
                        ? 0.0
                        : 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi
                                                * normalizedDistance)
                               + 0.08 * std::cos (juce::MathConstants<double>::twoPi
                                                 * normalizedDistance);
                    const double weight = sinc * window;
                    const auto inputOffset = sourceFrame - readStart;
                    if (inputOffset >= 0 && inputOffset < readFrames)
                    {
                        weighted += conversionInput.getSample (
                            channel, static_cast<int> (inputOffset)) * weight;
                        weightSum += weight;
                    }
                }
                output.setSample (channel, outputFrame,
                                      static_cast<float> (weightSum == 0.0
                                          ? 0.0 : weighted / weightSum));
            }
        }
    };
    if (conversionGuard) return conversionGuard (convert);
    convert(); return true;
}
}
