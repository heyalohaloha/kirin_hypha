#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace hypha::reference_audition
{
// A full capture authorizes calibration; per-block output confirmation never
// upgrades that calibration after a discontinuity.
inline bool referenceCopyProofCurrent (bool version, bool copy, std::uint64_t proof,
                                       std::uint64_t continuity) noexcept
{
    return ! version || ! copy || proof == continuity;
}

// A registered render can be exactly the live input (apart from a constant gain).
// Once established on the worker, check that special case on each output block:
// a media-item loop need not change the host clock. No allocation or DSP on A.
template <class A, class B>
bool referenceCopyCorresponds (std::size_t count, A a, B b, bool requireSignal = false, bool* pcmAgrees = nullptr) noexcept
{
    if (pcmAgrees) *pcmAgrees = false;
    double aa = 0.0, bb = 0.0, ab = 0.0;
    for (std::size_t i = 0; i < count; ++i)
    {
        const double x = a (i), y = b (i);
        if (! std::isfinite (x) || ! std::isfinite (y)) return false;
        aa += x * x; bb += y * y; ab += x * y;
    }
    // Silence carries no new position evidence after a discontinuity.
    if (aa <= 0.0 || bb <= 0.0)
    {
        const bool same = aa <= 0.0 && bb <= 0.0;
        if (pcmAgrees) *pcmAgrees = same;
        return ! requireSignal && same;
    }
    const bool same = ab > 0.0 && ab * ab >= aa * bb * (1.0 - 1.0e-8);
    if (pcmAgrees) *pcmAgrees = same;
    return same;
}
}
