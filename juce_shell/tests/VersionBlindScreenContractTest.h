#pragma once

// 2026-10-04（形式と見た目を PRE/POST Blind にそろえる。「答えを表示」は分かりにくかった）：
// REF の VERSION BLIND は LIVE BLIND と同じ部品・同じ置き方で窓全体に出る。
// 指示の文は LIVE BLIND と同じ（再生したまま 1 と 2 を切り替える → 聴き比べたら開示 → 開示した）で、「答え」とは
// 言わない。開示の前のボタンと読み上げは対応を言わず、開示の後は「1: V」「2: A」のようにそのまま切り替えられる。
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaVersionBlindScreen.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"
#include <iostream>

namespace hypha::tests
{
inline void verifyVersionBlindScreen()
{
    const auto require = [] (bool value, const juce::String& what)
    {
        if (! value) { std::cerr << "Version Blind screen: " << what << '\n'; std::exit (1); }
    };
    using reference_ui::BlindPhase;
    enum Case { startingStopped, starting, none, pendingTwo, oneHeard, bothHeard, paused, outside, lowered,
                revealedV, revealedA, stopped, stoppedHeld, count };
    const auto stateFor = [] (int which)
    {
        reference_ui::State state;
        state.separateComparisons = true;
        state.transportPlaying = which != startingStopped && which != paused;
        state.blindPhase = which <= starting ? BlindPhase::starting
            : which == revealedV || which == revealedA ? BlindPhase::revealed
            : which >= stopped ? BlindPhase::invalidated : BlindPhase::active;
        state.activeBlindStimulus = which == oneHeard || which == pendingTwo ? 1
            : which == outside ? 0 : which > oneHeard && which <= revealedA ? 2 : 0;
        state.pendingBlindStimulus = which == pendingTwo ? 2 : 0;
        state.blindStimulusOneHeard = which >= oneHeard;
        state.blindStimulusTwoHeard = which >= bothHeard;
        state.blindPaused = which == paused;
        state.blindOutsideSong = which == outside;
        state.blindRequiredAAttenuationDb = which == lowered || which == stoppedHeld ? 4.2 : 0.0;
        state.blindOneIsComparison = which == revealedV;
        return state;
    };
    blind_ui::ScreenComponent view ("version-blind");
    live_blind_ui::Component live;
    const auto button = [&view] (const char* id) { return dynamic_cast<juce::Button*> (view.findChildWithID (id)); };
    int cases = 0;
    for (auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        i18n::ScopedLanguage scoped (language);
        for (auto preset : observatory::sizePresets)
            for (int which = 0; which < count; ++which)
            {
                const auto state = stateFor (which);
                const auto screen = reference_ui::versionBlindScreen (state);
                view.setSize (preset.width, preset.height);
                view.setScreen (screen);
                auto* one = button ("version-blind-source-1");
                auto* two = button ("version-blind-source-2");
                auto* reveal = button ("version-blind-reveal");
                auto* end = button ("version-blind-end");
                require (one && two && reveal && end && view.findChildWithID ("version-blind-answer") == nullptr,
                         "the same controls as PRE/POST Blind, and no answer collection");
                const bool audition = state.blindPhase == BlindPhase::active || state.blindPhase == BlindPhase::revealed;
                require (one->isVisible() == audition && two->isVisible() == audition, "1 and 2 show while comparing");
                require (end->isVisible() && end->isEnabled(), "END is always there");
                require (reveal->isVisible() == (state.blindPhase == BlindPhase::active)
                             && reveal->isEnabled() == (state.blindPhase == BlindPhase::active
                                                        && state.blindStimulusOneHeard && state.blindStimulusTwoHeard),
                         "reveal is offered while comparing, once both sources were heard");
                if (which == pendingTwo) require (one->isEnabled() && ! two->isEnabled(), "a pending source waits");
                if (which == paused) require (! one->isEnabled() && ! two->isEnabled(), "paused sources wait for play");
                if (which == oneHeard) require (one->getToggleState() && ! two->getToggleState(), "the audible source is lit");
                // 指示の文：LIVE BLIND と同じ言葉で、「答え」とは言わない。
                const char* expected[] { "Play the DAW to begin", "Preparing the sources; A plays", "Try both sources while playing",
                                         "Try both sources while playing", "Try both sources while playing",
                                         "Reveal the sources when ready", "Play the DAW to continue", "Play within the song; A plays",
                                         "Reveal the sources when ready", "Sources revealed; keep comparing",
                                         "Sources revealed; keep comparing", "Blind stopped; A plays", "Blind stopped; A plays" };
                require (screen.instruction == expected[which], "instruction " + juce::String (which));
                require (screen.title == (which == revealedV || which == revealedA ? "BLIND RESULT" : "VERSION BLIND"),
                         "the title reads like LIVE BLIND");
                require (screen.detail == (which == lowered || which == stoppedHeld ? juce::String ("END returns +4.2 dB")
                                                                                      : juce::String ("END returns to A")),
                         "the line under the buttons says what END returns");
                if (language == i18n::Language::japanese)
                    for (const auto& text : { screen.instruction, screen.detail })
                        require (i18n::tr (text) != text, "Japanese for " + text);
                if (which == revealedV)
                    require (one->getButtonText() == "1: V" && two->getButtonText() == "2: A", "the result names V and A");
                if (which == revealedA)
                    require (one->getButtonText() == "1: A" && two->getButtonText() == "2: V", "the result names A and V");
                if (! (which == revealedV || which == revealedA))
                    require (one->getButtonText() == "SOURCE 1" && two->getButtonText() == "SOURCE 2"
                                 && one->getTitle() == "SOURCE 1" && two->getTitle() == "SOURCE 2",
                             "before the reveal, nothing names A or V");
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
                        require (text_style::shownWidth (font, action->getButtonText()) <= action->getWidth() - 12, "action fits whole");
                    }
                    if (auto* label = dynamic_cast<juce::Label*> (child))
                    {
                        juce::AttributedString text;
                        text.append (text_style::shownText (label->getText()), label->getFont(), COL_NORMAL);
                        juce::TextLayout layout;
                        const auto bounds = label->getBorderSize().subtractedFrom (label->getLocalBounds());
                        layout.createLayout (text, static_cast<float> (bounds.getWidth()));
                        require (layout.getHeight() <= bounds.getHeight(), "instruction fits without truncation: " + label->getText());
                    }
                }
                // 見た目は PRE/POST Blind と同じ：同じ大きさなら、1・2・開示・終了は同じ場所に同じ大きさで出る。
                live.setSize (preset.width, preset.height);
                for (const auto* id : { "source-1", "source-2", "reveal", "end" })
                {
                    const auto* mine = view.findChildWithID (juce::String ("version-blind-") + id);
                    const auto* theirs = live.findChildWithID (juce::String ("live-blind-") + id);
                    require (mine && theirs && mine->getBounds() == theirs->getBounds(), juce::String ("same place as LIVE BLIND: ") + id);
                }
                ++cases;
            }
    }
    std::cout << "Version Blind screen: PASS " << cases << " language/size/state cases\n";
}
}
