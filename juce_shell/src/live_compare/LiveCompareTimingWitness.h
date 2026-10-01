#pragma once
#include "LiveCompareBlockClock.h"
#include "LiveCompareCalibration.h"
#include <atomic>
#include <cstdint>

namespace hypha::live_compare
{
// Fixed-size metadata, not a PCM history. Its generation spans only one verified PRE timeline.
// The sequence covers EVERY field; no reader combines a new clock with an old origin/anchor.
struct TimingHeader
{
    std::atomic<std::uint64_t> sequence { 0 }, generation { 0 };
    std::atomic<std::uint64_t> ownerA { 0 }, ownerB { 0 }; // immutable, one PRE mapping lifetime
    std::atomic<std::uint32_t> flags { 0 }; // 1: active clock; 2: linear origin; 4: loop anchor
    std::atomic<std::int64_t> clock { 0 }, project { 0 }, origin { 0 }, start { 0 };
    std::atomic<std::int32_t> frames { 0 };
    std::atomic<std::int64_t> loopClock { 0 }, loopProject { 0 };
    std::atomic<double> ppq { 0 }, loopStart { 0 }, loopEnd { 0 }, bpm { 0 };
};

struct TimingSnapshot
{
    std::uint64_t generation = 0;
    std::uint64_t ownerA = 0, ownerB = 0;
    bool active = false;
    std::int64_t origin = 0;
    BlockClock block;
    LoopAnchor anchor;
};

inline bool readTiming (const TimingHeader& h, TimingSnapshot& result) noexcept
{
    const auto seq = h.sequence.load (std::memory_order_acquire);
    if ((seq & 1u) != 0) return false;
    TimingSnapshot next;
    next.generation = h.generation.load (std::memory_order_relaxed);
    next.ownerA = h.ownerA.load (std::memory_order_relaxed);
    next.ownerB = h.ownerB.load (std::memory_order_relaxed);
    const auto flags = h.flags.load (std::memory_order_relaxed);
    next.active = (flags & 1u) != 0;
    next.anchor.linearKnown = (flags & 2u) != 0;
    next.anchor.loopKnown = (flags & 4u) != 0;
    next.origin = h.origin.load (std::memory_order_relaxed);
    next.anchor.runStart = h.start.load (std::memory_order_relaxed);
    next.anchor.runProject = next.anchor.runStart - next.origin;
    next.block.clock = h.clock.load (std::memory_order_relaxed);
    next.block.project = h.project.load (std::memory_order_relaxed);
    next.block.frames = h.frames.load (std::memory_order_relaxed);
    next.block.clockValid = next.block.projectValid = next.block.playing = next.active;
    next.anchor.clock = h.loopClock.load (std::memory_order_relaxed);
    next.anchor.project = h.loopProject.load (std::memory_order_relaxed);
    next.anchor.loop = { next.anchor.loopKnown, next.anchor.loopKnown,
        h.ppq.load (std::memory_order_relaxed), h.loopStart.load (std::memory_order_relaxed),
        h.loopEnd.load (std::memory_order_relaxed), h.bpm.load (std::memory_order_relaxed) };
    next.block.loop = next.anchor.loop;
    std::atomic_thread_fence (std::memory_order_acquire);
    if (seq != h.sequence.load (std::memory_order_relaxed)) return false;
    result = next;
    return true;
}

// PRE Audio Thread. Always records clock continuity; no input pointer, samples, demand, or gain.
// A stop/gap/missing clock/seek/range or tempo edit fences the old origin immediately.
class TimingPublisher
{
public:
    TimelineStep observe (TimingHeader& h, const BlockClock& block, double rate) noexcept
    {
        if (h.sequence.load (std::memory_order_relaxed) == 0)
        {
            timeline.reset();
            havePrevious = false;
        }
        const bool active = block.playing && block.clockValid && block.projectValid && block.frames > 0;
        const auto movement = timeline.observe (block, rate);
        if (! havePrevious || movement.broken)
        {
            ++generation;
            linearKnown = loopKnown = false;
            start = block.clock;
        }
        if (active && ! block.loop.active)
        {
            // The timeline has checked the second and all later project movements.
            linearKnown = havePrevious && ! movement.broken;
            origin = block.clock - block.project;
            loopKnown = false;
        }
        if (active && block.loop.usable (rate) && (! loopKnown || movement.broken))
        {
            loopClock = block.clock;
            loopProject = block.project;
            loop = block.loop;
            loopKnown = true;
        }
        const auto seq = h.sequence.load (std::memory_order_relaxed);
        h.sequence.store (seq + 1, std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_release);
        h.generation.store (generation, std::memory_order_release); // individual fence for an already-seeded PCM consumer
        h.flags.store (active ? (1u | (linearKnown ? 2u : 0u) | (loopKnown ? 4u : 0u)) : 0u,
                       std::memory_order_relaxed);
        h.clock.store (block.clock, std::memory_order_relaxed);
        h.project.store (block.project, std::memory_order_relaxed);
        h.frames.store (block.frames, std::memory_order_relaxed);
        h.origin.store (origin, std::memory_order_relaxed);
        h.start.store (start, std::memory_order_relaxed);
        h.loopClock.store (loopClock, std::memory_order_relaxed);
        h.loopProject.store (loopProject, std::memory_order_relaxed);
        h.ppq.store (loop.ppq, std::memory_order_relaxed);
        h.loopStart.store (loop.start, std::memory_order_relaxed);
        h.loopEnd.store (loop.end, std::memory_order_relaxed);
        h.bpm.store (loop.bpm, std::memory_order_relaxed);
        h.sequence.store (seq + 2, std::memory_order_release);
        havePrevious = active;
        return movement;
    }
private:
    LoopTimeline timeline;
    std::uint64_t generation = 0;
    bool havePrevious = false, linearKnown = false, loopKnown = false;
    std::int64_t origin = 0, start = 0, loopClock = 0, loopProject = 0;
    LoopContext loop;
};

struct TimingEvidence
{
    bool valid = false;
    std::int64_t k = 0, postClock = 0;
    std::uint64_t preGeneration = 0;
    std::uint64_t ownerA = 0, ownerB = 0;
};

// POST Audio Thread. Linear project joins can be observed without an audition or PCM copy.
// Repeated loop positions can only corroborate an existing K, NEVER create a new one.
class TimingObserver
{
public:
    explicit TimingObserver (CalibrationProfile profileIn = {}) noexcept : profile (profileIn) {}
    TimingEvidence observe (const TimingSnapshot& pre, const BlockClock& post, double rate,
                            std::int64_t capacity, bool coherent = true) noexcept
    {
        const bool active = pre.active && post.playing && post.clockValid && post.projectValid
            && post.frames > 0 && ! post.afterGap;
        const bool sourceChanged = haveSource && (generation != pre.generation
            || previous.ownerA != pre.ownerA || previous.ownerB != pre.ownerB);
        if (coherent) { previous = pre; generation = pre.generation; haveSource = true; }
        auto verified = post;
        const auto start = valid ? post.clock - k : 0;
        const auto age = valid ? pre.block.clock + pre.block.frames - start : 0;
        const bool corroborated = valid && post.loop.active
            && corroborateLoop (pre.anchor, post, start, rate, verified);
        const auto movement = timeline.observe (corroborated ? verified : post, rate,
                                                coherent ? (age > 0 && age < capacity ? age : 0) : lastAge);
        if (! active || sourceChanged || movement.broken)
            invalidate();
        if (coherent && active && ! sourceChanged && ! movement.broken && havePrevious
            && ! post.loop.active && ! pre.anchor.loopKnown && pre.anchor.linearKnown)
        {
            const auto candidate = (post.clock - post.project) - pre.origin;
            const auto address = post.clock - candidate;
            if (address >= pre.anchor.runStart && address + post.frames <= pre.block.clock + pre.block.frames)
            {
                if (valid && candidate != k && profile.invalidateOnDisagreement) invalidate();
                if (streak == 0 || candidate != last) streak = 1;
                else if (streak < profile.streak) ++streak;
                last = candidate;
                if (streak >= profile.streak) { k = candidate; valid = true; }
            }
        }
        havePrevious = active;
        const bool inObservedRange = valid && start >= pre.anchor.runStart
            && start + post.frames <= pre.block.clock + pre.block.frames && age <= capacity;
        // On the block that completes calibration, start/age above still use the old state.
        const bool linearReady = valid && ! post.loop.active && pre.anchor.linearKnown
            && ! pre.anchor.loopKnown && post.clock - k >= pre.anchor.runStart
            && post.clock - k + post.frames <= pre.block.clock + pre.block.frames
            && pre.block.clock + pre.block.frames - (post.clock - k) <= capacity;
        if (coherent && valid && age > 0 && age < capacity) lastAge = age;
        return { coherent && active && (linearReady || (corroborated && inObservedRange)), k, post.clock,
                 generation, pre.ownerA, pre.ownerB };
    }
    // A torn metadata snapshot is not a source discontinuity. Track POST's continuity using the
    // last anchor, but provide NO evidence for this callback. The next coherent PRE generation
    // must still agree. This preserves K through a writer overlap, never through a seek/stop.
    TimingEvidence unavailable (const BlockClock& post, double rate, std::int64_t capacity) noexcept
    { return observe (previous, post, rate, capacity, false); }
    void reset() noexcept { *this = TimingObserver (profile); }
private:
    void invalidate() noexcept { valid = false; streak = 0; havePrevious = false; }
    LoopTimeline timeline;
    CalibrationProfile profile;
    TimingSnapshot previous;
    std::uint64_t generation = 0;
    std::int64_t k = 0, last = 0, lastAge = 0;
    int streak = 0;
    bool valid = false, haveSource = false, havePrevious = false;
};
}
