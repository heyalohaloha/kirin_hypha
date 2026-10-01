#pragma once
#include "LiveCompareLoop.h"
#include <cstdint>

namespace hypha::live_compare
{
// One host callback. Project/musical coordinates corroborate a continuous-clock address;
// neither a repeated position nor callback arrival order identifies its occurrence.
struct BlockClock
{
    std::int64_t clock = 0, project = 0;
    std::int32_t frames = 0;
    bool clockValid = false, projectValid = false, playing = false, afterGap = false;
    LoopContext loop;
};

struct LoopAnchor
{
    bool linearKnown = false, loopKnown = false;
    std::int64_t runStart = 0, runProject = 0, clock = 0, project = 0;
    LoopContext loop;
};

// One shared rule for a previously proven K, whether observed before or during an audition.
// This NEVER derives K, a PCM index, or a new lap from musical coordinates.
inline bool corroborateLoop (const LoopAnchor& anchor, const BlockClock& block,
                            std::int64_t start, double rate, BlockClock& verified) noexcept
{
    if (! block.loop.usable (rate) || ! anchor.loopKnown || ! anchor.loop.sameRange (block.loop)) return false;
    if (start < anchor.clock)
        return anchor.linearKnown && start >= anchor.runStart
            && block.project == anchor.runProject + start - anchor.runStart;
    if (loopPositionMatches (anchor.loop, anchor.project, start - anchor.clock,
                            block.loop, block.project, rate)) return true;
    // Measured Studio Pro AU boundary: PPQ clamps, native samples remain one lap before start.
    // A clamped native sample, an extra lap, or inconsistent metadata is not this observation.
    if (! LoopContext::identical (block.loop.ppq, block.loop.start)) return false;
    const auto ppq = anchor.loop.advance (start - anchor.clock, rate);
    const auto samplesPerBeat = 60.0 * rate / anchor.loop.bpm;
    const auto loopStart = static_cast<double> (anchor.project)
        + (anchor.loop.start - anchor.loop.ppq) * samplesPerBeat;
    const auto expected = static_cast<double> (anchor.project) + (ppq - anchor.loop.ppq) * samplesPerBeat;
    const auto length = (anchor.loop.end - anchor.loop.start) * samplesPerBeat;
    if (! std::isfinite (expected) || ! std::isfinite (length) || ppq < anchor.loop.start || ppq >= anchor.loop.end
        || static_cast<double> (block.project) >= loopStart
        || std::abs (static_cast<double> (block.project) - (expected - length)) > 1.01
        || std::abs (expected) >= 9007199254740992.0) return false;
    verified.project = static_cast<std::int64_t> (std::llround (expected));
    verified.loop.ppq = ppq;
    return true;
}
}
