#pragma once
#include "LiveCompareClock.h"
#include "LiveCompareLoop.h"
#include "LiveCompareIntervals.h"
#include <cstdint>
#include <limits>

namespace hypha::live_compare
{
// One host callback. Project/musical coordinates corroborate a continuous-clock address;
// neither a repeated position nor callback arrival order identifies its occurrence.
struct BlockClock
{
    std::int64_t clock = 0, project = 0;
    std::int32_t frames = 0;
    bool clockValid = false, projectValid = false, playing = false, afterGap = false;
    std::uint8_t clockBasis = 0, clockAuthority = 0, presentationSource = 0;
    bool outputPresentationValid = false;
    std::uint32_t outputPresentationSamples = 0, maximumDelaySamples = 0;
    LoopContext loop;
    // One wall reading for gap/expiry rejection and the chain display, never a PCM coordinate
    // or an independent alignment proof. Thread identity is diagnostic only.
    std::uint64_t wallNanos = 0, thread = 0;
    bool transportRestart = false; // explicit stopped host notification since the previous render
    bool wallGap = false; // raw scheduling observation; REAPER additionally requires paired continuity
    // Opaque non-RT chain/configuration fingerprints; no host pointer crosses into the ring.
    std::uint64_t runtimeChainA = 0, runtimeChainB = 0, runtimeRevision = 0;
    bool runtimeExpired = false; // freshness withholds this block; identity still fences changes
};

struct LoopAnchor
{
    bool linearKnown = false, loopKnown = false;
    std::int64_t runStart = 0, runProject = 0, clock = 0, project = 0, loopSamples = 0;
    LoopContext loop;
    LinearIntervals intervals;
};

inline bool checkedClockAdd (std::int64_t a, std::int64_t b, std::int64_t& result) noexcept
{
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((b > 0 && a > maximum - b) || (b < 0 && a < minimum - b)) return false;
    result = a + b;
    return true;
}

inline bool checkedClockSubtract (std::int64_t a, std::int64_t b, std::int64_t& result) noexcept
{
    constexpr auto minimum = std::numeric_limits<std::int64_t>::min();
    constexpr auto maximum = std::numeric_limits<std::int64_t>::max();
    if ((b > 0 && a < minimum + b) || (b < 0 && a > maximum + b)) return false;
    result = a - b;
    return true;
}

inline bool sameClockProof (const BlockClock& a, const BlockClock& b) noexcept
{
    return a.clockBasis == b.clockBasis && a.clockAuthority == b.clockAuthority
        && a.runtimeChainA == b.runtimeChainA && a.runtimeChainB == b.runtimeChainB
        && a.runtimeRevision == b.runtimeRevision
        && a.maximumDelaySamples == b.maximumDelaySamples
        && a.presentationSource == b.presentationSource
        && a.outputPresentationValid == b.outputPresentationValid
        && (! a.outputPresentationValid
            || a.outputPresentationSamples == b.outputPresentationSamples);
}

inline bool logicRender (const BlockClock& b) noexcept
{
    return b.clockBasis == static_cast<std::uint8_t> (ClockBasis::audioUnitRender)
        && b.clockAuthority == static_cast<std::uint8_t> (ClockAuthority::logicRender);
}

// Logic's render stamps name the compensated content sample, including cycle occurrences.
// The simultaneously observed PRE/POST distance must independently equal the host's output
// presentation difference. Missing, reverse or contradictory reports grant no permission.
// Logic reports its cycle range even with Cycle off, while PRE publishes a range only for an
// active cycle; with Cycle off on both sides the range is not part of the pair.
inline bool logicRenderPair (const BlockClock& pre, const BlockClock& post) noexcept
{
    std::int64_t distance = 0;
    return logicRender (pre) && logicRender (post) && pre.clockValid && post.clockValid
        && pre.projectValid && post.projectValid && pre.playing && post.playing
        && !pre.afterGap && !post.afterGap && pre.frames > 0 && pre.frames == post.frames
        && pre.presentationSource == 2 && post.presentationSource == 2
        && pre.outputPresentationValid && post.outputPresentationValid
        && pre.outputPresentationSamples >= post.outputPresentationSamples
        && checkedClockSubtract (pre.clock, post.clock, distance)
        && distance == static_cast<std::int64_t> (pre.outputPresentationSamples - post.outputPresentationSamples)
        && ((! pre.loop.active && ! post.loop.active) || pre.loop.sameRange (post.loop));
}

// One shared rule for a previously proven K, whether observed before or during an audition.
// This NEVER derives K, a PCM index, or a new lap from musical coordinates.
inline bool corroborateLoop (const LoopAnchor& anchor, const BlockClock& block,
                            std::int64_t start, double rate, BlockClock& verified,
                            const BlockClock* preProof = nullptr) noexcept
{
    if (! block.loop.usable (rate) || ! anchor.loopKnown || ! anchor.loop.sameRange (block.loop)) return false;
    if (start < anchor.clock)
    {
        std::int64_t offset = 0, expected = 0;
        return anchor.linearKnown && start >= anchor.runStart
            && checkedClockSubtract (start, anchor.runStart, offset)
            && checkedClockAdd (anchor.runProject, offset, expected)
            && block.project == expected;
    }
    std::int64_t offset = 0;
    if (! checkedClockSubtract (start, anchor.clock, offset)) return false;
    if (loopPositionMatches (anchor.loop, anchor.project, offset,
                            block.loop, block.project, rate)) return true;
    const auto ppq = anchor.loop.advance (offset, rate);
    const auto samplesPerBeat = 60.0 * rate / anchor.loop.bpm;
    const auto loopStart = static_cast<double> (anchor.project)
        + (anchor.loop.start - anchor.loop.ppq) * samplesPerBeat;
    const auto expected = static_cast<double> (anchor.project) + (ppq - anchor.loop.ppq) * samplesPerBeat;
    const auto length = (anchor.loop.end - anchor.loop.start) * samplesPerBeat;
    if (! std::isfinite (expected) || ! std::isfinite (length) || ppq < anchor.loop.start || ppq >= anchor.loop.end
        || std::abs (expected) >= 9007199254740992.0) return false;
    // Measured Studio Pro VST3 boundary: native samples clamp to the loop start, but PPQ
    // reports the delayed tail BEFORE the start. Only an already certified content-clock
    // address, a native-measured cycle and the reported positive PDC span can corroborate it.
    // PPQ never chooses start/K/PCM, and arbitrary clamps or another lap cannot pass this rule.
    if (preProof != nullptr && block.loop.ppq < block.loop.start)
    {
        const auto content = static_cast<std::uint8_t> (ClockBasis::vst3Continuous);
        const auto certified = static_cast<std::uint8_t> (ClockAuthority::certifiedContent);
        if (! block.playing || ! block.clockValid || ! block.projectValid || block.afterGap
            || block.frames <= 0 || preProof->afterGap || preProof->frames <= 0
            || start != block.clock || preProof->clockBasis != content || block.clockBasis != content
            || preProof->clockAuthority != certified || block.clockAuthority != certified
            || ! preProof->playing || ! preProof->clockValid || ! preProof->projectValid
            || ! preProof->outputPresentationValid || ! block.outputPresentationValid
            || preProof->presentationSource != 1 || block.presentationSource != 1
            || preProof->outputPresentationSamples <= block.outputPresentationSamples
            || anchor.loopSamples <= 0
            || std::abs (length - static_cast<double> (anchor.loopSamples)) > 1.01
            || std::abs (static_cast<double> (block.project) - loopStart) > 1.01) return false;
        const auto delay = preProof->outputPresentationSamples - block.outputPresentationSamples;
        const auto tail = (block.loop.start - block.loop.ppq) * samplesPerBeat;
        const auto tolerance = block.loop.beats (1, rate) + 1.0e-9;
        if (! std::isfinite (tail) || tail > static_cast<double> (delay) + 1.01
            || static_cast<double> (anchor.loopSamples) <= static_cast<double> (delay)
            || std::abs (block.loop.ppq + block.loop.end - block.loop.start - ppq) > tolerance)
            return false;
        verified.project = static_cast<std::int64_t> (std::llround (expected));
        verified.loop.ppq = ppq;
        return true;
    }
    // Measured Studio Pro AU boundary: PPQ clamps, native samples remain one lap before start.
    // A clamped native sample, an extra lap, or inconsistent metadata is not this observation.
    if (! LoopContext::identical (block.loop.ppq, block.loop.start)
        || static_cast<double> (block.project) >= loopStart
        || std::abs (static_cast<double> (block.project) - (expected - length)) > 1.01) return false;
    verified.project = static_cast<std::int64_t> (std::llround (expected));
    verified.loop.ppq = ppq;
    return true;
}
}
