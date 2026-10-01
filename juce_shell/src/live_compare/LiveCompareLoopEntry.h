#pragma once

#include "LiveCompareBlockClock.h"
#include "LiveCompareClock.h"

#include <algorithm>
#include <cstdint>
#include <limits>

namespace hypha::live_compare
{
enum class ClockAuthority : std::uint8_t { none, certifiedContent, boundedAaxEngine };
enum class LoopEntryKind : std::uint8_t { none, linear, contentClock, presentationLatency, boundedEngine };
enum class LoopEntryFailure : std::uint8_t { none, observingCycle, clockUnavailable, loopTooShort };

struct LoopEntryCandidate
{
    std::int64_t k = 0;
    LoopEntryKind kind = LoopEntryKind::none;
    LoopEntryFailure failure = LoopEntryFailure::clockUnavailable;
    bool valid = false;
};

inline std::int64_t positiveModulo (std::int64_t value, std::int64_t modulus) noexcept
{
    if (modulus <= 0) return 0;
    const auto remainder = value % modulus;
    return remainder < 0 ? remainder + modulus : remainder;
}

inline bool checkedAddProduct (std::int64_t base, std::int64_t factor,
                               std::int64_t multiplicand, std::int64_t& result) noexcept
{
    if (multiplicand <= 0) return false;
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((factor > 0 && factor > maximum / multiplicand)
        || (factor < 0 && factor < minimum / multiplicand)) return false;
    const auto product = factor * multiplicand;
    if ((product > 0 && base > maximum - product)
        || (product < 0 && base < minimum - product)) return false;
    result = base + product;
    return true;
}

inline bool loopAddressIsCurrent (const LoopAnchor& anchor, const BlockClock& pre,
                                  const BlockClock& post, std::int64_t k,
                                  double rate, std::int64_t capacity) noexcept
{
    if (capacity <= 0 || pre.frames <= 0 || post.frames <= 0) return false;
    if ((k < 0 && post.clock > std::numeric_limits<std::int64_t>::max() + k)
        || (k > 0 && post.clock < std::numeric_limits<std::int64_t>::min() + k)
        || pre.clock > std::numeric_limits<std::int64_t>::max() - pre.frames) return false;
    const auto start = post.clock - k;
    const auto end = pre.clock + pre.frames;
    if (start > std::numeric_limits<std::int64_t>::max() - post.frames
        || start < anchor.runStart || start > end
        || (start < 0 && end > std::numeric_limits<std::int64_t>::max() + start)
        || start + post.frames > end || end - start > capacity)
        return false;
    auto verified = post;
    return corroborateLoop (anchor, post, start, rate, verified);
}

inline LoopEntryCandidate initialLoopCandidate (const LoopAnchor& anchor,
                                                const BlockClock& pre,
                                                const BlockClock& post,
                                                std::int64_t postLoopSamples,
                                                double rate,
                                                std::int64_t capacity) noexcept
{
    const bool common = pre.clockAuthority != 0 && pre.clockAuthority == post.clockAuthority
        && pre.clockBasis == post.clockBasis;
    if (common && pre.clockBasis == static_cast<std::uint8_t> (ClockBasis::vst3Continuous)
        && pre.clockAuthority == static_cast<std::uint8_t> (ClockAuthority::certifiedContent))
    {
        const bool current = loopAddressIsCurrent (anchor, pre, post, 0, rate, capacity);
        return { 0, LoopEntryKind::contentClock,
                 current ? LoopEntryFailure::none : LoopEntryFailure::observingCycle, current };
    }

    if (! anchor.loopKnown || anchor.loopSamples <= 0 || postLoopSamples <= 0)
        return { 0, LoopEntryKind::none, LoopEntryFailure::observingCycle, false };
    const auto loopSamples = anchor.loopSamples;
    if (postLoopSamples != loopSamples) return {};
    std::int64_t postOrigin = 0, preOrigin = 0, base = 0;
    if (! checkedClockSubtract (post.clock, post.project, postOrigin)
        || ! checkedClockSubtract (pre.clock, pre.project, preOrigin)
        || ! checkedClockSubtract (postOrigin, preOrigin, base)) return {};

    if (common && pre.clockBasis == static_cast<std::uint8_t> (ClockBasis::aaxEngine)
        && pre.clockAuthority == static_cast<std::uint8_t> (ClockAuthority::boundedAaxEngine))
    {
        const auto bound = std::min (pre.maximumDelaySamples, post.maximumDelaySamples);
        if (bound == 0) return {};
        if (loopSamples <= static_cast<std::int64_t> (bound))
            return { 0, LoopEntryKind::boundedEngine, LoopEntryFailure::loopTooShort, false };
        const auto candidate = positiveModulo (base, loopSamples);
        if (candidate > static_cast<std::int64_t> (bound)) return {};
        const bool current = loopAddressIsCurrent (anchor, pre, post, candidate, rate, capacity);
        return { candidate, LoopEntryKind::boundedEngine,
                 current ? LoopEntryFailure::none : LoopEntryFailure::observingCycle, current };
    }

    const bool presentation = pre.clockBasis == static_cast<std::uint8_t> (ClockBasis::audioUnitRender)
        && post.clockBasis == static_cast<std::uint8_t> (ClockBasis::audioUnitRender)
        && pre.presentationSource != 0
        && pre.presentationSource == post.presentationSource
        && pre.outputPresentationValid && post.outputPresentationValid
        && pre.outputPresentationSamples > post.outputPresentationSamples;
    if (! presentation) return {};
    const auto delay = static_cast<std::int64_t> (pre.outputPresentationSamples
                                                   - post.outputPresentationSamples);
    const auto skew = static_cast<std::int64_t> (pre.frames) + post.frames;
    if (delay <= 0 || loopSamples <= delay + 2 * skew || delay + skew >= capacity)
        return { 0, LoopEntryKind::presentationLatency, LoopEntryFailure::loopTooShort, false };

    std::int64_t preEnd = 0, postAtBase = 0, ageAtBase = 0, targetAge = 0, ageDelta = 0;
    if (! checkedClockAdd (pre.clock, pre.frames, preEnd)
        || ! checkedClockSubtract (post.clock, base, postAtBase)
        || ! checkedClockSubtract (preEnd, postAtBase, ageAtBase)
        || ! checkedClockAdd (delay, pre.frames, targetAge)
        || ! checkedClockSubtract (targetAge, ageAtBase, ageDelta)) return {};
    auto laps = ageDelta / loopSamples;
    const auto remainder = ageDelta % loopSamples;
    const auto half = loopSamples / 2 + loopSamples % 2;
    if (remainder >= half) ++laps;
    else if (remainder <= -half) --laps;
    std::int64_t candidate = 0;
    if (! checkedAddProduct (base, laps, loopSamples, candidate)) return {};
    std::int64_t postAtCandidate = 0, age = 0, error = 0;
    if (! checkedClockSubtract (post.clock, candidate, postAtCandidate)
        || ! checkedClockSubtract (preEnd, postAtCandidate, age)
        || ! checkedClockSubtract (age, targetAge, error)
        || error > skew || error < -skew) return {};
    const bool current = loopAddressIsCurrent (anchor, pre, post, candidate, rate, capacity);
    return { candidate, LoopEntryKind::presentationLatency,
             current ? LoopEntryFailure::none : LoopEntryFailure::observingCycle, current };
}
}
