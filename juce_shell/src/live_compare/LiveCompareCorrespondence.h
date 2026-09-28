#pragma once

#include "LiveCompareRing.h"

#include <cstdint>
#include <limits>

namespace hypha::live_compare
{
// Host-profile values. They were first measured in Studio Pro 8.1.2 and Pro Tools 2026.4 and are
// qualified per host and buffer setting, never hard-coded as invariants.
struct CalibrationProfile
{
    std::int32_t streak = 8;               // equal join candidates needed before K is trusted
    bool invalidateOnDisagreement = true;  // M1: one disagreeing candidate invalidates K
};

enum class Verdict : std::uint8_t
{
    accepted,
    foreignRing,  // the ring is not stamped for this pair and sample rate
    noClock,      // this block has no continuous clock
    stopped,      // the host is not playing
    calibrating,  // K is not proven (never calibrated, after a gap, or after a disagreement)
    writing,      // PRE was writing when POST started to read
    beforeRun,    // the range starts before PRE's current run
    notWritten,   // the range ends after what PRE has written
    overwritten,  // the range is older than the ring capacity
    torn          // PRE wrote while POST was reading
};

struct Decision
{
    Verdict verdict = Verdict::noClock;
    std::int64_t preStart = 0;       // PRE clock of the first frame, when accepted
    std::int64_t k = 0;
    bool kValid = false;
    bool candidateSeen = false;
    std::int64_t candidate = 0;
    bool invalidatedByDisagreement = false;
};

// POST, Audio Thread. Maps POST's continuous clock to PRE's through K, calibrated from steady
// project-time joins, and copies PRE's samples only when every frame of the block is proven.
class Consumer
{
public:
    explicit Consumer (CalibrationProfile profileIn = {}) noexcept : profile (profileIn) {}

    // out receives n frames per channel only when the verdict is accepted; otherwise its contents
    // are unspecified and must not be played. out is preallocated scratch, never the host buffer.
    Decision process (const Ring& ring, std::uint64_t pairKey, std::uint32_t sampleRate,
                      const BlockClock& block, float* const* out, int outChannels) noexcept
    {
        Decision decision;
        if (! ring.matches (pairKey, sampleRate))
        {
            invalidate();
            havePrevious = false;
            decision.verdict = Verdict::foreignRing;
            return decision;
        }
        if (block.afterGap)
            invalidate();
        if (! block.clockValid || block.frames <= 0)
        {
            havePrevious = false;
            decision.verdict = Verdict::noClock;
            return fill (decision);
        }

        const bool continuous = havePrevious && block.projectValid
                             && block.project == previousProject + previousFrames;
        std::int64_t candidate = 0;
        if (continuous && block.playing && joinCandidate (ring.header, block, candidate))
        {
            decision.candidateSeen = true;
            decision.candidate = candidate;
            if (kValid && candidate != k && profile.invalidateOnDisagreement)
            {
                invalidate();
                decision.invalidatedByDisagreement = true;
            }
            feed (candidate);
        }
        previousProject = block.project;
        previousFrames = block.frames;
        havePrevious = block.playing && block.projectValid;

        if (! block.playing)
        {
            decision.verdict = Verdict::stopped;
            return fill (decision);
        }
        if (! kValid)
        {
            decision.verdict = Verdict::calibrating;
            return fill (decision);
        }
        decision.preStart = block.clock - k;
        decision.verdict = read (ring, decision.preStart, block.frames, out, outChannels);
        return fill (decision);
    }

    void reset() noexcept
    {
        invalidate();
        havePrevious = false;
    }

    bool calibrated() const noexcept { return kValid; }
    std::int64_t offset() const noexcept { return k; }

private:
    static constexpr std::int64_t noCandidate = std::numeric_limits<std::int64_t>::min();

    Decision fill (Decision decision) const noexcept
    {
        decision.k = k;
        decision.kValid = kValid;
        return decision;
    }

    void invalidate() noexcept
    {
        kValid = false;
        streak = 0;
        last = noCandidate;
    }

    void feed (std::int64_t candidate) noexcept
    {
        streak = candidate == last ? streak + 1 : 1;
        last = candidate;
        if (streak >= profile.streak)
        {
            k = candidate;
            kValid = true;
        }
    }

    // The latest PRE block whose project range holds this block's first frame gives
    // K = POST clock - PRE clock of the same project frame.
    static bool joinCandidate (const RingHeader& h, const BlockClock& block, std::int64_t& candidate) noexcept
    {
        const auto count = h.published.load (std::memory_order_acquire);
        const auto limit = std::min<std::uint64_t> (count, joinSearchDepth);
        for (std::uint64_t back = 1; back <= limit; ++back)
        {
            const auto& d = h.descriptors[(count - back) % ringDescriptors];
            const auto seq = d.seq.load (std::memory_order_acquire);
            if ((seq & 1u) != 0)
                continue;
            const auto clock = d.clock.load (std::memory_order_relaxed);
            const auto project = d.project.load (std::memory_order_relaxed);
            const auto frames = d.frames.load (std::memory_order_relaxed);
            const auto flags = d.flags.load (std::memory_order_relaxed);
            std::atomic_thread_fence (std::memory_order_acquire);
            if (d.seq.load (std::memory_order_relaxed) != seq)
                continue;
            if ((flags & (descriptorPlaying | descriptorProjectValid)) != (descriptorPlaying | descriptorProjectValid))
                continue;
            if (block.project >= project && block.project < project + frames)
            {
                candidate = block.clock - (clock + (block.project - project));
                return true;
            }
        }
        return false;
    }

    static Verdict read (const Ring& ring, std::int64_t start, std::int32_t frames,
                         float* const* out, int outChannels) noexcept
    {
        const auto& h = ring.header;
        const auto seq = h.seq.load (std::memory_order_acquire);
        if ((seq & 1u) != 0)
            return Verdict::writing;
        const auto runStart = h.runStart.load (std::memory_order_relaxed);
        const auto writeEnd = h.writeEnd.load (std::memory_order_relaxed);
        const auto end = start + frames;
        Verdict verdict = Verdict::accepted;
        if (start < runStart)
            verdict = Verdict::beforeRun;
        else if (writeEnd < end)
            verdict = Verdict::notWritten;
        else if (writeEnd - start > static_cast<std::int64_t> (ringCapacityFrames))
            verdict = Verdict::overwritten;
        if (verdict == Verdict::accepted && out != nullptr)
            for (int channel = 0; channel < outChannels; ++channel)
            {
                const auto source = static_cast<std::uint32_t> (std::min (channel, static_cast<int> (ringChannels) - 1));
                for (std::int32_t i = 0; i < frames; ++i)
                    out[channel][i] = ring.samples[sampleSlot (start + i, source)].load (std::memory_order_relaxed);
            }
        std::atomic_thread_fence (std::memory_order_acquire);
        if (h.seq.load (std::memory_order_relaxed) != seq)
            return Verdict::torn;
        return verdict;
    }

    CalibrationProfile profile;
    bool kValid = false;
    std::int64_t k = 0;
    std::int64_t last = noCandidate;
    std::int32_t streak = 0;
    bool havePrevious = false;
    std::int64_t previousProject = 0;
    std::int32_t previousFrames = 0;
};
}
