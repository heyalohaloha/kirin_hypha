#pragma once
#include "LiveCompareBlockClock.h"
#include "LiveCompareCalibration.h"
#include "LiveCompareLoopCycle.h"
#include "LiveCompareLoopEntry.h"
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
    std::atomic<std::uint32_t> flags { 0 }; // bits0..2: active/linear/loop; bits8..15: generation cause
    std::atomic<std::uint32_t> proof { 0 }, outputPresentation { 0 }, maximumDelay { 0 };
    std::atomic<std::int64_t> clock { 0 }, project { 0 }, origin { 0 }, start { 0 };
    std::atomic<std::int32_t> frames { 0 };
    std::atomic<std::int64_t> loopClock { 0 }, loopProject { 0 }, loopSamples { 0 };
    std::atomic<double> ppq { 0 }, loopStart { 0 }, loopEnd { 0 }, bpm { 0 };
};

struct TimingSnapshot
{
    std::uint64_t generation = 0;
    std::uint64_t ownerA = 0, ownerB = 0;
    bool active = false;
    TimelineBreak cause = TimelineBreak::unknown; // belongs to this whole PRE generation
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
    const auto cause = (flags >> 8u) & 0xffu;
    next.cause = cause > 0 && cause <= static_cast<std::uint32_t> (TimelineBreak::compensationChanged)
        ? static_cast<TimelineBreak> (cause) : TimelineBreak::unknown;
    const auto proof = h.proof.load (std::memory_order_relaxed);
    next.block.clockBasis = static_cast<std::uint8_t> (proof & 0xffu);
    next.block.clockAuthority = static_cast<std::uint8_t> ((proof >> 8u) & 0xffu);
    next.block.presentationSource = static_cast<std::uint8_t> ((proof >> 16u) & 0xffu);
    next.block.outputPresentationValid = (proof & (1u << 24u)) != 0;
    next.block.outputPresentationSamples = h.outputPresentation.load (std::memory_order_relaxed);
    next.block.maximumDelaySamples = h.maximumDelay.load (std::memory_order_relaxed);
    next.origin = h.origin.load (std::memory_order_relaxed);
    next.anchor.runStart = h.start.load (std::memory_order_relaxed);
    if (! checkedClockSubtract (next.anchor.runStart, next.origin, next.anchor.runProject))
        next.anchor.linearKnown = false;
    next.block.clock = h.clock.load (std::memory_order_relaxed);
    next.block.project = h.project.load (std::memory_order_relaxed);
    next.block.frames = h.frames.load (std::memory_order_relaxed);
    next.block.clockValid = next.block.projectValid = next.block.playing = next.active;
    next.anchor.clock = h.loopClock.load (std::memory_order_relaxed);
    next.anchor.project = h.loopProject.load (std::memory_order_relaxed);
    next.anchor.loopSamples = h.loopSamples.load (std::memory_order_relaxed);
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
        const auto cycle = cycles.observe (block, rate);
        const auto proof = proofWord (block);
        const bool proofChanged = havePrevious && ! sameClockProof (previousBlock, block);
        if (! havePrevious || movement.broken || cycle.changed || proofChanged)
        {
            ++generation;
            generationCause = ! active && movement.cause != TimelineBreak::none ? movement.cause
                : proofChanged ? TimelineBreak::clockProofChanged
                : movement.cause != TimelineBreak::none ? movement.cause : TimelineBreak::unknown;
            linearKnown = loopKnown = false;
            start = block.clock;
        }
        if (active && ! block.loop.active)
        {
            // The timeline has checked the second and all later project movements.
            linearKnown = havePrevious && ! movement.broken
                && checkedClockSubtract (block.clock, block.project, origin);
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
        h.flags.store ((active ? (1u | (linearKnown ? 2u : 0u) | (loopKnown ? 4u : 0u)) : 0u)
                           | (static_cast<std::uint32_t> (generationCause) << 8u),
                       std::memory_order_relaxed);
        h.clock.store (block.clock, std::memory_order_relaxed);
        h.project.store (block.project, std::memory_order_relaxed);
        h.frames.store (block.frames, std::memory_order_relaxed);
        h.proof.store (proof, std::memory_order_relaxed);
        h.outputPresentation.store (block.outputPresentationSamples, std::memory_order_relaxed);
        h.maximumDelay.store (block.maximumDelaySamples, std::memory_order_relaxed);
        h.origin.store (origin, std::memory_order_relaxed);
        h.start.store (start, std::memory_order_relaxed);
        h.loopClock.store (loopClock, std::memory_order_relaxed);
        h.loopProject.store (loopProject, std::memory_order_relaxed);
        h.loopSamples.store (cycle.known ? cycle.samples : 0, std::memory_order_relaxed);
        h.ppq.store (loop.ppq, std::memory_order_relaxed);
        h.loopStart.store (loop.start, std::memory_order_relaxed);
        h.loopEnd.store (loop.end, std::memory_order_relaxed);
        h.bpm.store (loop.bpm, std::memory_order_relaxed);
        h.sequence.store (seq + 2, std::memory_order_release);
        havePrevious = active;
        previousBlock = block;
        return movement;
    }
private:
    static std::uint32_t proofWord (const BlockClock& block) noexcept
    {
        return std::uint32_t (block.clockBasis)
            | (std::uint32_t (block.clockAuthority) << 8u)
            | (std::uint32_t (block.presentationSource) << 16u)
            | (block.outputPresentationValid ? (1u << 24u) : 0u);
    }
    LoopTimeline timeline;
    LoopCycleMeter cycles;
    std::uint64_t generation = 0;
    bool havePrevious = false, linearKnown = false, loopKnown = false;
    BlockClock previousBlock;
    TimelineBreak generationCause = TimelineBreak::unknown;
    std::int64_t origin = 0, start = 0, loopClock = 0, loopProject = 0;
    LoopContext loop;
};

