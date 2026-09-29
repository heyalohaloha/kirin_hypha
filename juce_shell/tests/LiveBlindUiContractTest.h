#pragma once
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaTextStyle.h"
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
            for (int phase = 0; phase < 9; ++phase)
            {
                live_compare::LiveBlindStatus state;
                state.stage = phase == 0 ? Stage::preparing : phase == 1 ? Stage::approval
                    : phase == 7 ? Stage::invalidated : phase == 8 ? Stage::finishing : Stage::active;
                state.lowerPostDb = -24.0;
                state.trial.played = phase >= 3 ? 3 : 1;
                state.trial.audible = phase == 2 ? 1 : 2;
                state.trial.revealed = phase >= 4 && phase <= 6;
                state.trial.firstPre = phase == 4;
                state.trial.answer = phase - 3;
                view.setSize (preset.width, preset.height);
                view.setState (state, phase != 8, phase >= 7 ? 0.0631f : 1.0f);
                require (! button ("live-blind-end")->isEnabled() == (phase == 8), "END receipt controls availability");
                if (phase == 2) require (! button ("live-blind-answer")->isEnabled(), "one source cannot answer");
                if (phase == 3) require (button ("live-blind-answer")->isEnabled(), "both sources can answer");
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
