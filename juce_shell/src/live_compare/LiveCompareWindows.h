#pragma once

#include "LiveCompareSession.h"

#include <atomic>
#include <cstdint>
#include <vector>

// Non-RT copies of aligned windows for MATCH and the content-offset estimate. POST's input history
// is contiguous over [start, end) of POST's clock; PRE's ring is read through the proven K. A copy
// is valid only if, afterwards, its source has not moved past the window.
namespace hypha::live_compare
{
// Interleaved stereo POST history [start, start + frames).
inline void copyPostHistory (const PostRenderer::HistoryView& view, std::int64_t start, std::int64_t frames,
                             std::vector<float>& out)
{
    const auto mask = view.frames - 1;
    for (std::int64_t i = 0; i < frames; ++i)
    {
        const auto slot = static_cast<std::size_t> ((start + i) & mask) * 2;
        out[static_cast<std::size_t> (i) * 2] = view.samples[slot].load (std::memory_order_relaxed);
        out[static_cast<std::size_t> (i) * 2 + 1] = view.samples[slot + 1].load (std::memory_order_relaxed);
    }
}

// Interleaved stereo PRE [start, start + frames). PRE keeps writing ahead of the window; the copy is
// valid when, afterwards, the window still lies inside PRE's current run and within the ring
// capacity of its write end.
inline bool copyPreRing (const Ring& ring, std::int64_t start, std::int64_t frames, std::vector<float>& out,
                        std::uint64_t expectedRun = 0)
{
    const auto& h = ring.header;
    const auto runBefore = h.run.load (std::memory_order_acquire);
    if (expectedRun != 0 && runBefore != expectedRun) return false;
    if (start < h.runStart.load (std::memory_order_acquire) || h.writeEnd.load (std::memory_order_acquire) < start + frames)
        return false;
    for (std::int64_t i = 0; i < frames; ++i)
    {
        out[static_cast<std::size_t> (i) * 2] = ring.samples[sampleSlot (start + i, 0)].load (std::memory_order_relaxed);
        out[static_cast<std::size_t> (i) * 2 + 1] = ring.samples[sampleSlot (start + i, 1)].load (std::memory_order_relaxed);
    }
    std::atomic_thread_fence (std::memory_order_acquire);
    return h.run.load (std::memory_order_relaxed) == runBefore
        && h.writeEnd.load (std::memory_order_relaxed) - start <= static_cast<std::int64_t> (ringCapacityFrames);
}
}
