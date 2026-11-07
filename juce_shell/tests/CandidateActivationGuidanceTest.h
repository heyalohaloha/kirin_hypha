#pragma once
#include "CandidateProductHost.h"
#include "CandidateRecoveryTest.h"

namespace hypha::tests
{
template<class Require> void verifyCandidateActivationGuidance (Require require)
{
    using namespace live_compare;
    for (const bool blind : { false, true }) for (const int cause : { 0, 1, 2 })
    {
        ProductTestClock wall;
        CandidateHost<Require> host (wall, require, 2, 512);
        host.pair(); auto& post = host.post();
        require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started,
                 "activation guidance starts A/B");
        post.selectLiveComparePre (true); host.render (1000, true, true);
        if (blind)
        {
            require (post.beginLiveBlind() == StartResult::started, "activation guidance starts BLIND");
            host.render (1000, true, false, false); // BLIND's MATCH ramps A's gain as it plays
            require (post.liveBlindStatus().stage == BlindStage::active, "activation BLIND reaches playback");
        }
        const auto command = LiveTimingFixtureAccess::command (post);
        const auto gain = post.liveCompareStatus().gain;
        if (cause == 0) host.pre2().hostComponentActivationChanged (false);
        if (cause == 1) host.pre2().getBypassParameter()->setValue (1.0f);
        if (cause != 1) host.pre2Enabled = false;
        if (cause == 1) host.render (1, true, blind ? false : true, false); // the 5 ms bypass fade to a
        host.render (400, false, blind ? false : true, false);
        const char* expected = cause == 0 ? "A/B: enable PRE2 in the DAW"
            : cause == 1 ? "A/B: enable PRE2; bypass is on"
                         : "A/B: timing unavailable; check PRE1 and PRE2";
        require (post.liveCompareStatus().prolongedWait, "guidance follows a prolonged callback gap");
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
        {
            const i18n::ScopedLanguage scoped (language);
            auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
            editor->setVisible (true);
            for (const auto size : observatory::sizePresets)
            {
                editor->setSize (size.width, size.height); candidateTick (*editor);
                if (blind)
                {
                    auto* screen = candidateControl (*editor, "live-blind-screen");
                    auto* label = screen ? dynamic_cast<juce::Label*> (candidateControl (*screen, "live-blind-text-5")) : nullptr;
                    require (screen && screen->isVisible() && label
                        && text_style::shownText (label->getText()) == text_style::shownText (expected),
                        "BLIND editor shows the current activation cause in both languages");
                    const auto view = live_blind_ui::screenFor (post.liveBlindStatus(), true, 1.0f);
                    require (view.sourceOne == "SOURCE 1" && view.sourceTwo == "SOURCE 2", "guidance preserves anonymity");
                }
                else
                {
                    const auto warning = dynamic_cast<KirinHyphaEditor&> (*editor).*candidateEditorTick (CandidateEditorWarning {});
                    require (text_style::shownText (warning) == text_style::shownText (expected),
                             "LISTEN editor shows the current activation cause in both languages");
                }
            }
            post.editorBeingDeleted (editor.get());
        }
        if (cause == 0)
        {
            host.pre2Enabled = true; host.render (1, false, false, false);
            require (post.candidateHostInactive(), "a callback cannot erase the host inactive notification");
            host.pre2().hostComponentActivationChanged (true);
        }
        if (cause == 1) host.pre2().getBypassParameter()->setValue (0.0f);
        host.pre2Enabled = true;
        host.render (1, true, false, false); // A/B never ended: PRE2 may fade back to x at once
        requireCandidateRawPost (host, require);
        host.render (300, true, blind ? false : true);
        require (post.liveCompareStatus().verdict == Verdict::accepted && post.liveCompareStatus().gain == gain,
                 "fresh timing automatically restores the same gain");
        if (blind)
        {
            const auto resumed = LiveTimingFixtureAccess::command (post);
            require (resumed.epoch() == command.epoch() && resumed.stimulus() == command.stimulus()
                && resumed.firstPre() == command.firstPre(), "BLIND keeps its epoch, selection and assignment");
        }
        else require (post.liveCompareStatus().preSelected, "LISTEN keeps its selection");
    }
}
}
