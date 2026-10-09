#pragma once

#include "ReferenceScalarConversionOracle.h"
#include "../src/reference_audition/ReferenceVisualAudio.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>

// Independent, frozen scalar behavior guards coefficient sharing, including truncated edges.
template <typename Reader, typename Require>
void verifyReferenceConversionParity (Require&& require)
{
    constexpr std::array<std::pair<int, int>, 7> rates {{
        { 44'100, 48'000 }, { 48'000, 44'100 }, { 96'000, 48'000 },
        { 48'000, 96'000 }, { 8'000, 48'000 }, { 192'000, 44'100 },
        { 48'000, 48'000 }
    }};
    int cases = 0;
    for (const auto [sourceRate, hostRate] : rates)
        for (const int channels : { 1, 2 })
            for (const int frames : { 128, 517 })
            {
                const auto length = 10'000;
                const auto hostEnd = static_cast<std::int64_t> (
                    static_cast<double> (length) * hostRate / sourceRate);
                for (const auto start : { std::int64_t { 0 }, std::int64_t { 1777 }, hostEnd - 12 })
                {
                    Reader original (sourceRate, channels, length), optimized (sourceRate, channels, length);
                    juce::AudioBuffer<float> a, b, scratchA, scratchB;
                    require (hypha::reference_scalar_oracle::readReferenceVisualAudio (
                        original, start, frames, hostRate, channels, a, scratchA, {}),
                        "frozen scalar conversion succeeds");
                    require (hypha::reference_audition::readReferenceVisualAudio (
                        optimized, start, frames, hostRate, channels, b, scratchB),
                        "shared-coefficient conversion succeeds");
                    for (int channel = 0; channel < channels; ++channel)
                        require (std::memcmp (a.getReadPointer (channel), b.getReadPointer (channel),
                            sizeof (float) * static_cast<size_t> (frames)) == 0,
                            "conversion PCM remains bit identical to frozen scalar converter");
                    ++cases;
                }
            }
    Reader reader (48'000, 2, 10'000);
    juce::AudioBuffer<float> output, scratch;
    require (! hypha::reference_audition::readReferenceVisualAudio (
        reader, -1, 128, 44'100, 2, output, scratch), "negative start remains invalid");
    require (! hypha::reference_audition::readReferenceVisualAudio (
        reader, 0, 128, 44'100, 1, output, scratch), "wrong channel layout remains invalid");
    bool entered = false;
    require (! hypha::reference_audition::readReferenceVisualAudio (
        reader, 0, 128, 44'100, 2, output, scratch,
        [&entered] (const std::function<void()>&) { entered = true; return false; }),
        "cancelled conversion does not report success");
    require (entered, "conversion guard still owns the work");
    std::cout << "Reference scalar PCM parity: " << cases << " cases; invalid/cancel PASS\n";
}
