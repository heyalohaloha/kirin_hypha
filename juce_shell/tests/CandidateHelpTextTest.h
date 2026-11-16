#pragma once
#include "CandidateProductHost.h"
#include "../src/HyphaHelpLineText.h"

namespace hypha::tests
{
template<class Require> void verifyCandidateHelpText (Require require)
{
    using namespace live_compare;
    const std::array<const char*, 5> descriptions {{
        "Match A to B loudness over the latest four seconds",
        "MATCH is on. Press MATCH to turn it off; MATCH again and AUTO are in MENU",
        "AUTO: A follows B loudness within 0.5 dB, up to 6 dB from your MATCH, lower if the TP ceiling needs it. Press AUTO to turn MATCH off; MATCH again and AUTO are in MENU",
        "MATCH stopped at the true-peak ceiling: A is still quieter than B. Press to turn MATCH off; MATCH again is in MENU",
        "MATCH is off: A and B play at their own levels. Press MATCH to turn the same MATCH on" }};
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage scoped (language);
        observatory::View view (observatory::Role::post); view.setVisible (true);
        observatory::LiveCompareFooter footer;
        footer.entryEnabled = footer.active = footer.candidate = true;
        for (const auto size : observatory::sizePresets)
        {
            view.setSize (size.width, size.height);
            for (int state = 0; state < 5; ++state)
            {
                footer.matched = state == 1 || state == 2;
                footer.following = state == 2; footer.matchLimited = state == 3; footer.matchOff = state == 4;
                view.setLiveCompareFooter (footer);
                auto* button = dynamic_cast<juce::Button*> (candidateControl (view, "observatory-live-match"));
                require (button && button->getDescription() == descriptions[static_cast<std::size_t> (state)]
                    && button->getTooltip() == button->getDescription()
                    && button->getTitle() == button->getButtonText(), "actual MATCH control retains full accessible help");
                require (text_style::shownText (button->getDescription()).isNotEmpty(), "MATCH help is translated");
                const auto shortened = help_line::forLine (button->getDescription(), false);
                require (shortened.isNotEmpty() && text_style::shownText (shortened).isNotEmpty(), "short help is translated");
                if (state == 1 || state == 2) require (shortened.contains ("off"), "matched help says a press turns MATCH off");
                if (state == 4) require (shortened.contains ("turn it on"), "MATCH off help says a press turns it on");
            }
        }
    }
    ProductTestClock wall;
    CandidateHost<Require> host (wall, require, 2, 512);
    host.delayA = host.delayB = 0; host.pair(); auto& post = host.post();
    post.setLiveCompareAutoAlign (true);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "help fixture starts A/B");
    host.render (1000, true, false);
    auto editor = std::unique_ptr<juce::AudioProcessorEditor> (post.createEditorIfNeeded());
    editor->setSize (900, 600); editor->setVisible (true); candidateTick (*editor);
    auto estimate = post.measureLiveCompareOffset(); estimate.lagFrames = -1545;
    LiveTimingFixtureAccess::publishMeasuredAlign (post, estimate); LiveTimingFixtureAccess::settleContent (post); candidateTick (*editor);
    auto warning = dynamic_cast<KirinHyphaEditor&> (*editor).*candidateEditorTick (CandidateEditorWarning {});
    require (!warning.contains ("32.19 ms aligned"), "requested correction is not yet displayed as applied");
    host.render (3, true, false, false); candidateTick (*editor);
    warning = dynamic_cast<KirinHyphaEditor&> (*editor).*candidateEditorTick (CandidateEditorWarning {});
    require (warning == "A 32.19 ms aligned"
        && i18n::translate (warning, i18n::Language::japanese) == juce::String::fromUTF8 (u8"Aの位置補正：32.19 ms適用済み"),
        "applied processor correction is shown in Japanese");
    post.finishLiveCompare(); host.render (10, true, false, false); host.render (800, false, false);
    require (post.startCandidateCompare (host.pre2().instanceId(), true) == StartResult::started, "help fixture restarts A/B");
    candidateTick (*editor);
    warning = dynamic_cast<KirinHyphaEditor&> (*editor).*candidateEditorTick (CandidateEditorWarning {});
    require (!warning.contains ("32.19 ms aligned"), "new A/B epoch hides the previous applied correction");
    post.editorBeingDeleted (editor.get());
}
}
