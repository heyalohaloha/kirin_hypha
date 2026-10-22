#pragma once
#include "DoublePrecisionParityTest.h"

namespace hypha::tests::precision
{
inline const char* listenReasonName (live_compare::RecoveryReason reason)
{
    using R = live_compare::RecoveryReason;
   #define KIRIN_LISTEN_REASON(name) case R::name: return #name
    switch (reason)
    {
        KIRIN_LISTEN_REASON(none); KIRIN_LISTEN_REASON(stopped); KIRIN_LISTEN_REASON(positionChanged);
        KIRIN_LISTEN_REASON(callbackGap); KIRIN_LISTEN_REASON(projectClockMissing); KIRIN_LISTEN_REASON(clockMissing);
        KIRIN_LISTEN_REASON(calibrating); KIRIN_LISTEN_REASON(writing); KIRIN_LISTEN_REASON(beforeRun);
        KIRIN_LISTEN_REASON(notWritten); KIRIN_LISTEN_REASON(overwritten); KIRIN_LISTEN_REASON(torn);
        KIRIN_LISTEN_REASON(foreignRing); KIRIN_LISTEN_REASON(pairChanged); KIRIN_LISTEN_REASON(preUnavailable);
        KIRIN_LISTEN_REASON(formatChanged); KIRIN_LISTEN_REASON(restored); KIRIN_LISTEN_REASON(compensationOff);
        KIRIN_LISTEN_REASON(contentChanged); KIRIN_LISTEN_REASON(bypassed); KIRIN_LISTEN_REASON(offline);
        KIRIN_LISTEN_REASON(outputTaken); KIRIN_LISTEN_REASON(gainChanged); KIRIN_LISTEN_REASON(nonFinite);
        KIRIN_LISTEN_REASON(ceiling); KIRIN_LISTEN_REASON(blockTooLarge); KIRIN_LISTEN_REASON(randomUnavailable);
        KIRIN_LISTEN_REASON(unknown); KIRIN_LISTEN_REASON(loopUnproven); KIRIN_LISTEN_REASON(loopWaiting);
        KIRIN_LISTEN_REASON(loopTooShort); KIRIN_LISTEN_REASON(loopClockUnavailable); KIRIN_LISTEN_REASON(cycleUnverified);
        KIRIN_LISTEN_REASON(reaperFirstCycle); KIRIN_LISTEN_REASON(reaperDelayUnverified); KIRIN_LISTEN_REASON(reaperLoopTooShort);
    }
   #undef KIRIN_LISTEN_REASON
    return "invalid";
}

struct DoublePrecisionListenDiagnostics
{
    int unchanged = 0, changed = 0, fullyPre = 0;
    std::uint64_t callbacks = 0;
    std::int64_t position = 0;
    double even[2] {}, odd[2] {};

    void print (const Processor& pre, const Processor& post, int stable, bool mixed) const
    {
        const auto state = post.liveCompareStatus();
        std::cerr << "double LISTEN diagnostic: callbacks=" << callbacks << " position=" << position
                  << " unchanged=" << unchanged << " changed=" << changed << " fullyPre=" << fullyPre
                  << " stableBlocks=" << stable << " mixedFade=" << mixed << '\n';
        std::cerr << "  layout PRE=" << pre.getTotalNumInputChannels() << '/' << pre.getTotalNumOutputChannels()
                  << " POST=" << post.getTotalNumInputChannels() << '/' << post.getTotalNumOutputChannels()
                  << " double=" << pre.isUsingDoublePrecision() << '/' << post.isUsingDoublePrecision() << '\n';
        std::cerr << "  samples even=" << std::hexfloat << even[0] << '/' << even[1]
                  << " odd=" << odd[0] << '/' << odd[1] << std::defaultfloat << '\n';
        std::cerr << "  active=" << state.active << " selected=" << state.preSelected
                  << " verified=" << (state.active && state.verdict == live_compare::Verdict::accepted)
                  << " audible=" << state.preAudible << " waiting=" << state.preWaiting
                  << " verdict=" << static_cast<int> (state.verdict)
                  << " reason=" << listenReasonName (state.reason)
                  << " observation=" << listenReasonName (state.observation)
                  << " reentry=" << state.timingReentryPending << " generation=" << state.sessionGeneration
                  << " gain=" << state.gain << " postTarget=" << state.postTarget << " postActual=" << state.postActual << '\n';
        const auto* mapping = LiveTimingFixtureAccess::publishedRing (pre);
        const auto* ring = mapping != nullptr ? mapping->ring() : nullptr;
        if (ring == nullptr) { std::cerr << "  PRE ring unavailable\n"; return; }
        const auto& h = ring->header;
        std::cerr << "  PRE publication: run=" << h.run.load() << " published=" << h.published.load()
                  << " seq=" << h.seq.load() << " start=" << h.runStart.load() << " end=" << h.writeEnd.load()
                  << " channels=" << h.channels.load() << " demand=" << h.demand.load()
                  << " closed=" << h.ownerClosed.load() << " timingGeneration=" << h.timingGeneration.load() << '\n';
        live_compare::TimingSnapshot timing;
        if (live_compare::readTiming (h.timing, timing))
            std::cerr << "  PRE timing: seq=" << timing.sequence << " generation=" << timing.generation
                      << " owner=" << timing.ownerA << '/' << timing.ownerB << " active=" << timing.active
                      << " clock=" << timing.block.clock << " project=" << timing.block.project
                      << " frames=" << timing.block.frames << " origin=" << timing.origin
                      << " basis=" << static_cast<int> (timing.block.clockBasis)
                      << " authority=" << static_cast<int> (timing.block.clockAuthority)
                      << " cause=" << static_cast<int> (timing.cause) << '\n';
        else std::cerr << "  PRE timing snapshot unreadable\n";
    }
};
}
