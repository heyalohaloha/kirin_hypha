#pragma once
#include "CandidateProductHost.h"
#include "CandidateMenuHost.h"
#include "CandidateReturnEntryTest.h"
#include "CandidateAlignmentEpochTest.h"
#include "LiveCompareRestartAlignmentTest.h"
#include "CandidatePostOnlyReturnTest.h"
#include "CandidateActivationGuidanceTest.h"
#include "CandidateHelpTextTest.h"
#include "CandidateLoopBoundaryTest.h"
#include "CandidateLongReturnTest.h"
#include "CandidateRecoveryTest.h"
#include "CandidateCompensationTest.h"
#include "CandidateLifetimeTest.h"
#include "CandidateBlindScopeRaceTest.h"
#include "CandidateAdmissionTest.h"
#include "CandidateRecoveryPriorityTest.h"

namespace hypha::tests
{
template<class Require> void verifyCandidateAudit (Require require)
{
    using namespace live_compare;
    verifyCandidateBlindScopeRace (require);
    verifyCandidatePreAdmission (require);
    verifyCandidateRecoveryPriority (require);
    std::cout << "Candidate product: BLIND admission boundary\n" << std::flush;
    verifyCandidateBlindStart (require);
    std::cout << "Candidate product: hardware placement\n" << std::flush;
    verifyCandidateHardwarePlacement (require);
    std::cout << "Candidate product: lifetime and shared exclusion\n" << std::flush;
    verifyCandidateLifetime (require);
    std::cout << "Candidate product: compensation and BLIND guidance\n" << std::flush;
    verifyCandidateCompensation (require);
    std::cout << "Candidate product: host return\n" << std::flush;
    verifyCandidateRecoveryAudit (require);
    verifyCandidateLongReturn (require);
    verifyCandidatePostOnlyReturn (require);
    verifyCandidateActivationGuidance (require);
    verifyCandidateHelpText (require);
    verifyCandidateLoopBoundary (require);
    std::cout << "Candidate product: queued return\n" << std::flush;
    verifyCandidateReturnEntry (require);
    std::cout << "Candidate product: alignment\n" << std::flush;
    verifyCandidateAlignment (require);
    verifyCandidateAlignmentEpoch (require);
    verifyCandidateContentConfirmation (require);
    verifyLiveCompareRestartAlignment (require);
    ProductTestClock wall;
    {
        CandidateHost<Require> host (wall, require, 2, 512);
        host.pair(); verifyCandidateMenuHost (host, require);
    }
    std::cout << "Candidate product: continuity\n" << std::flush;
    for (const int frames : { 256, 512, 1024 })
    {
        CandidateHost<Require> host (wall, require, 2, frames);
        host.pair();
        require (host.post().startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "continuous candidate fixture enters through the real processor");
        host.post().selectLiveComparePre (true);
        host.sourceContinuity.begin(); host.outputContinuity.begin();
        host.render (2400000 / frames, true, true); // fifty seconds without one missing block
        for (auto& clock : host.clocks) clock.offset -= 4096;
        host.sourceContinuity.begin(); host.outputContinuity.begin();
        host.render (200, true, true);
        host.post().selectLiveComparePre (false); host.outputContinuity.begin();
        host.render (200, true, false);
        require (host.post().beginLiveBlind() == StartResult::started, "continuous fixture enters BLIND");
        host.outputContinuity.enabled = false;
        host.render (480000 / frames, true, false, false);
        require (host.post().liveBlindStatus().stage == BlindStage::active, "continuous fixture reaches anonymous playback");
        host.outputContinuity.begin(); host.render (2400000 / frames, true, false);
    }
    // B's known delay exceeds the old fixed quarantine. No comparison sample may
    // enter normal Watch/Record merely because four wall-clock seconds elapsed.
    {
        CandidateHost<Require> host (wall, require, 2, 512);
        host.delayB = 240000;
        host.pair();
        require (host.post().startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "long-delay A/B starts through the processor entry");
        host.render (1800, true, false, false);
        host.render (40, true, false);
        host.post().finishLiveCompare();
        host.render (410, true, false, false);
        require (host.post().candidateMeasurementIsolated() && !host.post().keepPair(),
                 "known delayed comparison input stays outside normal measurement and Record after END");
        host.render (520, false, false);
        require (!host.post().candidateMeasurementIsolated(), "normal measurement resumes after B carries the return position");
    }
    // No PRE2 callback at all: both a host non-active notification and silent callback
    // withdrawal must complete END without asking the listener to re-enable the insert.
    for (const bool notified : { false, true })
    {
        CandidateHost<Require> host (wall, require, 1, 256);
        host.pair();
        require (host.post().startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "callback-stall A/B starts");
        host.render (140, true, false);
        host.pre2Enabled = false;
        if (notified) host.pre2().hostComponentActivationChanged (false);
        host.post().finishLiveCompare();
        host.render (1000, false, false);
        require (!host.post().candidateMeasurementIsolated(), "END retires a stopped PRE2 reader and releases capture exclusion");
        host.pre2().hostComponentActivationChanged (true);
        host.pre2Enabled = true;
        host.render (4, false, false);
        require (host.post().startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "a new comparison starts after stopped-receiver END");
    }
    {
        CandidateHost<Require> host (wall, require, 2, 512);
        host.pair();
        require (host.post().startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "first POST owns candidate comparison");
        host.render (160, true, false);
        auto second = std::make_unique<KirinHyphaProcessorBase> (KirinHyphaProcessorBase::Role::Post);
        second->setPlayHead (&host.clocks[2]);
        prepareFixtureProcessor (*second, 48000, host.block);
        LiveTimingFixtureAccess::enableFixtureWrites (*second);
        require (second->setPairCandidate (host.pre1().instanceId(), "Source"), "second POST keeps the same ordinary source");
        require (second->startCandidateCompare (host.pre2().instanceId(), true) == StartResult::comparisonBusy,
                 "second POST cannot take the active candidate");
        second->serviceLiveCompare(); second.reset();
        const auto before = host.replacementBlocks;
        host.render (100, true, false);
        require (host.replacementBlocks == before + 100 && host.post().liveCompareStatus().verdict == Verdict::accepted,
                 "rejected POST teardown preserves the first POST's PCM demand and every replacement block");
    }
}
}
