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
                // The wait for the return is bounded by the return itself; it never moves the press's deadline.
                const auto deadline = juce::Time::getMillisecondCounterHiRes() * 0.001 + 5.0;
                candidateEntryDeadline (next) = deadline; candidateTick (next);
                require (candidateEntryDeadline (next) == deadline, "waiting for the return keeps the press's deadline");
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

// POST is lowered, then the host bypasses POST and goes on calling it: the RETURN a LISTEN press asks for
// cannot finish (the audio side waits for a block it may change, and the callbacks never go quiet). The
// press's 10 s still end its wait, saying that bypass holds the return; turning bypass off returns POST.
template<class Require> void verifyCandidateBypassedReturnEntry (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    require (post.startLiveCompare() == StartResult::started, "LISTEN starts before its MATCH lowers POST");
    host.render (100, false, false);
    MatchResult level; level.failure = MatchFailure::none; level.measuredDb = 8.0;
    level.prePeakDbtp = -1.0; level.postPeakDbtp = -4.0; level.ceilingDbtp = -1.0;
    require (post.applyLiveCompareMatch (planMatch (level, 0.0), MatchChoice::lowerPost), "the approved MATCH lowers POST");
    host.render (30, false, false); post.stopLiveCompare(); host.render (10, false, false);
    require (post.liveCompareStatus().postTarget < 1.0f, "the lowered level is held after LISTEN");
    post.getBypassParameter()->setValue (1.0f);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
    editor->setSize (900, 600); editor->setVisible (true); candidateTick (*editor);
    pressComparisonMenuEntry (*editor, "LISTEN", require);
    require (afterQueuedMessages ([] {}), "deliver the queued LISTEN press");
    for (int i = 0; i < 200; ++i) { host.render (1, false, false); candidateTick (*editor); }
    require (candidateEntryPending (*editor) && post.liveCompareStatus().finishing,
             "bypass holds the RETURN the press asked for while its callbacks go on");
    expireCandidateEntryDeadline (*editor); candidateTick (*editor);
    require (! candidateEntryPending (*editor) && candidateToast (*editor) == live_compare_ui::bypassHoldsReturnNotice,
             "the press's 10 s end the wait and say that bypass holds the return");
    post.getBypassParameter()->setValue (0.0f);
    for (int i = 0; i < 100 && post.liveCompareStatus().finishing; ++i) { host.render (1, false, false); candidateTick (*editor); }
    require (! post.liveCompareStatus().finishing && post.liveCompareStatus().postActual == 1.0f
                 && ! post.liveCompareStatus().active,
             "with bypass off the same RETURN finishes and nothing starts by itself");
    post.editorBeingDeleted (editor.get()); editor.reset();
}

// Offline callbacks keep arriving while RETURN cannot change the output. The queued press ends,
// but the already requested return remains pending until realtime callbacks resume.
template<class Require> void verifyCandidateOfflineReturnEntry (Require require)
{
    using namespace live_compare;
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.pair(); auto& post = host.post();
    require (post.startLiveCompare() == StartResult::started, "LISTEN starts before the offline return");
    host.render (100, false, false);
    MatchResult level; level.failure = MatchFailure::none; level.measuredDb = 8.0;
    level.prePeakDbtp = -1.0; level.postPeakDbtp = -4.0; level.ceilingDbtp = -1.0;
    require (post.applyLiveCompareMatch (planMatch (level, 0.0), MatchChoice::lowerPost), "MATCH lowers POST");
    host.render (30, false, false); post.stopLiveCompare(); host.render (10, false, false);
    require (post.liveCompareStatus().postTarget < 1.0f, "POST stays lowered");
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
    editor->setSize (900, 600); editor->setVisible (true); candidateTick (*editor);
    pressComparisonMenuEntry (*editor, "LISTEN", require);
    require (afterQueuedMessages ([] {}), "deliver the LISTEN press");
    require (candidateEntryPending (*editor) && post.liveCompareStatus().finishing, "RETURN is requested");
    candidateEntryDeadline (*editor) = juce::Time::getMillisecondCounterHiRes() * 0.001 + 120.0;
    post.setNonRealtime (true);
    for (int i = 0; i < 1200; ++i) { host.render (1, false, false); candidateTick (*editor); }
    require (candidateEntryPending (*editor) && post.liveCompareStatus().finishing, "offline callbacks hold RETURN");
    expireCandidateEntryDeadline (*editor); candidateTick (*editor);
    require (! candidateEntryPending (*editor)
                 && candidateToast (*editor) == "Rendering offline: press again after it finishes",
             "the expired press explains offline rendering and stops waiting");
    require (post.liveCompareStatus().finishing, "the RETURN itself remains requested");
    post.setNonRealtime (false);
    for (int i = 0; i < 100 && post.liveCompareStatus().finishing; ++i) { host.render (1, false, false); candidateTick (*editor); }
    require (! post.liveCompareStatus().finishing && post.liveCompareStatus().postActual == 1.0f
                 && ! post.liveCompareStatus().active, "realtime completes RETURN without starting the expired press");
    post.editorBeingDeleted (editor.get()); editor.reset();
}

