#pragma once

#include "LiveCompareRing.h"

#include <cstdint>
#include <limits>

namespace hypha::live_compare
{
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
    torn,         // PRE wrote while POST was reading
    loopUnproven, // no unique K before entering a loop
    loopWaiting   // known K retained, but these loop coordinates do not prove this block
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
    bool timelineChanged = false;
    std::uint64_t run = 0;
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
        // A fresh entry has never authorised PRE. A startup stop/clock hole must withhold
        // output, not consume its one admission through LoopTimeline's invalid first block.
        // Preparation independently fences that observation and must supply a complete,
        // current generation/owner proof before adoption. After any successful admission,
        // initialAdmission is false: this cannot reopen a revoked comparison or Blind.
        if (initialAdmission && ! kValid
            && (! block.playing || ! block.clockValid || ! block.projectValid
                || block.frames <= 0 || block.afterGap))
        {
            invalidate(); // discard partial linear streaks as well as the observed timeline
            havePrevious = false;
            haveRun = false;
            timeline.reset();
            decision.verdict = ! block.playing ? Verdict::stopped
                : ! block.clockValid || ! block.projectValid || block.frames <= 0
                    ? Verdict::noClock : Verdict::calibrating;
            return fill (decision);
        }
        // An entry may wait for PRE's first demanded PCM, even if POST is scheduled first.
        // Previous sessions' samples cannot make a new clock-only preparation audible.
        if (initialAdmission && block.playing && block.clockValid && block.projectValid && ! initialPcmReady (ring))
        {
            decision.verdict = Verdict::notWritten;
            return fill (decision);
        }
        // An initial LOOP wrap is the expected transport shape, not a broken proof. The timing
        // preparation observed before this renderer owns the only admission decision; until its
        // generation-bound evidence is adopted, keep POST and leave this one-shot admission open.
        // Otherwise the ordinary timeline observer can race the preparation at the first wrap and
        // permanently close an otherwise valid initial-loop entry merely because one thread ran
        // first. Non-LOOP entries retain the ordinary linear calibration path below.
        if (initialAdmission && ! kValid && block.playing && block.clockValid
            && block.projectValid && block.loop.active)
        {
            decision.verdict = Verdict::loopUnproven;
            return fill (decision);
        }
        if (preparedGeneration != 0)
        {
            // After admission the ordinary PCM seqlock is authoritative. Clock-only writer
            // overlap is not a new reason to interrupt an otherwise proven block. Only an
            // actual PRE generation/lifetime change fences this prepared origin.
            if (! preparedTimingCurrent (ring.header))
            {
                invalidate(); initialAdmission = false;
                preparedGeneration = 0;
                decision.timelineChanged = true;
                decision.verdict = ! block.playing ? Verdict::stopped
                    : ! block.clockValid ? Verdict::noClock : Verdict::calibrating;
                return fill (decision);
            }
        }
        const auto run = ring.header.run.load (std::memory_order_acquire);
        const auto runStart = ring.header.runStart.load (std::memory_order_acquire);
        const auto writeEnd = ring.header.writeEnd.load (std::memory_order_acquire);
        const auto age = kValid ? writeEnd - (block.clock - k) : 0;
        auto verifiedBlock = block;
        const bool loopVerified = kValid && block.loop.active
            && loopJoin (ring.header, block, block.clock - k, sampleRate, verifiedBlock);
        const auto timelineStep = timeline.observe (loopVerified ? verifiedBlock : block, sampleRate,
            age > 0 && age < ringCapacityFrames ? age : 0);
        decision.run = run;
        decision.timelineChanged = timelineStep.broken || (haveRun && run != previousRun);
        previousRun = run; haveRun = true;
        if (decision.timelineChanged)
        { invalidate(); initialAdmission = false; }
        if (! block.clockValid || block.frames <= 0)
        {
            havePrevious = false;
            decision.verdict = Verdict::noClock;
            return fill (decision);
        }

