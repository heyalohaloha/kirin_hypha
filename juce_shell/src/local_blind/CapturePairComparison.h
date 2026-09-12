#pragma once

#include "ExactRangeCapture.h"

#include <cstdint>
#include <vector>

namespace hypha::local_blind
{
// Non-RT evidence for one already-completed exact PRE/POST capture pair. A correlation result is
// diagnostic only: it cannot repair a range, authorize Blind, or change either captured PCM copy.
struct CapturePairComparison
{
    bool valid = false;
    bool exactAtZero = false;
    bool lagEstimated = false;
    std::uint64_t generation = 0;
    std::uint32_t sampleRate = 0;
    int channels = 0;
    std::int64_t start = 0;
    std::int64_t frames = 0;
    std::int64_t bestLagFrames = 0; // positive: POST content occurs later than PRE
    double zeroLagCorrelation = 0.0;
    double bestCorrelation = 0.0;
    double correlationMargin = 0.0;
    double normalizedZeroLagRmsError = 0.0;
};

CapturePairComparison compareCapturePair (
    CaptureRange range, const std::vector<float>& pre,
    const std::vector<float>& post) noexcept;
}
