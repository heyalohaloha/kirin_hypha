#pragma once
#include "ReaperTiming.h"

namespace hypha::live_compare
{
inline void observeReaperIntervals (LinearIntervals& w, const BlockClock& previous,
                                    const BlockClock& block, double rate, bool restart) noexcept
{
    if (!reaperProcessing (block)) { w = {}; return; }
    std::int64_t end = 0, previousEnd = 0;
    if (!checkedClockAdd (block.clock, block.frames, end)) { w = {}; return; }
    const bool linear = !restart && w.end == block.clock
        && checkedClockAdd (previous.project, previous.frames, previousEnd) && block.project == previousEnd;
    if (!linear)
    {
        // A crossing callback is admitted by the current block proof. A later native
        // reading separately proves inheritance: the map splits at the next callback
        // start, not at the actual loop boundary inside the preceding callback.
        const bool split = !restart && w.end == block.clock && block.project < previous.project
            && previous.loop.usable (rate) && block.loop.usable (rate)
            && previous.loop.sameRange (block.loop)
            && loopPositionMatches (previous.loop, previous.project, previous.frames,
                                    block.loop, block.project, rate);
        if (split)
        {
            w.start = w.split; w.project = w.splitProject;
            w.split = block.clock; w.splitProject = block.project;
        }
        else w = { block.clock, block.project, block.clock, block.project, end };
    }
    w.end = end;
}

inline std::int64_t reaperRuntimeAge (const BlockClock& pre, const BlockClock& post) noexcept
{
    if (!reaperProcessing (pre) || !reaperProcessing (post)
        || pre.runtimeChainA == 0 || pre.runtimeChainB == 0 || post.runtimeRevision == 0
        || pre.runtimeRevision == 0 || pre.runtimeChainA != post.runtimeChainA
        || pre.runtimeChainB != post.runtimeChainB || pre.maximumDelaySamples != post.maximumDelaySamples
        || post.maximumDelaySamples > ReaperTimingLimits::delay)
        return 0;
    return static_cast<std::int64_t> (post.maximumDelaySamples) + ReaperTimingLimits::scheduling;
}

// Every possible address in [E-U,E) must be covered. Count mappings, not correlations or
// the newest arrival. Across the first PRE wrap the previous map remains until it ages out;
// POST can therefore still address its delayed tail before POST has observed any wrap.
inline bool reaperWindowAddress (const LoopAnchor& anchor, const BlockClock& pre,
                                 const BlockClock& post, double rate, std::int64_t capacity,
                                 std::int64_t& start) noexcept
{
    const auto upper = reaperRuntimeAge (pre, post);
    if (pre.runtimeExpired || post.runtimeExpired || upper <= 0 || upper >= capacity || pre.frames <= 0 || post.frames <= 0
        || pre.frames > ReaperTimingLimits::scheduling || post.frames > ReaperTimingLimits::scheduling
        || !pre.playing || !post.playing || !pre.clockValid || !post.clockValid
        || !pre.projectValid || !post.projectValid || pre.afterGap || post.afterGap
        || !pre.loop.usable (rate) || !post.loop.usable (rate) || !pre.loop.sameRange (post.loop)
        || (anchor.loopSamples > 0 && anchor.loopSamples <= upper)) return false;
    const auto& w = anchor.intervals;
    std::int64_t end = 0, oldest = 0;
    if (!checkedClockAdd (pre.clock, pre.frames, end) || w.end != end
        || !checkedClockSubtract (end, upper, oldest) || oldest < w.start
        || w.start > w.split || w.split > w.end) return false;
    int candidates = 0;
    const auto inspect = [&] (std::int64_t a, std::int64_t p, std::int64_t b)
    {
        std::int64_t offset = 0, address = 0, finish = 0;
        if (a >= b || !checkedClockSubtract (post.project, p, offset)
            || !checkedClockAdd (a, offset, address) || !checkedClockAdd (address, post.frames, finish)
            || address < a || address >= b || address < oldest || finish > b) return;
        start = address; ++candidates;
    };
    inspect (w.start, w.project, w.split);
    inspect (w.split, w.splitProject, w.end);
    return candidates == 1;
}
}
