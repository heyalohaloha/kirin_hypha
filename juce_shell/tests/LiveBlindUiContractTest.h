#pragma once
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include <iostream>

namespace hypha::tests
{
inline void verifyLiveBlindUiContract()
{
    const auto require = [] (bool value, const char* what)
    {
        if (! value) { std::cerr << "Live Blind UI: " << what << '\n'; std::exit (1); }
    };
    using Stage = live_compare::BlindStage;
    live_blind_ui::Component view;
    const auto button = [&] (const char* id)
    { return dynamic_cast<juce::Button*> (view.findChildWithID (id)); };
    const auto preview = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
    int cases = 0;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (auto preset : observatory::sizePresets)
            for (int phase = 0; phase < 11 + static_cast<int> (live_compare::RecoveryReason::loopClockUnavailable); ++phase)
            {
                live_compare::LiveBlindStatus state;
                state.stage = phase == 0 ? Stage::preparing : phase == 1 ? Stage::approval
                    : phase == 7 || phase >= 11 ? Stage::invalidated : phase == 8 ? Stage::finishing
                    : phase >= 9 ? Stage::failed : Stage::active;
                state.waiting = phase == 9 ? live_compare::MatchFailure::outOfRange : live_compare::MatchFailure::invalidPlan;
                state.lowerPostDb = -24.0;
                state.trial.played = phase >= 3 ? 3 : 1;
                state.trial.audible = phase == 2 ? 1 : 2;
                state.trial.revealed = phase >= 4 && phase <= 6;
                state.trial.firstPre = phase == 4;
                state.reason = phase >= 11 ? static_cast<live_compare::RecoveryReason> (phase - 10)
                                          : live_compare::RecoveryReason::none;
                state.observation = phase == 0 ? live_compare::RecoveryReason::loopUnproven : state.reason;
                state.contentHeld = state.reason == live_compare::RecoveryReason::contentChanged;
                state.compensationOff = state.reason == live_compare::RecoveryReason::compensationOff;
                view.setSize (preset.width, preset.height);
                view.setState (state, phase != 8, phase >= 7 ? 0.0631f : 1.0f);
                if (phase == 0)
                    require (i18n::tr (dynamic_cast<juce::Label*> (
                                           view.findChildWithID ("live-blind-text-2"))->getText())
                                 == (language == i18n::Language::japanese
                                     ? juce::String::fromUTF8 (u8"このLOOPではDAWの時刻対応を確認できません")
                                     : juce::String ("DAW timing is unavailable for this loop")),
                             "unproven initial loop is unavailable, not a promise of progress");
                if (state.observation == live_compare::RecoveryReason::loopUnproven)
                    require (live_compare_ui::blindRecovery (state, phase != 0).action
                                 == live_compare_ui::RecoveryAction::none,
                             "unknown loop proof does not request LOOP off or promise automatic recovery");
                require (! button ("live-blind-end")->isEnabled() == (phase == 8), "END receipt controls availability");
                require (view.findChildWithID ("live-blind-answer") == nullptr, "no unused preference collection");
                if (phase == 2) require (! button ("live-blind-reveal")->isEnabled(), "one source cannot reveal");
                if (phase == 3) require (button ("live-blind-reveal")->isEnabled(), "both sources can reveal");
                if (phase >= 9) require (! button ("live-blind-source-1")->isVisible()
                    && ! button ("live-blind-reveal")->isVisible() && button ("live-blind-end")->isEnabled(),
                    "failed MATCH offers END, never source selection or reveal");
                for (int i = 0; i < view.getNumChildComponents(); ++i)
                {
                    auto* child = view.getChildComponent (i);
                    if (! child->isVisible()) continue;
                    require (view.getLocalBounds().contains (child->getBounds()) && ! child->getBounds().isEmpty(),
                             "every control stays in the editor");
                    for (int j = i + 1; j < view.getNumChildComponents(); ++j)
                        require (! view.getChildComponent (j)->isVisible()
                            || ! child->getBounds().intersects (view.getChildComponent (j)->getBounds()), "no overlap");
                    if (auto* action = dynamic_cast<juce::TextButton*> (child))
                    {
                        const auto font = labelFont (presentation::forEditor (preset.width, preset.height), typography::TextRole::action);
                        require (text_style::shownWidth (font, action->getButtonText()) <= action->getWidth() - 12,
                                 "action fits whole");
                        if (phase == 2 || phase == 3)
                            require (! action->getTitle().contains ("PRE") && ! action->getTitle().contains ("POST"),
                                     "anonymous titles never disclose the mapping");
                    }
                    if (auto* label = dynamic_cast<juce::Label*> (child))
                    {
                        juce::AttributedString text;
                        text.append (text_style::shownText (label->getText()), label->getFont(), COL_NORMAL);
                        juce::TextLayout layout;
                        const auto bounds = label->getBorderSize().subtractedFrom (label->getLocalBounds());
                        layout.createLayout (text, static_cast<float> (bounds.getWidth()));
                        if (layout.getHeight() > bounds.getHeight()) std::cerr << preset.width << " phase " << phase
                            << ' ' << text_style::shownText (label->getText()) << '\n';
                        require (layout.getHeight() <= bounds.getHeight(), "instruction fits without truncation");
                    }
                }
                if (preview.isNotEmpty())
                {
                    const auto file = juce::File (preview).getChildFile ("live-blind-"
                        + juce::String (static_cast<int> (language)) + "-" + juce::String (phase)
                        + "-" + juce::String (preset.width) + ".png");
                    auto stream = file.createOutputStream();
                    require (stream != nullptr, "preview file opens");
                    require (juce::PNGImageFormat().writeImageToStream (view.createComponentSnapshot (view.getLocalBounds()), *stream),
                             "preview written");
                }
                ++cases;
            }
    }
    std::cout << "Live Blind UI: PASS " << cases << " language/size/state cases\n";
}
}
