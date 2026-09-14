#pragma once
#include "ReferenceRuntimeACapture.h"
#include <array>
#include <cmath>

namespace hypha::reference_audition
{
inline juce::String referenceObservationIdentity (const RuntimeACaptureAudio& audio)
{
    return audio.cuePcmSha256 + ":" + juce::String (audio.startSample) + ":"
        + juce::String (audio.frameCount) + ":" + juce::String (audio.sampleRateHz)
        + ":" + juce::String (audio.channels);
}

// Worker-only evidence. Keep the calibration passage and one recent passage.
// Their PCM is shared with the existing bounded capture, never copied on RT.
// Compare corresponding samples, not loudness at different musical positions.
class CalibrationObservation final
{
public:
    void clear() { observations = {}; }

    void remember (std::shared_ptr<const RuntimeACaptureAudio> audio,
                   std::int64_t sourceStart, std::int64_t sourceRate)
    {
        const Entry next { std::move (audio), sourceStart, sourceRate };
        if (!observations[0].audio) observations[0] = next;
        else observations[1] = next;
    }

    bool changed (const RuntimeACaptureAudio& current, std::int64_t sourceStart,
                  std::int64_t sourceRate) const
    {
        for (const auto& previous : observations)
        {
            if (!previous.audio || sourceRate <= 0 || previous.sourceRate != sourceRate
                || previous.audio->sampleRateHz != current.sampleRateHz
                || previous.audio->channels != current.channels) continue;
            const auto& old = *previous.audio;
            const auto offset = static_cast<std::int64_t> (std::llround (
                static_cast<long double> (sourceStart - previous.sourceStart)
                    * current.sampleRateHz / sourceRate));
            const auto oldFirst = juce::jmax<std::int64_t> (0, offset);
            const auto newFirst = juce::jmax<std::int64_t> (0, -offset);
            const auto count = juce::jmin (old.frameCount - oldFirst, current.frameCount - newFirst);
            if (count < current.sampleRateHz * 3) continue;
            double energy = 0.0, difference = 0.0;
            for (std::int64_t frame = 0; frame < count; ++frame)
                for (int channel = 0; channel < current.channels; ++channel)
                {
                    const auto a = old.interleaved[static_cast<size_t> ((oldFirst + frame) * old.channels + channel)];
                    const auto b = current.interleaved[static_cast<size_t> ((newFirst + frame) * current.channels + channel)];
                    energy += static_cast<double> (a) * a;
                    const auto delta = static_cast<double> (b) - a;
                    difference += delta * delta;
                }
            // Ignore quantisation/dither, but detect material edits in a repeated
            // passage. Silence alone cannot establish a changed calibration.
            if (energy / static_cast<double> (count * current.channels) > 1.0e-8
                && difference > energy * 1.0e-5) return true;
        }
        return false;
    }

private:
    struct Entry
    {
        std::shared_ptr<const RuntimeACaptureAudio> audio;
        std::int64_t sourceStart = 0, sourceRate = 0;
    };
    std::array<Entry, 2> observations;
};
}
