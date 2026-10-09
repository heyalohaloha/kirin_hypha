#include "ReferenceVisualAudio.h"
#include <cmath>
#include <array>
#include <unordered_map>
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

    using Weights = std::array<double, halfTaps * 2>;
    const auto convert = [&] {
        const double cutoff = juce::jmin (1.0, outputSampleRate / sourceSampleRate) * 0.94;
        // This non-RT job owns at most 512 exact phases (about 200 KiB), then computes
        // uncached phases normally. No rounding, persistent cache or rate/source sharing.
        std::unordered_map<double, Weights> phases;
        phases.reserve (128);
        for (int outputFrame = 0; outputFrame < framesPerPage; ++outputFrame)
        {
            const double position = static_cast<double> (pageStart + outputFrame) * ratio;
            const auto center = static_cast<std::int64_t> (std::floor (position));
            if (center - halfTaps + 1 >= reader.lengthInSamples)
            {
                for (int channel = 0; channel < channels; ++channel) output.setSample (channel, outputFrame, 0.0f);
                continue; // The original tap loop has no source sample here and outputs exact zero.
            }
            const auto phase = position - static_cast<double> (center);
            const auto found = phases.find (phase);
            Weights uncached {};
            const Weights* weights = found != phases.end() ? &found->second : nullptr;
            if (weights == nullptr)
            {
                for (int tap = -halfTaps + 1; tap <= halfTaps; ++tap)
                {
                    const double distance = position - static_cast<double> (center + tap);
                    const double scaled = juce::MathConstants<double>::pi * cutoff * distance;
                    const double sinc = std::abs (scaled) < 1.0e-12
                        ? cutoff : cutoff * std::sin (scaled) / scaled;
                    const double normalizedDistance = distance / static_cast<double> (halfTaps);
                    const double window = std::abs (normalizedDistance) >= 1.0
                        ? 0.0
                        : 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi * normalizedDistance)
                               + 0.08 * std::cos (juce::MathConstants<double>::twoPi * normalizedDistance);
                    uncached[static_cast<size_t> (tap + halfTaps - 1)] = sinc * window;
                }
                weights = phases.size() < 512u ? &phases.emplace (phase, uncached).first->second : &uncached;
            }
            // The source position and sinc/window coefficients are shared by both channels.
            // Accumulate each channel in the original tap order, preserving the exact PCM.
            double weighted[2] { 0.0, 0.0 };
            double weightSum = 0.0;
            for (int tap = -halfTaps + 1; tap <= halfTaps; ++tap)
            {
                const auto sourceFrame = center + tap;
                if (sourceFrame < 0 || sourceFrame >= reader.lengthInSamples)
                    continue;
                const double weight = (*weights)[static_cast<size_t> (tap + halfTaps - 1)];
                const auto inputOffset = sourceFrame - readStart;
                if (inputOffset >= 0 && inputOffset < readFrames)
                {
                    for (int channel = 0; channel < channels; ++channel)
                        weighted[channel] += conversionInput.getSample (
                            channel, static_cast<int> (inputOffset)) * weight;
                    weightSum += weight;
                }
            }
            for (int channel = 0; channel < channels; ++channel)
                output.setSample (channel, outputFrame, static_cast<float> (
                    weightSum == 0.0 ? 0.0 : weighted[channel] / weightSum));
        }
    };
    if (conversionGuard) return conversionGuard (convert);
    convert(); return true;
}
}