        const bool continuous = havePrevious && block.projectValid
                             && block.project == previousProject + previousFrames;
        std::int64_t candidate = 0;
        // A project-time match is not unique inside a loop. Preserve an already proven K;
        // never recalibrate it onto a newer occurrence of the same project coordinate.
        if (continuous && block.playing && ! block.loop.active
            && joinCandidate (ring.header, block, runStart, candidate))
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
            decision.verdict = block.loop.active ? Verdict::loopUnproven : Verdict::calibrating;
            return fill (decision);
        }
        decision.preStart = block.clock - k;
        if (block.loop.active && ! loopVerified)
        {
            // No PRE copy or fade from an unproven boundary. A finite clamped interval may
            // recover on the same K; a broken timeline/run already invalidated it above.
            decision.verdict = Verdict::loopWaiting;
            return fill (decision);
        }
        decision.verdict = read (ring, decision.preStart, block.frames, out, outChannels);
        // PCM seq protects the samples/run, not the separate clock-only generation. A PRE
        // stop/seek/close during this copy must not leave a completed proof audible.
        if (ring.header.run.load (std::memory_order_acquire) != run
            || ring.header.ownerClosed.load (std::memory_order_acquire) != 0
            || ! preparedTimingCurrent (ring.header))
        {
            invalidate();
            initialAdmission = false;
            preparedGeneration = 0;
            decision.timelineChanged = true;
            decision.verdict = Verdict::torn;
        }
        return fill (decision);
    }

    void reset() noexcept
    {
        invalidate();
        havePrevious = false;
        haveRun = false;
        initialAdmission = true;
        preparedGeneration = 0;
        timeline.reset();
    }

    bool calibrated() const noexcept { return kValid; }
    std::int64_t offset() const noexcept { return k; }

    // Audio Thread, only while a new explicit session is still unproven. An unavailable first
    // snapshot may wait, but a completed or broken proof never reopens this admission. The evidence
    // must still reproduce from the current generation: either a linear origin, a certified content
    // clock, a positive presentation latency, or a bounded AAX engine clock. PCM availability and
    // the separate output permission are still checked normally; this cannot resume a trial.
    bool adoptInitialTiming (const Ring& ring, const BlockClock& block,
                             const TimingEvidence& evidence) noexcept
    {
        if (kValid) return true;
        if (! initialAdmission || ! evidence.valid || evidence.postClock != block.clock
            || ! initialPcmReady (ring)) return false;
        TimingSnapshot current;
        if (! readTiming (ring.header.timing, current) || ! current.active
            || current.generation != evidence.preGeneration
            || current.ownerA != evidence.ownerA || current.ownerB != evidence.ownerB
            || ring.header.ownerClosed.load (std::memory_order_acquire) != 0) return false;
        if (evidence.kind == LoopEntryKind::linear)
        {
            if (! current.anchor.linearKnown) return false;
        }
        else
        {
            const auto entry = initialLoopCandidate (current.anchor, current.block, block,
                evidence.loopSamples, ring.header.sampleRate.load (std::memory_order_relaxed),
                ringCapacityFrames);
            if (! entry.valid || entry.kind != evidence.kind || entry.k != evidence.k) return false;
        }
        k = evidence.k;
        kValid = true;
        initialAdmission = false;
        preparedGeneration = evidence.preGeneration;
        preparedOwnerA = evidence.ownerA;
        preparedOwnerB = evidence.ownerB;
        return true;
    }

