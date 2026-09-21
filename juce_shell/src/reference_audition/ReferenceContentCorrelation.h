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

    struct ContentEnvelopeCorrelation
    {
        bool accepted = false;
        double fastCorrelation = 0.0;
        double slowCorrelation = 0.0;
        double onsetCorrelation = 0.0;
        double bandMedianCorrelation = 0.0;
        int agreeingBands = 0;

        double score() const noexcept;
    };

    ContentCorrelation correlateReferenceContent (
        const std::vector<float>& a, const std::vector<float>& b,
        int sampleRate, int maximumLagSamples, bool timingDetail = false);

    // Confirms that two already timeline-aligned observations contain the same
    // musical passage after mastering changes. It never estimates a position;
    // the caller must independently prove the timeline candidate and ambiguity.
    ContentEnvelopeCorrelation correlateReferenceEnvelope (
        const std::vector<float>& a, const std::vector<float>& b,
        int sampleRate, int channels);
}
