#pragma once
#include "LiveCompareSharedRing.h"
#include "LiveCompareSession.h"
#include "LiveCompareReentry.h"
#include "LiveCompareCompletion.h"

namespace hypha::live_compare
{
// PRE2's separately owned reader. Its own ring is published BEFORE this receives x.
// Reuses LIVE's correspondence and symmetric fade; unity is never subject to MATCH or AUTO.
struct CandidateReceiver
{
    SharedRingMapping source;
    TimingObserver timing;
    PostRenderer renderer;
    PostLevel unity;
    Completion completion;
    std::uint64_t token = 0, permission = 0, run = 0, leaseUntil = 0;
    std::int64_t verifiedStart = 0, verifiedEnd = 0;
    std::uint64_t lastGeneration = 0, compensationSeen = 0;
    BlockClock lastOutput; // read off RT only after publication withdrawal and reader exit
    bool initial = true, demandOwned = false;
    bool requestReturn() noexcept
    {
        if (completion.command() == 0) completion.request();
        return ! completion.pending();
    }
    ~CandidateReceiver()
    {
        if (demandOwned && source.ring() != nullptr)
            source.ring()->header.candidate.readers.fetch_sub (1, std::memory_order_acq_rel);
    }

    bool prepare (std::uint64_t key, std::uint64_t request, std::uint64_t authority,
                  std::uint32_t rate, int frames, std::uint64_t ownerA, std::uint64_t ownerB)
    {
        if (! source.open (key, rate, false)) return false;
        if (source.ring()->header.timing.ownerA.load() != ownerA
            || source.ring()->header.timing.ownerB.load() != ownerB) return false;
        if ((source.ring()->header.source.load() & ringSourceMultiMono) != 0) return false;
        source.ring()->header.candidate.readers.fetch_add (1, std::memory_order_acq_rel);
        demandOwned = true; token = request; permission = authority;
        renderer.prepare (frames, rate); unity.configure (rate);
        return true;
    }

    void process (Ring& published, BlockClock block, float* const* io, int channels,
                  bool usable, bool permitted,
                  bool compensationOff = false, std::uint64_t compensationRevision = 0,
                  bool offline = false) noexcept
    {
        lastOutput = block;
        lastGeneration = published.header.timing.generation.load (std::memory_order_acquire);
        auto& receipt = published.header.candidate;
        auto* input = source.ring();
        const auto finishing = completion.command();
        std::uint64_t observedUntil = 0;
        const auto lease = receipt.readLease (token, block.wallNanos, observedUntil);
        if (lease != CandidateTransfer::Lease::updating) leaseUntil = observedUntil;
        const bool leased = lease == CandidateTransfer::Lease::valid
            || (lease == CandidateTransfer::Lease::updating && leaseUntil != 0
                && block.wallNanos <= leaseUntil && receipt.owner.load() == token);
        const bool requested = finishing == 0 && permitted && receipt.released.load (std::memory_order_acquire) != token
            && leased;
        if (compensationOff || compensationRevision != compensationSeen)
        {
            renderer.revokeTiming(); timing.reset(); initial = true;
            verifiedStart = verifiedEnd = 0; block.afterGap = true;
        }
        compensationSeen = compensationRevision;
        // With compensation off, no project/engine correspondence can authorize x.
        // Keep the request and render only the transition to the actual input.
        block.clockValid = block.clockValid && !compensationOff;
        const bool allowed = requested && usable && !compensationOff && block.playing
            && input != nullptr && input->matches (source.key(), source.rate());
        // Hardware and plug-ins on either side use the same current clock/ring proof.
        if (! usable || ! permitted || input == nullptr)
        {
            // The previous block may still end on x. Leave it over the same short fade; an
            // offline pass starts here, so it stays exact from its first sample.
            bool fading = false;
            if (offline) renderer.bypassTransition();
            else fading = renderer.fadeToInput (io, channels, block.frames);
            renderer.revokeTiming(); timing.reset(); initial = true;
            verifiedStart = verifiedEnd = 0;
            receipt.publish (token, 0, 0, 0, false);
            // The command can retire this receiver once the fade is over, even when the host
            // keeps calling it bypassed.
            completion.observe (finishing, true, fading, 1.0f);
            return;
        }
        TimingSnapshot snapshot;
        const auto evidence = readTiming (input->header.timing, snapshot)
            ? timing.observe (snapshot, block, source.rate(), ringCapacityFrames)
            : timing.unavailable (block, source.rate(), ringCapacityFrames);
        if (renderer.needsNewAdmission()) { renderer.renewNamedTiming(); initial = true; }
        if (initial && renderer.adoptInitialTiming (*input, block, evidence)) initial = false;
        const auto report = renderer.render (*input, source.key(), source.rate(), block, io, channels,
            allowed, 1.0f, unity, 1.0f, std::numeric_limits<float>::max(), false, RecoveryReason::none, nullptr, {}, 0, true);
        const auto currentRun = published.header.run.load (std::memory_order_acquire);
        const bool exact = allowed && report.verdict == Verdict::accepted && report.stableSource
            && report.audibleSource == RenderReport::AudibleSource::pre;
        if (! exact || currentRun != run || block.clock != verifiedEnd)
            verifiedStart = block.clock;
        verifiedEnd = exact ? block.clock + block.frames : block.clock;
        run = currentRun;
        // The first forward fade always ends with positive PRE weight. Later return fades
        // cannot erase this sticky request fact even if they end at exact pass-through.
        if (report.preAudible) receipt.noteOutputChange (token);
        receipt.publish (token, run, verifiedStart, verifiedEnd, report.preAudible);
        completion.observe (finishing, usable, report.preAudible, 1.0f);
    }
};
}
