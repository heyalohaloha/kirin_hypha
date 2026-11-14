#pragma once
#include "CandidateProductHost.h"
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLiveCompareRecoveryText.h"

namespace hypha::tests
{
// Each A/B start owns a fresh alignment: nothing published for an earlier start ever applies, even for
// the same PRE2 after the host's positions moved. Only a correction content confirmed for that same
// candidate carries over, under the same host conditions (verifyCandidateKeptCorrection below).
template<class Require> void verifyCandidateAlignmentEpoch (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.delayA = 0; host.delayB = 0; host.pair();
    auto& post = host.post();
    post.setLiveCompareAutoAlign (true);
    for (const int reported : { 1545, 0 })
    {
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "candidate starts with the same PRE2");
        host.render (950, true, false, false);
        auto old = post.measureLiveCompareOffset(); old.lagFrames = -1545;
        LiveTimingFixtureAccess::publishMeasuredAlign (post, old);
        host.render (3, true, false, false);
        require (post.liveCompareAlignFrames() == 1545, "the previous session applies its correction");
        post.finishLiveCompare(); host.render (10, true, false, false); host.render (800, false, false);
        host.reportedDelayB = reported;
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "candidate restarts with the same PRE2 after reported compensation changes");
        require (post.liveCompareAlignFrames() == 0, "the prior applied display is absent before the first callback");
        LiveTimingFixtureAccess::publishMeasuredAlign (post, old);
        post.selectLiveComparePre (true); host.render (40, true, true, false);
        require (post.liveCompareAlignFrames() == 0, "a delayed result from the prior session cannot be applied");
        post.finishLiveCompare(); host.render (10, true, false, false); host.render (800, false, false);
    }
}

