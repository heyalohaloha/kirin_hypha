#pragma once
#include "CandidateProductHost.h"

namespace hypha::tests
{
template<class Require> void verifyCandidateReturnEntry (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    for (const bool candidate : { false, true })
    {
        CandidateHost<Require> host (wall, require, 2, 512);
        host.pair(); auto& post = host.post();
        require (post.beginLiveBlind() == StartResult::started, "ordinary BLIND begins before queued return regression");
        host.render (1400, false, false);
        require (post.liveBlindStatus().stage == BlindStage::active, "ordinary BLIND owns a real capture scope");
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
        editor->setSize (900, 600); editor->setVisible (true); candidateTick (*editor);
        for (auto& clock : host.clocks) clock.playing = false;
        host.render (1, false, false);
        auto* end = dynamic_cast<juce::Button*> (candidateControl (*editor, "live-blind-end"));
        require (end && end->onClick, "real BLIND END button exists");
        end->onClick(); candidateTick (*editor);
        require (post.liveCompareStatus().finishing && post.liveCompareReturningScope() != 0,
                 "stopped ordinary BLIND retains only its ending scope");
        if (candidate)
        {
            post.editorBeingDeleted (editor.get()); editor.reset();
            enterCandidateThroughEditor (post, require, [&] (juce::AudioProcessorEditor& next)
            {
                expireCandidateEntryDeadline (next); candidateTick (next);
                require (candidateEntryPending (next), "ordinary BLIND END retains a confirmed A/B request while stopped");
                for (auto& clock : host.clocks) clock.playing = true;
                for (int i = 0; i < 200 && !post.candidateCompareActive(); ++i)
                { host.render (1, false, false); candidateTick (next); }
            });
        }
        else
        {
            pressComparisonMenuEntry (*editor, "BLIND", require);
            require (afterQueuedMessages ([] {}), "deliver the queued BLIND press");
            expireCandidateEntryDeadline (*editor); candidateTick (*editor);
            require (candidateEntryPending (*editor), "ordinary BLIND END retains the next BLIND press while stopped");
            for (auto& clock : host.clocks) clock.playing = true;
            host.serviceBlindAfterAudio = false; host.render (1, false, false); host.serviceBlindAfterAudio = true;
            require (!post.liveCompareStatus().finishing && post.liveCompareReturningScope() != 0,
                     "the same ending scope remains identifiable between RT completion and service cleanup");
            for (int i = 0; i < 200 && candidateEntryPending (*editor); ++i)
            { host.render (1, false, false); candidateTick (*editor); }
            require (!candidateEntryPending (*editor) && post.liveCompareStatus().active,
                     "one queued BLIND press starts automatically after the prior scope returns");
            post.editorBeingDeleted (editor.get()); editor.reset();
        }
    }
}

template<class Require> void verifyCandidateAlignment (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.delayA = 0; // identical clock K for both peers makes a stale correction observable
    host.pair(); auto& post = host.post();
    post.setLiveCompareAutoAlign (true);
    require (post.startLiveCompare() == StartResult::started, "ordinary comparison starts before peer switch");
    host.render (800, false, false);
    auto old = post.measureLiveCompareOffset(); old.clockK = host.delayB; old.lagFrames = -96;
    LiveTimingFixtureAccess::publishMeasuredAlign (post, old); host.render (2, false, false);
    require (post.liveCompareAlignFrames() == 96, "ordinary peer has an applied content correction");
    post.stopLiveCompare(); host.render (4, false, false);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "switch to actual candidate peer");
    LiveTimingFixtureAccess::publishMeasuredAlign (post, old); // a delayed publisher from PRE1
    post.selectLiveComparePre (true); host.render (100, true, true);
    require (post.liveCompareAlignFrames() == 0, "PRE1 correction and delayed publications cannot move PRE2's audio");
    host.render (900, true, true);
    auto previous = post.measureLiveCompareOffset(); previous.clockK = host.delayB; previous.lagFrames = -96;
    post.selectLiveComparePre (false); host.render (2, true, false, false);
    LiveTimingFixtureAccess::publishMeasuredAlign (post, previous); host.render (2, true, false, false);
    require (post.liveCompareAlignFrames() == 96, "candidate peer has its own applied correction");
    post.finishLiveCompare(); host.render (10, true, false, false); host.render (800, false, false);
    require (post.startLiveCompare() == StartResult::started, "return to the ordinary peer after candidate END");
    LiveTimingFixtureAccess::publishMeasuredAlign (post, previous); host.render (100, false, false);
    require (post.liveCompareAlignFrames() == 0, "candidate correction cannot return to ordinary PRE1 with the same K");
}
}
