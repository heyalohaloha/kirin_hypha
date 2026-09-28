#pragma once

#include "../src/HyphaLanguage.h"
#include "../src/HyphaLocalBlindComponent.h"
#include "../src/HyphaLocalBlindSteps.h"
#include "../src/HyphaTextStyle.h"
#include "../src/HyphaObservatoryView.h"

#include <cstdlib>
#include <iostream>

// INV-LC17: the named A/B on the PRE / POST Blind screen. The screen that starts Blind offers it
// beside START BLIND wherever both labels fit whole; while it plays, the sources are named with
// the fixed gain each one plays at, either can be chosen at any time, nothing can be answered,
// START BLIND hides which is which again, and the second step stays lit.
namespace hypha::tests
{
inline void verifyLocalBlindNamedUiContract()
{
    const auto require = [] (bool value, const char* message)
    {
        if (! value)
        {
            std::cerr << "Local Blind named A/B UI: " << message << '\n';
            std::exit (EXIT_FAILURE);
        }
    };
    using Phase = local_blind::ProductSessionPhase;

    local_blind::ProductSessionView ready;
    ready.phase = Phase::ready;
    ready.sampleRate = 48000;
    ready.channels = 2;
    ready.start = 48000;
    ready.frames = 192000;
    ready.fixedPreGainDb = 6.0;
    auto named = ready;
    named.phase = Phase::listening;
    named.trial.named = true;
    named.trial.activeStimulus = 1;
    require (local_blind_ui::stepFor (named) == local_blind_ui::Step::start,
             "the named A/B keeps the start step lit");
    named.phase = Phase::armed;
    require (local_blind_ui::stepFor (named) == local_blind_ui::Step::start, "also while it waits");

    local_blind_ui::Component component;
    const auto button = [&] (const char* id)
    {
        auto* result = dynamic_cast<juce::Button*> (component.findChildWithID (id));
        require (result != nullptr, id);
        return result;
    };
    const auto label = [&] (const char* id)
    {
        auto* result = dynamic_cast<juce::Label*> (component.findChildWithID (id));
        require (result != nullptr, id);
        return result->getText();
    };
    const auto show = [&] (const local_blind::ProductSessionView& state, int width, int height)
    {
        component.setState (state);
        component.setPresentationContext (presentation::forEditor (width, height));
        component.setSize (width, height);
    };

    bool startedNamed = false, startedBlind = false, startedTrial = false;
    int selected = 0;
    component.onStartNamed = [&] (bool) { startedNamed = true; };
    component.onStartBlind = [&] { startedBlind = true; };
    component.onStart = [&] (bool) { startedTrial = true; };
    component.onSelectStimulus = [&] (int stimulus) { selected = stimulus; };

    show (ready, 600, 400);
    require (button ("local-blind-named")->isVisible() && button ("local-blind-start")->isVisible()
                 && button ("local-blind-named")->getButtonText() == "NAMED A/B",
             "the screen that starts Blind offers the named A/B beside START BLIND");
    button ("local-blind-named")->onClick();
    require (startedNamed && ! startedTrial, "NAMED A/B starts the named A/B");

    named.phase = Phase::listening;
    show (named, 600, 400);
    require (label ("local-blind-title").startsWith ("PRE / POST NAMED A/B")
                 && label ("local-blind-status") == "PLAYING PRE",
             "the named A/B says what plays");
    require (button ("local-blind-source-1")->getButtonText() == "PRE +6.0 dB"
                 && button ("local-blind-source-2")->getButtonText() == "POST"
                 && button ("local-blind-source-1")->isEnabled() && button ("local-blind-source-2")->isEnabled()
                 && button ("local-blind-source-1")->getToggleState(),
             "the sources are named with their fixed gain and either can be chosen");
    require (! button ("local-blind-answer-1")->isVisible() && ! button ("local-blind-reveal")->isVisible()
                 && ! button ("local-blind-named")->isVisible() && button ("local-blind-stop")->isVisible(),
             "nothing can be answered in the named A/B, and STOP stays");
    button ("local-blind-source-2")->onClick();
    require (selected == 2, "POST is chosen by name");
    require (button ("local-blind-start")->isVisible()
                 && button ("local-blind-start")->getButtonText() == "START BLIND", "START BLIND follows");
    startedTrial = false;
    button ("local-blind-start")->onClick();
    require (startedBlind && ! startedTrial, "START BLIND begins Blind on the same range");

    // With POST lowered by approval, PRE plays at its own level and POST shows the reduction.
    auto lowered = named;
    lowered.lowerPostGainDb = -18.0;
    show (lowered, 600, 400);
    require (button ("local-blind-source-1")->getButtonText() == "PRE"
                 && button ("local-blind-source-2")->getButtonText() == "POST -18.0 dB",
             "an approved POST reduction is shown on POST");
    auto approval = ready;
    approval.trial.lowerPostApprovalRequired = true;
    approval.lowerPostGainDb = -18.0;
    show (approval, 900, 600);
    require (button ("local-blind-named")->isVisible()
                 && button ("local-blind-named")->getButtonText() == "LOWER POST 18.0 dB & A/B",
             "the named A/B asks for the same approval as Blind");

    // Every named state at every size in both languages: whole text, inside the screen.
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        auto pending = named;
        pending.trial.pendingStimulus = 2;
        auto complete = named;
        complete.trial.passComplete = true;
        auto waiting = named;
        waiting.phase = Phase::armed;
        const local_blind::ProductSessionView states[] { ready, approval, named, lowered, pending, complete, waiting };
        for (std::size_t which = 0; which < std::size (states); ++which)
            for (const auto preset : observatory::sizePresets)
            {
                const auto& state = states[which];
                show (state, preset.width, preset.height);
                const auto context = presentation::forEditor (preset.width, preset.height);
                for (int index = 0; index < component.getNumChildComponents(); ++index)
                {
                    const auto* child = component.getChildComponent (index);
                    if (! child->isVisible()) continue;
                    require (! child->getBounds().isEmpty() && component.getLocalBounds().contains (child->getBounds()),
                             "every named control stays within the screen");
                    if (const auto* action = dynamic_cast<const juce::TextButton*> (child))
                        require (text_style::shownWidth (monoFont (context, typography::TextRole::action),
                                                         action->getButtonText()) <= action->getWidth() - 12,
                                 "every named label fits whole");
                    if (const auto* text = dynamic_cast<const juce::Label*> (child); text != nullptr && text->getText().isNotEmpty())
                    {
                        const auto shownLabel = text_style::shownText (text->getText());
                        const auto bounds = text->getBorderSize().subtractedFrom (text->getLocalBounds());
                        juce::AttributedString attributed;
                        attributed.append (shownLabel, text->getFont(), juce::Colours::white);
                        juce::TextLayout layout;
                        layout.createLayout (attributed, static_cast<float> (bounds.getWidth()));
                        const auto height = requiresJapaneseGlyphs (shownLabel)
                            ? text_style::wrappedHeight (shownLabel, nativeTextFontLike (text->getFont()), bounds.getWidth())
                            : layout.getHeight();
                        if (height > bounds.getHeight() + 1)
                            std::cerr << "named clipped label: " << preset.width << " " << text->getComponentID()
                                      << " " << shownLabel << '\n';
                        require (height <= bounds.getHeight() + 1, "every named instruction reads whole");
                    }
                }
                if (which == 0 && preset.width >= 600)
                    require (button ("local-blind-named")->isVisible(), "the named A/B is offered from 200%");
            }
    }
}
}