// INV-LC28 / INV-AB: unit B is an outboard whose delay the host positions show 1065 frames short, so A
// sounds early. A correction confirmed for this candidate is kept with the host conditions it was
// confirmed under. The same PRE2 restarted on material that confirms nothing plays A at K plus it from
// its first audible block (that block fades in from B), told as not re-measured. Units rearranged while stopped (PRE2's reported
// output latency changes) start fresh, and another candidate (a new PRE2) never gets the value.
template<class Require> void verifyCandidateKeptCorrection (Require require)
{
    using namespace live_compare;
    constexpr int residual = 1065;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.processorTimers = true; host.timerBlocks = 4;
    host.delayB = 383 + residual; host.reportedDelayB = -residual;
    candidateBroadband = true;
    host.pair(); auto& post = host.post();
    post.setLiveCompareAutoAlign (true);
    const auto start = [&] (const char* why)
    {
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, why);
        post.selectLiveComparePre (true);
    };
    const auto finish = [&] { post.finishLiveCompare(); host.render (10, true, false, false); host.render (800, false, false); };
    struct Row { int audible = 0, aligned = 0, atK = 0, firstAudible = -1, notRemeasured = 0, unconfirmed = 0; };
    const auto heard = [&] (const char* what, int blocks)
    {
        Row row;
        for (int n = 0; n < blocks; ++n)
        {
            host.render (1, true, true, false);
            const auto s = post.liveCompareStatus();
            row.notRemeasured += s.contentNotRemeasured ? 1 : 0;
            row.unconfirmed += post.candidateAlignUnconfirmed() ? 1 : 0;
            if (! s.preAudible) continue;
            if (row.firstAudible < 0) row.firstAudible = n;
            ++row.audible;
            bool aligned = true, atK = true; // A at its content position, or 1065 frames early at K
            for (int ch = 0; ch < host.channels; ++ch)
                for (int i = 0; i < host.block; ++i)
                {
                    const auto at = host.cursor - host.block + i - host.delayA - host.delayB;
                    const auto value = host.audio.getSample (ch, i);
                    aligned = aligned && std::abs (value - candidateA (at, ch) * s.gain) < 3.0e-6f;
                    atK = atK && std::abs (value - candidateA (at + residual, ch) * s.gain) < 3.0e-6f;
                }
            row.aligned += aligned ? 1 : 0;
            row.atK += atK ? 1 : 0;
        }
        std::printf ("A/B outboard %-34s audible %3d first %3d PRE early 0: %3d, at K (1065 early): %3d, not re-measured %3d, unconfirmed %3d, align %lld\n",
                     what, row.audible, row.firstAudible, row.aligned, row.atK, row.notRemeasured, row.unconfirmed,
                     static_cast<long long> (post.liveCompareAlignFrames()));
        return row;
    };
    start ("A/B starts on broadband material");
    host.render (1, true, true, false);
    for (int i = 0; i < 1200 && ! (post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == residual); ++i)
        host.render (1, true, true, false);
    require (post.liveCompareAlignFrames() == residual, "broadband material confirms A 1065 frames early");
    const auto confirmed = heard ("confirmed", 60);
    require (confirmed.audible == 60 && confirmed.aligned == 60, "A sounds at its content position");
    finish();
    candidateBroadband = false; // repeating sines: nothing confirms a position from here on
    start ("the same candidate restarts");
    const auto kept = heard ("same candidate restarted", 300);
    require (kept.audible > 150 && kept.atK == 0 && kept.aligned >= kept.audible - 1 && kept.notRemeasured > 0
             && post.liveCompareAlignFrames() == residual,
             "the same candidate plays A at K plus the kept correction from its first audible block, not re-measured");
    finish();
    for (auto& clock : host.clocks) clock.playing = false;
    host.render (20, false, false, false);
    host.clocks[1].outputPresentation += 512; // the units after PRE2 were rearranged
    for (auto& clock : host.clocks) clock.playing = true;
    host.render (200, false, false, false);
    start ("the candidate restarts after the units moved");
    const auto moved = heard ("units rearranged while stopped", 300);
    require (moved.audible > 150 && moved.aligned == 0 && moved.atK >= moved.audible - 1 && moved.notRemeasured == 0
             && post.liveCompareAlignFrames() == 0, "changed host conditions start fresh: A at K");
    finish();
    // The rearranged candidate confirms its own correction again, so one exists when another candidate starts.
    candidateBroadband = true;
    start ("the candidate starts on broadband material after the units moved");
    for (int i = 0; i < 1200 && ! (post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == residual); ++i)
        host.render (1, true, true, false);
    require (post.liveCompareAlignFrames() == residual, "the rearranged candidate confirms A 1065 frames early again");
    finish();
    candidateBroadband = false;
    // Another candidate: a new PRE2 instance at the same place.
    host.instances[1]->releaseResources();
    host.instances[1].reset();
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    host.instances[1] = std::make_unique<KirinHyphaProcessorBase> (KirinHyphaProcessorBase::Role::Pre);
    auto layout = host.pre2().getBusesLayout();
    layout.inputBuses.set (0, juce::AudioChannelSet::stereo()); layout.outputBuses.set (0, layout.inputBuses[0]);
    require (host.pre2().setBusesLayout (layout), "the new PRE2 negotiates stereo");
    host.pre2().setPlayHead (&host.clocks[1]);
    prepareFixtureProcessor (host.pre2(), 48000, host.block);
    LiveTimingFixtureAccess::enableFixtureWrites (host.pre2());
    host.render (60, false, false, false);
    awaitAnalysisInput (host.pre2()); awaitAnalysisExchange (host.pre2(), true);
    start ("another candidate starts");
    const auto other = heard ("another candidate", 300);
    require (other.audible > 150 && other.aligned == 0 && other.atK >= other.audible - 1 && other.notRemeasured == 0
             && post.liveCompareAlignFrames() == 0,
             "another candidate never gets the kept correction");
    finish();
}

// INV-LC28: an ended hold is told in A/B and its BLIND with the words LISTEN and BLIND use, the same
// for both sources (the renderer's hold and the status rule have their own tests).
template<class Require> void verifyCandidateHoldNotice (Require require)
{
    using namespace live_compare;
    const auto label = juce::String (live_compare_ui::unmeasuredNotice()) + " / " + live_compare_ui::chainDelayNotice();
    for (const bool candidate : { false, true })
        for (const int source : { 1, 2 })
        {
            LiveBlindStatus status;
            status.stage = BlindStage::active; status.candidate = candidate; status.trial.selected = source;
            const auto told = alignNotice (true, false, true);
            status.contentUnmeasured = told.unmeasured; status.contentNotRemeasured = told.notRemeasured;
            require (live_blind_ui::screenFor (status, true, 1.0f).detail == label,
                     "an ended hold shows the same unmeasured label for both sources, in A/B as in BLIND");
        }
}