struct TimingEvidence
{
    bool valid = false;
    std::int64_t k = 0, postClock = 0;
    std::uint64_t preGeneration = 0;
    std::uint64_t ownerA = 0, ownerB = 0;
    std::int64_t loopSamples = 0;
    LoopEntryKind kind = LoopEntryKind::none;
    LoopEntryFailure failure = LoopEntryFailure::none;
};

// POST Audio Thread. Linear project joins can be observed without an audition or PCM copy.
// An initial loop may create K only from an explicit host certificate, a documented delay bound,
// or a positive presentation-latency proof; repeated musical positions alone never create it.
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
        const bool postProofChanged = havePostProof && ! sameClockProof (previousPost, post);
        if (coherent) { previous = pre; generation = pre.generation; haveSource = true; }
        std::int64_t start = 0, preEnd = 0, age = 0;
        const bool addressValid = ! valid
            || (checkedClockSubtract (post.clock, k, start)
                && checkedClockAdd (pre.block.clock, pre.block.frames, preEnd)
                && checkedClockSubtract (preEnd, start, age));
        if (! addressValid) invalidate();
        auto verified = post;
        const bool corroborated = valid && post.loop.active
            && corroborateLoop (pre.anchor, post, start, rate, verified, &pre.block);
        // Cycle and timeline observers must consume the SAME corroborated coordinates.
        // Feeding the raw clamp into the cycle meter would revoke a proven K every wrap.
        const auto cycle = postCycles.observe (corroborated ? verified : post, rate);
        const auto movement = timeline.observe (corroborated ? verified : post, rate,
                                                coherent ? (age > 0 && age < capacity ? age : 0) : lastAge);
        if (! active || sourceChanged || postProofChanged || movement.broken || cycle.changed)
            invalidate();
        if (coherent && active && ! sourceChanged && ! movement.broken && havePrevious
            && ! post.loop.active && ! pre.anchor.loopKnown && pre.anchor.linearKnown)
        {
            std::int64_t postOrigin = 0, candidate = 0, address = 0;
            std::int64_t addressEnd = 0, availableEnd = 0;
            if (checkedClockSubtract (post.clock, post.project, postOrigin)
                && checkedClockSubtract (postOrigin, pre.origin, candidate)
                && checkedClockSubtract (post.clock, candidate, address)
                && checkedClockAdd (address, post.frames, addressEnd)
                && checkedClockAdd (pre.block.clock, pre.block.frames, availableEnd)
                && address >= pre.anchor.runStart && addressEnd <= availableEnd)
            {
                if (valid && candidate != k && profile.invalidateOnDisagreement) invalidate();
                if (streak == 0 || candidate != last) streak = 1;
                else if (streak < profile.streak) ++streak;
                last = candidate;
                if (streak >= profile.streak)
                { k = candidate; valid = true; kind = LoopEntryKind::linear; }
            }
        }
        LoopEntryFailure failure = LoopEntryFailure::none;
        if (coherent && active && ! sourceChanged && ! postProofChanged && ! movement.broken
            && havePrevious && post.loop.active && ! valid)
        {
            const auto entry = initialLoopCandidate (pre.anchor, pre.block, post,
                                                     cycle.known ? cycle.samples : 0,
                                                     rate, capacity);
            failure = entry.failure;
            if (entry.valid)
            {
                if (streak == 0 || entry.k != last || entry.kind != candidateKind) streak = 1;
                else if (streak < profile.streak) ++streak;
                last = entry.k;
                candidateKind = entry.kind;
                if (streak >= profile.streak)
                { k = entry.k; valid = true; kind = entry.kind; }
            }
            else if (entry.failure != LoopEntryFailure::observingCycle)
                streak = 0;
        }
        havePrevious = active;
        previousPost = post;
        havePostProof = active;
        std::int64_t observedEnd = 0, availableEnd = 0;
        const bool inObservedRange = valid && addressValid && start >= pre.anchor.runStart
            && checkedClockAdd (start, post.frames, observedEnd)
            && checkedClockAdd (pre.block.clock, pre.block.frames, availableEnd)
            && observedEnd <= availableEnd && age <= capacity;
        // On the block that completes calibration, start/age above still use the old state.
        std::int64_t linearStart = 0, linearEnd = 0, linearAvailableEnd = 0, linearAge = 0;
        const bool linearReady = valid && ! post.loop.active && pre.anchor.linearKnown
            && ! pre.anchor.loopKnown && checkedClockSubtract (post.clock, k, linearStart)
            && checkedClockAdd (linearStart, post.frames, linearEnd)
            && checkedClockAdd (pre.block.clock, pre.block.frames, linearAvailableEnd)
            && checkedClockSubtract (linearAvailableEnd, linearStart, linearAge)
            && linearStart >= pre.anchor.runStart && linearEnd <= linearAvailableEnd
            && linearAge <= capacity;
        if (coherent && valid && age > 0 && age < capacity) lastAge = age;
        const bool loopReady = valid && kind != LoopEntryKind::linear
            && loopAddressIsCurrent (pre.anchor, pre.block, post, k, rate, capacity);
        return { coherent && active && (linearReady || loopReady || (corroborated && inObservedRange)),
                 k, post.clock, generation, pre.ownerA, pre.ownerB,
                 cycle.known ? cycle.samples : 0, kind,
                 valid ? LoopEntryFailure::none : failure };
    }
    // A torn metadata snapshot is not a source discontinuity. Track POST's continuity using the
    // last anchor, but provide NO evidence for this callback. The next coherent PRE generation
    // must still agree. This preserves K through a writer overlap, never through a seek/stop.
    TimingEvidence unavailable (const BlockClock& post, double rate, std::int64_t capacity) noexcept
    { return observe (previous, post, rate, capacity, false); }
    void reset() noexcept { *this = TimingObserver (profile); }
private:
    void invalidate() noexcept
    {
        valid = false; streak = 0; havePrevious = false; kind = LoopEntryKind::none;
        candidateKind = LoopEntryKind::none;
    }
    LoopTimeline timeline;
    LoopCycleMeter postCycles;
    CalibrationProfile profile;
    TimingSnapshot previous;
    std::uint64_t generation = 0;
    std::int64_t k = 0, last = 0, lastAge = 0;
    int streak = 0;
    LoopEntryKind kind = LoopEntryKind::none, candidateKind = LoopEntryKind::none;
    BlockClock previousPost;
    bool valid = false, haveSource = false, havePrevious = false, havePostProof = false;
};
}
