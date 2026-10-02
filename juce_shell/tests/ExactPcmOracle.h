#pragma once

#include <cmath>
#include <cstring>
#include <limits>

namespace hypha::test
{
// Compare the stored float PCM, not a subtraction from an unrounded product. Clang may fuse
// actual - source * gain into FMA, leaving a nonzero residual even for bit-identical PCM.
// No tolerance: a one-ULP difference, opposite zero sign or non-finite sample is rejected.
inline bool exactPcm (float actual, float expected) noexcept
{
    return std::isfinite (actual) && std::isfinite (expected)
        && std::memcmp (&actual, &expected, sizeof (float)) == 0;
}

inline bool exactScaledPcm (float actual, float source, float gain) noexcept
{
    const float expected = source * gain;
    return exactPcm (actual, expected);
}

// Fixed IEEE-754 rounded product, independently specified rather than computed by the renderer.
// This runs in the same optimized translation unit as each oracle consumer, including ARM64/FMA.
inline bool exactPcmControls() noexcept
{
    static_assert (sizeof (float) == 4 && std::numeric_limits<float>::is_iec559);
    // Volatile inputs prevent folding the positive control into a compile-time expression;
    // it must exercise the runtime oracle under the consumer's contraction/optimisation flags.
    volatile float storedSample = 0x1.925892p-4f; // float(0.137f * 0.717f)
    volatile float storedSource = 0.137f, storedGain = 0.717f;
    const float sample = storedSample, source = storedSource, gain = storedGain;
    return exactScaledPcm (sample, source, gain)
        && ! exactScaledPcm (std::nextafter (sample, 1.0f), source, gain)
        && ! exactScaledPcm (std::nextafter (sample, 0.0f), source, gain)
        && ! exactScaledPcm (sample, 0.138f, 0.717f)
        && ! exactScaledPcm (sample, 0.137f, 0.718f)
        && exactPcm (0.0f, 0.0f) && exactPcm (-0.0f, -0.0f)
        && ! exactPcm (-0.0f, 0.0f)
        && ! exactPcm (std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity())
        && ! exactPcm (std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN());
}
}