// Real units differ, so the two sides share a position, not a waveform. The processor's own
// content service confirms A through its normal timer, and BLIND then counts both sources.
template<class Require> void verifyCandidateContentConfirmation (Require require)
{
    using namespace live_compare;
    candidateBroadband = true;
    for (const int late : { 0, 512 }) // 512: a delay nobody compensated leaves A after B
    {
        ProductTestClock wall;
        CandidateHost<Require> host (wall, require, 2, 512);
        host.processorTimers = true; host.timerBlocks = 4;
        host.reportedDelayB = late;
        host.pair(); auto& post = host.post();
        post.setLiveCompareAutoAlign (true);
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "content fixture starts A/B through the real API");
        post.selectLiveComparePre (true);
        bool waited = false;
        for (int i = 0; i < 1200 && (!post.liveCompareStatus().preAudible || post.liveCompareAlignPending()); ++i)
        {
            host.render (1, true, true, false);
            waited = waited || post.liveCompareAlignPending();
        }
        require (waited && post.liveCompareStatus().preAudible && !post.liveCompareAlignPending()
            && post.candidateCompareActive() && post.liveCompareAlignFrames() == 0,
            "A that differs from B is confirmed at its compensated position and then sounds");
        require (post.liveContentTimingObservation().lagFrames == late && !post.candidateAlignUnconfirmed()
            && post.liveCompareStatus().observation != RecoveryReason::preLate,
            "a late A sounds as it is, with the measured amount in place of a wait");
        for (int n = 0; n < 40; ++n)
        {
            host.render (1, true, true, false);
            require (post.liveCompareStatus().preAudible, "confirmed A keeps sounding");
            for (int ch = 0; ch < host.channels; ++ch) for (int i = 0; i < host.block; ++i)
                require (std::abs (host.audio.getSample (ch, i) - candidateA (host.cursor - host.block + i
                    - host.delayA - host.delayB - late, ch)) < 3.0e-6f, "A sounds at its clock position, never B");
        }
        post.selectLiveComparePre (false); host.render (900, true, false, false);
        const auto measured = post.measureLiveCompare();
        require (measured.ok() && post.applyLiveCompareMatch (planMatch (measured, 0.0), MatchChoice::basis),
                 "content fixture matches through the real MATCH");
        host.render (100, true, false, false);
        require (post.beginLiveBlind() == StartResult::started, "content fixture enters BLIND");
        host.render (600, true, false, false);
        require (post.liveBlindStatus().stage == BlindStage::active, "the confirmed comparison reaches an anonymous trial");
        for (const int source : { 1, 2 })
        { require (post.selectLiveBlind (source), "either source can be selected"); host.render (40, true, false, false); }
        require (post.liveBlindStatus().trial.played == 3 && post.revealLiveBlind(),
                 "both sources are counted as heard once content is confirmed");
    }
    candidateBroadband = false;
    // Repeating sines confirm no position. The wait for them ends, and B does not come back.
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.processorTimers = true;
    host.pair(); auto& post = host.post();
    post.setLiveCompareAutoAlign (true);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
             "unconfirmed fixture starts A/B through the real API");
    post.selectLiveComparePre (true);
    int blocks = 0;
    while (blocks < 400 && !post.liveCompareStatus().preAudible) { host.render (1, true, true, false); ++blocks; }
    require (blocks < 110 && post.liveCompareStatus().preAudible && post.liveCompareAlignFrames() == 0,
             "the wait for a position nothing can confirm is short, then A sounds");
    for (int n = 0; n < 500; ++n)
    {
        host.render (1, true, true, false);
        require (post.liveCompareStatus().preAudible, "unconfirmed A keeps sounding; B does not return");
        for (int ch = 0; ch < host.channels; ++ch) for (int i = 0; i < host.block; ++i)
            require (std::abs (host.audio.getSample (ch, i) - candidateA (host.cursor - host.block + i
                - host.delayA - host.delayB, ch)) < 3.0e-6f, "unconfirmed A is A at its clock position");
    }
    require (post.candidateAlignUnconfirmed(), "the footer says what the listener hears");
    post.finishLiveCompare(); host.render (10, true, false, false);
    require (!post.candidateAlignUnconfirmed(), "the notice ends with the comparison");
}
}
