#pragma once

#include <cstdint>
#include <vector>

namespace hypha::reference_audition
{
    // Worker-only analysis. No sample returned by this function reaches playback.
    struct ContentCorrelation
    {
        bool accepted = false;
        std::int64_t offsetSamples = 0; // B position minus A position
        double correlation = 0.0;
        double ambiguityDb = 0.0;
    };

    ContentCorrelation correlateReferenceContent (
        const std::vector<float>& a, const std::vector<float>& b,
        int sampleRate, int maximumLagSamples, bool timingDetail = false);
}
