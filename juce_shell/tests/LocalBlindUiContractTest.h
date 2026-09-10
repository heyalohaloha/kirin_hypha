#pragma once

#include "../src/HyphaLocalBlindComponent.h"
#include "../src/HyphaObservatoryView.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
inline void verifyLocalBlindUiContract()
{
    const auto require = [] (bool value, const char* message)
    {
        if (! value)
        {
            std::cerr << "Local Blind UI: " << message << '\n';
            std::exit (EXIT_FAILURE);
        }
    };
    require (local_blind_ui::productEntryEnabled,
             "product entry opens after the macOS AU exact PDC proof");

    observatory::View post (observatory::Role::post);
    observatory::View pre (observatory::Role::pre);
    auto* postEntry = dynamic_cast<juce::Button*> (
        post.findChildWithID ("observatory-local-blind"));
    auto* preEntry = dynamic_cast<juce::Button*> (
        pre.findChildWithID ("observatory-local-blind"));
    require (postEntry != nullptr && preEntry != nullptr, "one shared entry control exists");
    post.setLocalBlindEntryEnabled (true);
    pre.setLocalBlindEntryEnabled (true);
    for (const auto preset : observatory::sizePresets)
    {
        post.setSize (preset.width, preset.height);
        pre.setSize (preset.width, preset.height);
        require (postEntry->isVisible() == observatory::isFullDensity (preset.density),
                 "POST entry exists only in a large analysis frame");
        require (! preEntry->isVisible(), "PRE never consumes a second Blind UI slot");
    }

    local_blind_ui::Component component;
    component.setPresentationContext (presentation::forEditor (600, 400));
    component.setSize (600, 400);
    const auto button = [&] (const char* id)
    {
        auto* result = dynamic_cast<juce::Button*> (component.findChildWithID (id));
        require (result != nullptr, id);
        return result;
    };
    const auto labelText = [&] (const char* id)
    {
        auto* result = dynamic_cast<juce::Label*> (component.findChildWithID (id));
        require (result != nullptr, id);
        return result->getText();
    };

    local_blind::ProductSessionView ready;
    ready.phase = local_blind::ProductSessionPhase::ready;
    ready.sampleRate = 48'000;
    ready.start = 48'000;
    ready.frames = 192'000;
    ready.lowerPostGainDb = -5.5;
    ready.trial.lowerPostApprovalRequired = true;
    component.setState (ready);
    auto* start = button ("local-blind-start");
    require (start->isVisible() && start->getButtonText().contains ("5.5"),
             "required fixed attenuation is shown before approval");
    bool approved = false;
    component.onStart = [&] (bool value) { approved = value; };
    start->onClick();
    require (approved, "attenuation approval is explicit");

    ready.trial.lowerPostApprovalRequired = false;
    component.setState (ready);
    require (start->getTitle().contains ("PRE is matched to POST with fixed gain")
                 && start->getTitle().contains ("Solo and routing stay unchanged"),
             "normal start describes the gain reference and preserves DAW mix context");

    local_blind::ProductSessionView listening = ready;
    listening.phase = local_blind::ProductSessionPhase::listening;
    listening.trial.phase = local_blind::TrialPhase::listening;
    listening.trial.activeStimulus = 1;
    component.setState (listening);
    require (! labelText ("local-blind-title").containsIgnoreCase ("PRE")
                 && ! labelText ("local-blind-title").containsIgnoreCase ("POST")
                 && ! labelText ("local-blind-status").containsIgnoreCase ("PRE")
                 && ! labelText ("local-blind-status").containsIgnoreCase ("POST"),
             "source identity is absent before reveal");
    require (! button ("local-blind-answer-1")->isEnabled()
                 && ! button ("local-blind-reveal")->isEnabled(),
             "answer and reveal wait for both complete passes");
    component.setPresentationContext (presentation::forEditor (300, 200));
    component.setSize (300, 200);
    for (int index = 0; index < component.getNumChildComponents(); ++index)
    {
        const auto* child = component.getChildComponent (index);
        require (! child->isVisible() || (! child->getBounds().isEmpty()
                    && component.getLocalBounds().contains (child->getBounds())),
                 "every active control remains usable at the minimum editor size");
    }

    listening.trial.canAnswer = true;
    component.setState (listening);
    local_blind::TrialAnswer answer = local_blind::TrialAnswer::none;
    component.onAnswer = [&] (auto value) { answer = value; };
    button ("local-blind-answer-neither")->onClick();
    require (answer == local_blind::TrialAnswer::noPreference,
             "no-preference answer stays distinct from cannot-distinguish");
    listening.trial.answer = answer;
    component.setState (listening);
    require (button ("local-blind-reveal")->isEnabled(),
             "answered complete trial can reveal");

    listening.phase = local_blind::ProductSessionPhase::revealed;
    listening.trial.phase = local_blind::TrialPhase::revealed;
    listening.trial.revealedOneSide = 1;
    component.setState (listening);
    require (labelText ("local-blind-status").contains ("SOURCE 1 = PRE"),
             "identity appears only after reveal");

    listening.phase = local_blind::ProductSessionPhase::returnPending;
    listening.trial.phase = local_blind::TrialPhase::returnPending;
    component.setState (listening);
    require (button ("local-blind-return")->isVisible()
                 && local_blind_ui::blocksDisclosure (listening)
                 && labelText ("local-blind-detail").contains ("unchanged live signal"),
             "stopped trial requires explicit live return and keeps disclosure blocked");
    listening.phase = local_blind::ProductSessionPhase::returned;
    listening.trial.phase = local_blind::TrialPhase::returned;
    component.setState (listening);
    require (button ("local-blind-close")->isVisible()
                 && ! local_blind_ui::blocksDisclosure (listening),
             "confirmed live return releases the screen");
}
}
