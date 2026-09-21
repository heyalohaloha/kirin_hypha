#include "../src/reference_audition/ReferenceContentCorrelation.h"
#include <array>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace ref = hypha::reference_audition;
void require (bool value, const char* message)
{
    if (! value) { std::cerr << message << '\n'; std::exit (1); }
}
int main()
{
    for (const auto rate : { 8000, 44100, 48000, 96000 })
    {
        const auto count = static_cast<size_t> (rate * 2);
        std::vector<float> a (count), b (count), unrelated (count);
        std::uint32_t random = 913;
        const auto sample = [&random] {
            random = random * 1664525u + 1013904223u;
            return static_cast<float> ((random >> 8) / 16777216.0 - 0.5);
        };
        for (size_t index = 0; index < count; ++index) { a[index] = sample(); unrelated[index] = sample(); }
        for (const auto offset : { -317, 0, 317 })
        {
            for (size_t index = 0; index < count; ++index)
            {
                const auto source = static_cast<std::int64_t> (index) - offset;
                b[index] = source >= 0 && source < static_cast<std::int64_t> (count)
                    ? std::tanh (a[static_cast<size_t> (source)] * 1.2f) * -0.5f : 0.0f;
            }
            const auto match = ref::correlateReferenceContent (a, b, rate, 1000);
            require (match.accepted && match.offsetSamples == offset, "signed offset, gain, polarity and nonlinear variant must align");
        }
        require (!ref::correlateReferenceContent (a, unrelated, rate, 1000).accepted, "unrelated content must be rejected");
        b.assign (count, 0.0f);
        require (!ref::correlateReferenceContent (a, b, rate, 1000).accepted, "silence must be rejected");
        for (size_t index = 0; index < count; ++index)
            a[index] = b[index] = static_cast<float> (0.1 * std::sin (index * 6.283185307179586 * 997.0 / rate));
        require (!ref::correlateReferenceContent (a, b, rate, 1000).accepted, "periodic tone cannot establish unique position");
        b[13] = std::numeric_limits<float>::quiet_NaN();
        require (!ref::correlateReferenceContent (a, b, rate, 1000).accepted, "nonfinite audio must be rejected");
    }
    {
        constexpr int rate = 48000, channels = 2, frames = rate * 4;
        std::vector<float> original (static_cast<size_t> (frames * channels));
        std::vector<float> mastered (original.size());
        std::vector<float> unrelated (original.size());
        std::uint32_t random = 913, other = 1913, envelope = 867;
        std::array<float, channels> filtered {}, otherFiltered {};
        std::array<std::array<float, 7>, channels> allPassInput {}, allPassOutput {};
        const std::array<float, 7> allPassCoefficients {
            0.98f, -0.96f, 0.94f, -0.92f, 0.9f, -0.88f, 0.86f,
        };
        float amplitude = 0.1f;
        for (int frame = 0; frame < frames; ++frame)
        {
            if (frame % (rate / 20) == 0)
            {
                envelope = envelope * 1664525u + 1013904223u;
                amplitude = 0.04f + static_cast<float> (envelope >> 8) / 16777216.0f * 0.25f;
            }
            for (int channel = 0; channel < channels; ++channel)
            {
                random = random * 1664525u + 1013904223u;
                other = other * 1664525u + 1013904223u;
                const auto noise = static_cast<float> (random >> 8) / 16777216.0f - 0.5f;
                const auto unrelatedNoise = static_cast<float> (other >> 8) / 16777216.0f - 0.5f;
                filtered[static_cast<size_t> (channel)] += 0.32f
                    * (noise - filtered[static_cast<size_t> (channel)]);
                otherFiltered[static_cast<size_t> (channel)] += 0.51f
                    * (unrelatedNoise - otherFiltered[static_cast<size_t> (channel)]);
                const auto value = filtered[static_cast<size_t> (channel)] * amplitude;
                original[static_cast<size_t> (frame * channels + channel)] = value;
                unrelated[static_cast<size_t> (frame * channels + channel)]
                    = otherFiltered[static_cast<size_t> (channel)] * 0.2f;
                auto shifted = value;
                for (size_t stage = 0; stage < allPassCoefficients.size(); ++stage)
                {
                    const auto coefficient = channel == 0
                        ? allPassCoefficients[stage] : -allPassCoefficients[stage];
                    const auto input = shifted;
                    shifted = -coefficient * input
                        + allPassInput[static_cast<size_t> (channel)][stage]
                        + coefficient * allPassOutput[static_cast<size_t> (channel)][stage];
                    allPassInput[static_cast<size_t> (channel)][stage] = input;
                    allPassOutput[static_cast<size_t> (channel)][stage] = shifted;
                }
                mastered[static_cast<size_t> (frame * channels + channel)]
                    = std::tanh (shifted * 2.2f) * 0.45f;
            }
        }
        std::vector<float> originalLeft (frames), masteredLeft (frames);
        for (int frame = 0; frame < frames; ++frame)
        {
            originalLeft[static_cast<size_t> (frame)]
                = original[static_cast<size_t> (frame * channels)];
            masteredLeft[static_cast<size_t> (frame)]
                = mastered[static_cast<size_t> (frame * channels)];
        }
        require (!ref::correlateReferenceContent (
                     originalLeft, masteredLeft, rate, rate / 4).accepted,
                 "phase-shaped mastering fixture must exercise the envelope fallback");
        const auto samePassage = ref::correlateReferenceEnvelope (
            original, mastered, rate, channels);
        require (samePassage.accepted && samePassage.agreeingBands >= 4,
                 "multi-scale and multi-band envelopes must retain a mastered same-passage identity");
        require (!ref::correlateReferenceEnvelope (
                     original, unrelated, rate, channels).accepted,
                 "unrelated audio may not acquire a timeline from duration alone");
        std::fill (unrelated.begin(), unrelated.end(), 0.0f);
        require (!ref::correlateReferenceEnvelope (
                     original, unrelated, rate, channels).accepted,
                 "silence may not acquire a mastered-passage identity");
    }
    std::cout << "content correlation: signed offsets, nonlinear gain/polarity, unrelated, silence, periodic and nonfinite cases pass at four rates\n";
}
