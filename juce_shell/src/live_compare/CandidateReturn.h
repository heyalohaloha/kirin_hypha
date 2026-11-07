#pragma once
#include "LiveCompareRing.h"
#include "CandidateTransfer.h"
#include "LiveCompareSharedRing.h"
#include <limits>
#include <optional>

namespace hypha::live_compare
{
// Once PRE2 has withdrawn, observe the ordinary PRE1 -> POST pair without reading PCM.
// A fresh upstream marker must traverse the CURRENT whole chain before quarantine starts.
// This also works after a seek or loop wrap when PRE2 can no longer publish a clock.
struct CandidateReturnObserver
{
    SharedRingMapping source;
    TimingObserver timing;
    std::uint64_t generation = 0, reports = 0;
    std::int64_t marker = 0;
    bool demanded = false, seen = false, marked = false, proofAvailable = false;
    ~CandidateReturnObserver()
    {
        if (demanded) source.ring()->header.candidate.readers.fetch_sub (1, std::memory_order_acq_rel);
    }
    bool prepare (std::uint64_t key, std::uint32_t rate)
    {
        if (!source.open (key, rate, false)) return false;
        source.ring()->header.candidate.readers.fetch_add (1, std::memory_order_acq_rel);
        demanded = true;
        return true;
    }
    bool reached (const CandidateTransfer& transfer, std::uint64_t token,
                  const BlockClock& block, std::uint32_t rate) noexcept
    {
        proofAvailable = false;
        if (!transfer.withdrawn (token) || !source.ring()->matches (source.key(), rate))
        { seen = marked = false; timing.reset(); return false; }
        TimingSnapshot fresh;
        if (!readTiming (source.ring()->header.timing, fresh) || !fresh.active
            || fresh.block.wallNanos == 0 || block.wallNanos < fresh.block.wallNanos
            || block.wallNanos - fresh.block.wallNanos > CandidateTransfer::leaseNanos)
        { seen = marked = false; timing.reset(); return false; }
        const auto proof = timing.observe (fresh, block, rate, std::numeric_limits<std::int64_t>::max());
        proofAvailable = proof.valid;
        if (!seen || fresh.generation != generation || !proof.valid)
        {
            seen = true; marked = false; generation = fresh.generation; reports = fresh.reports;
            return false;
        }
        if (!marked)
        {
            if (fresh.reports == reports || !checkedClockAdd (fresh.block.clock, fresh.block.frames, marker)) return false;
            marked = true;
        }
        std::int64_t address = 0;
        return checkedClockSubtract (block.clock, proof.k, address) && address >= marker;
    }
};
// Retired positions remain useful through an uninterrupted POST projection only. A seek,
// missing clock or changed clock authority cannot turn an old return marker into new evidence.
struct CandidateReturnContinuity
{
    std::uint64_t token = 0;
    std::int64_t end = 0, origin = 0;
    BlockClock previous;
    bool known = false;
    bool currentProof (const Ring& ring, const BlockClock& block, const TimingEvidence& timing,
                       std::uint32_t rate) const noexcept
    {
        TimingSnapshot fresh;
        return ring.header.sampleRate.load (std::memory_order_acquire) == rate
            && ring.header.ownerClosed.load (std::memory_order_acquire) == 0
            && readTiming (ring.header.timing, fresh) && fresh.active
            && fresh.generation == timing.preGeneration && fresh.block.wallNanos != 0
            && fresh.block.wallNanos > previous.wallNanos && block.wallNanos >= fresh.block.wallNanos
            && block.wallNanos - fresh.block.wallNanos <= CandidateTransfer::leaseNanos;
    }
    bool observe (std::uint64_t request, const BlockClock& block, const TimingEvidence& timing, bool freshProof) noexcept
    {
        std::int64_t currentOrigin = 0;
        const bool linear = block.clockValid && block.projectValid && block.playing && !block.loop.active
            && checkedClockSubtract (block.clock, block.project, currentOrigin);
        if (request != token || !linear || block.clock != end || currentOrigin != origin
            || previous.clockBasis != block.clockBasis || previous.clockAuthority != block.clockAuthority) known = false;
        if (linear && timing.valid && freshProof) known = true;
        token = request; origin = currentOrigin; previous = block;
        if (!checkedClockAdd (block.clock, block.frames, end)) known = false;
        return known;
    }
};
// Only a host-supplied bound can replace a return-position witness. This evidence never
// permits PCM, MATCH, or listening. Absence cannot be replaced by the ring capacity.
inline std::optional<std::uint64_t> candidateReturnDrainFrames (const BlockClock& block) noexcept
{
    const bool bounded = reaperProcessing (block)
        || (block.clockAuthority == static_cast<std::uint8_t> (ClockAuthority::boundedAaxEngine)
            && block.maximumDelaySamples != 0);
    if (!bounded) return std::nullopt;
    const auto delay = static_cast<std::uint64_t> (block.maximumDelaySamples);
    const auto scheduling = static_cast<std::uint64_t> (ReaperTimingLimits::scheduling);
    if (delay > std::numeric_limits<std::uint64_t>::max() - scheduling) return std::nullopt;
    return delay + scheduling;
}
// Withdrawal is final. Once its marker has passed, a transient pair-proof gap cannot
// reintroduce substituted input. Count ordinary processing across short loops; restart
// only for a changed chain/clock, seek, stop or unavailable output. An absent/expired
// REAPER report pauses counting without reviving any old audio or MATCH authority.
struct CandidateReturnSettling
{
    LoopTimeline timeline;
    BlockClock previous;
    std::uint64_t token = 0, frames = 0, withdrawnFrames = 0;
    std::uint32_t rate = 0;
    bool drained = false, seen = false;
    std::uint64_t advance (std::uint64_t request, BlockClock block, std::uint32_t sampleRate,
                           bool reached, bool eligible, bool withdrawn = false) noexcept
    {
        const bool reaper = reaperProcessing (block);
        const bool current = !reaper || (!reaperTokenAbsent (block) && !block.runtimeExpired);
        if (reaperTokenAbsent (block)) retainReaperToken (block, previous);
        // The revision also changes during loop proof reacquisition. The complete chain
        // fingerprint and delay, rather than that observation counter, name the setup.
        block.runtimeRevision = previous.runtimeRevision;
        const bool changed = seen && !sameClockProof (block, previous);
        const auto movement = timeline.observe (block, sampleRate);
        const bool usable = eligible && block.playing && block.clockValid && block.projectValid && block.frames > 0;
        if (request != token || sampleRate != rate || changed || movement.broken || !usable)
        { drained = false; frames = withdrawnFrames = 0; }
        token = request; rate = sampleRate; previous = block; seen = true;
        // A coherent withdrawal also drains without either old PRE: count only continuous,
        // compensated callbacks past the bound on every insert's delay. This is return-only
        // evidence, never K, PCM permission or a MATCH window.
        if (!withdrawn) withdrawnFrames = 0;
        const auto bound = candidateReturnDrainFrames (block);
        const auto count = static_cast<std::uint64_t> (usable ? block.frames : 0);
        if (usable && current && withdrawn && bound)
        {
            if (withdrawnFrames >= *bound) reached = true;
            else if (withdrawnFrames <= std::numeric_limits<std::uint64_t>::max() - count) withdrawnFrames += count;
            else { drained = false; frames = withdrawnFrames = 0; return 0; }
        }
        if (usable && current && reached) drained = true;
        if (usable && current && drained)
        {
            if (frames > std::numeric_limits<std::uint64_t>::max() - count)
            { drained = false; frames = withdrawnFrames = 0; return 0; }
            frames += count;
        }
        return frames;
    }
};
// Drain-only positions never authorize PCM, MATCH or a listening trial. The ordinary
// settling allowance starts when B carries PRE2's last possibly substituted sample.
inline bool candidateReturnReached (const Ring& ring, std::uint64_t token,
    const BlockClock& block, const TimingEvidence& timing, std::uint32_t currentRate,
    bool continuous, bool freshProof) noexcept
{
    const auto& transfer = ring.header.candidate;
    std::int64_t boundary = 0, origin = 0;
    std::uint64_t generation = 0;
    if (token == 0 || !block.clockValid || !block.projectValid || !block.playing
        || !transfer.readReturn (token, boundary, origin, generation)) return false;
    std::int64_t address = 0;
    const auto sourceRate = ring.header.sampleRate.load (std::memory_order_acquire);
    if (currentRate == sourceRate && timing.valid && (continuous || freshProof)
        && checkedClockSubtract (block.clock, timing.k, address))
    {
        if (timing.preGeneration == generation) return address >= boundary;
        // A fresh upstream run after withdrawal also contains only ordinary input.
        TimingSnapshot fresh;
        if (timing.preGeneration > generation && readTiming (ring.header.timing, fresh)
            && fresh.generation == timing.preGeneration) return address >= fresh.anchor.runStart;
    }
    if (block.loop.active || !continuous) return false;
    // A stopped PRE2 no longer publishes timing. Its withdrawn-reader receipt still names
    // the last output's compensated project position. Use POST's CURRENT position, including
    // a changed B delay, to drain it. This does not revive the retired clock proof or any PCM.
    std::int64_t projectBoundary = 0;
    return currentRate != 0 && sourceRate != 0 && checkedClockSubtract (boundary, origin, projectBoundary)
        && static_cast<long double> (block.project) * sourceRate
            >= static_cast<long double> (projectBoundary) * currentRate;
}
}