template<class Require> void verifyAfterTailBlockedReturn (Require require)
{
    using namespace live_compare;
    using namespace live_compare_ui;
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const bool offline : { false, true })
    {
        const i18n::ScopedLanguage scoped (language);
        ProductTestClock wall;
        CandidateHost<Require> host (wall, require, 2, 512);
        host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "A/B starts");
        host.render (100, true, false, false);
        MatchResult level; level.failure = MatchFailure::none; level.measuredDb = 8.0;
        level.prePeakDbtp = -1.0; level.postPeakDbtp = -4.0; level.ceilingDbtp = -1.0;
        require (post.applyLiveCompareMatch (planMatch (level, 0.0), MatchChoice::lowerPost), "MATCH lowers POST");
        host.render (30, true, false, false);
        if (offline) post.setNonRealtime (true); else post.getBypassParameter()->setValue (1.0f);
        post.finishLiveCompare(); host.render (20, true, false, false);
        require (post.candidateReturning() || post.liveCompareStatus().finishing, "blocked callbacks hold return");
        auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
        editor->setSize (900, 600); editor->setVisible (true); candidateTick (*editor);
        auto& actual = dynamic_cast<KirinHyphaEditor&> (*editor);
        actual.*candidateEditorTick (CandidateEditorDomain {}) = observatory::Domain::reference;
        auto& waiting = actual.*candidateEditorTick (CandidateEditorTailState {});
        const auto pair = post.pairWaitSnapshot();
        require (pair.stable, "waiting press has a stable pair");
        for (const auto action : { AfterTail::referenceV, AfterTail::referenceC, AfterTail::referenceB,
                                   AfterTail::keepPair, AfterTail::keepAll })
        {
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            const auto activity = action == AfterTail::keepPair || action == AfterTail::keepAll
                ? output_owner::Activity::recordKeep : output_owner::Activity::audition;
            if (action == AfterTail::keepAll)
                waiting = { action, pair.key, pair.ticket, now, 0.0, true }; // roster staging is covered separately
            else
                require ((actual.*candidateEditorTick (CandidateEditorWaitTail {})) (activity, action)
                             && waiting.action == action, "the actual entry queues this press");
            waiting.since = now - 9.0;
            (actual.*candidateEditorTick (CandidateEditorServiceTail {}))();
            require (waiting.action == action, "press waits before ten seconds");
            waiting.since = now - 10.1;
            (actual.*candidateEditorTick (CandidateEditorServiceTail {}))();
            if (action == AfterTail::keepPair || action == AfterTail::keepAll)
            {
                require (waiting.action == action, "a blocked Keep keeps waiting for the tail past ten seconds");
                require (returnBlock (post) == (offline ? ReturnBlock::offline : ReturnBlock::bypass)
                             && juce::String (afterTailNotice (action, true, returnBlock (post)))
                                    == (offline ? keepOfflineWaitNotice : keepBypassWaitNotice),
                         "a blocked Keep says what blocks it and that it still starts by itself");
                waiting.since = now - 121.0;
                (actual.*candidateEditorTick (CandidateEditorServiceTail {}))();
                require (waiting.action == AfterTail::none
                             && candidateToast (*editor) == (offline ? offlineHoldsReturnNotice : bypassHoldsReturnNotice),
                         "a blocked Keep still ends at its two-minute deadline, saying what still holds it");
                continue;
            }
            const auto* expected = offline ? offlineHoldsReturnNotice : bypassHoldsReturnNotice;
            require (waiting.action == AfterTail::none && candidateToast (*editor) == expected,
                     "press ends at ten seconds with the blocked return reason");
            require (text_style::shownText (expected) == i18n::translate (expected, language),
                     "the blocked return reason is shown in the current language");
            if (language == i18n::Language::japanese)
                require (text_style::shownText (expected) != expected, "the blocked reason is translated");
        }
        post.setNonRealtime (false); post.getBypassParameter()->setValue (0.0f);
        for (const auto action : { AfterTail::keepPair, AfterTail::keepAll })
        {
            const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
            waiting = { action, pair.key, pair.ticket, now - 121.0, 0.0, action == AfterTail::keepAll };
            (actual.*candidateEditorTick (CandidateEditorServiceTail {}))();
            require (waiting.action == AfterTail::none && candidateToast (*editor) == keepTailEndedNotice,
                     "unproven tail keeps the two-minute Keep deadline");
            require (juce::String (afterTailNotice (action, false)) == "Play to continue, then Keep",
                     "stopped Keep asks for playback");
        }
        post.editorBeingDeleted (editor.get()); editor.reset();
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
