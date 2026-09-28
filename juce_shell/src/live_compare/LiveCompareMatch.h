#pragma once

#include "LiveCompareSession.h"

#include <cstdint>

namespace hypha::live_compare
{
enum class MatchFailure : std::uint8_t
{
    none,
    notProven,       // K is not valid for the latest block
    tooShort,        // less than minimumSeconds of contiguous, proven history
    overwritten,     // PRE or POST moved past the window while it was copied
    notEnoughSignal  // neither gain policy found enough paired active signal
};

struct MatchResult
{
    MatchFailure failure = MatchFailure::notProven;
    double measuredDb = 0.0;       // POST loudness minus PRE loudness over the aligned window
    double appliedDb = 0.0;        // gain applied to the PRE copy
    bool limitedByTruePeak = false; // PRE would exceed the true-peak ceiling at measuredDb
    double ceilingDbtp = 0.0;
    std::uint64_t analysisUnits = 0;
    double seconds = 0.0;
    bool ok() const noexcept { return failure == MatchFailure::none; }
};

// Non-RT (message thread). Aligns the latest window of POST's input history with PRE's ring through
// the proven K, measures it with the Local Blind gain policies (BS.1770 loudness, cue true peak),
// and returns the PRE gain. A PRE boost that would exceed max(-1 dBTP, POST peak, PRE peak) is
// limited to the ceiling in stage 1; the residual is reported instead of lowering POST.
MatchResult computeMatch (const Ring& ring, const PostRenderer& renderer, std::uint32_t sampleRate,
                          double maximumSeconds = 4.0, double minimumSeconds = 3.0);
}