private:
    static constexpr std::int64_t noCandidate = std::numeric_limits<std::int64_t>::min();

    bool preparedTimingCurrent (const RingHeader& h) const noexcept
    {
        if (preparedGeneration == 0) return true;
        const auto& timing = h.timing;
        return timing.generation.load (std::memory_order_acquire) == preparedGeneration
            && timing.ownerA.load (std::memory_order_acquire) == preparedOwnerA
            && timing.ownerB.load (std::memory_order_acquire) == preparedOwnerB;
    }

    static bool initialPcmReady (const Ring& ring) noexcept
    {
        const auto& h = ring.header;
        if (h.run.load (std::memory_order_acquire) == 0) return false;
        if (h.timing.sequence.load (std::memory_order_acquire) == 0) return true; // raw PCM fixtures
        TimingSnapshot current;
        if (! readTiming (h.timing, current) || ! current.active) return false;
        const auto seq = h.seq.load (std::memory_order_acquire);
        if ((seq & 1u) != 0) return false;
        const bool fresh = h.published.load (std::memory_order_relaxed) != 0
            && h.timingGeneration.load (std::memory_order_relaxed) == current.generation
            && h.writeEnd.load (std::memory_order_relaxed) == current.block.clock + current.block.frames;
        std::atomic_thread_fence (std::memory_order_acquire);
        return fresh && h.seq.load (std::memory_order_relaxed) == seq;
    }

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
        if (candidate != last) streak = 1;
        else if (streak < profile.streak) ++streak;
        last = candidate;
        if (streak >= profile.streak)
        {
            k = candidate;
            kValid = true;
            initialAdmission = false;
        }
    }

    // A linear PRE run has one project origin, checked at EVERY publish, so one anchor is
    // sufficient. No backwards descriptor scan, even for a long delay or a small host buffer.
    static bool joinCandidate (const RingHeader& h, const BlockClock& block, std::int64_t runStart,
                               std::int64_t& candidate) noexcept
    {
        const auto seq = h.seq.load (std::memory_order_acquire);
        if ((seq & 1u) != 0) return false;
        const auto flags = h.anchorFlags.load (std::memory_order_relaxed);
        const auto project = h.runProject.load (std::memory_order_relaxed);
        const auto end = h.writeEnd.load (std::memory_order_relaxed);
        const auto pre = runStart + (block.project - project);
        std::atomic_thread_fence (std::memory_order_acquire);
        if (h.seq.load (std::memory_order_relaxed) != seq || flags != 1u
            || pre < runStart || pre + block.frames > end || end - pre > ringCapacityFrames) return false;
        candidate = block.clock - pre;
        return true;
    }

    // Validate known K against the PRE run's constant-tempo loop anchor. Musical coordinates
    // corroborate the address, never choose it. Publisher advances the run on any unexplained
    // position change, missing data, tempo/range edit, clock reset or callback gap.
    static bool loopJoin (const RingHeader& h, const BlockClock& block, std::int64_t start,
                          double rate, BlockClock& verified) noexcept
    {
        if (! block.loop.usable (rate)) return false;
        const auto seq = h.seq.load (std::memory_order_acquire);
        if ((seq & 1u) != 0) return false;
        const auto flags = h.anchorFlags.load (std::memory_order_relaxed);
        const auto anchorClock = h.loopClock.load (std::memory_order_relaxed);
        const auto project = h.loopProject.load (std::memory_order_relaxed);
        const auto runStart = h.runStart.load (std::memory_order_relaxed);
        const auto runProject = h.runProject.load (std::memory_order_relaxed);
        const auto generation = h.timingGeneration.load (std::memory_order_relaxed);
        const LoopContext context { true, (flags & 2u) != 0,
            h.loopPpq.load (std::memory_order_relaxed), h.loopStart.load (std::memory_order_relaxed),
            h.loopEnd.load (std::memory_order_relaxed), h.loopBpm.load (std::memory_order_relaxed) };
        std::atomic_thread_fence (std::memory_order_acquire);
        if (h.seq.load (std::memory_order_relaxed) != seq) return false;
        LoopAnchor anchor { (flags & 1u) != 0, (flags & 2u) != 0,
            runStart, runProject, anchorClock, project, 0, context };
        if (corroborateLoop (anchor, block, start, rate, verified)) return true;
        // Only the exceptional boundary needs the separate clock snapshot. Normal blocks
        // keep the cheap PCM-anchor path; the snapshot must belong to this exact PCM run.
        TimingSnapshot timing;
        if (! readTiming (h.timing, timing) || ! timing.active || timing.generation != generation
            || ! timing.anchor.loopKnown || ! timing.anchor.loop.sameRange (context)) return false;
        // Clock observation may predate the first demanded PCM. Its earlier same-generation
        // anchor can corroborate a delayed tail without claiming those samples were written;
        // read() still rejects every frame before the independently checked PCM runStart.
        const bool matches = corroborateLoop (timing.anchor, block, start, rate, verified, &timing.block);
        return matches && h.seq.load (std::memory_order_acquire) == seq;
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
    LoopTimeline timeline;
    bool haveRun = false;
    bool initialAdmission = true;
    std::uint64_t preparedGeneration = 0;
    std::uint64_t preparedOwnerA = 0, preparedOwnerB = 0;
    std::uint64_t previousRun = 0;
};
}
