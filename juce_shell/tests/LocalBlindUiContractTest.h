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
    require (local_blind_ui::productEntryEnabled (
                 juce::AudioProcessor::wrapperType_VST3)
                 && local_blind_ui::productEntryEnabled (
                     juce::AudioProcessor::wrapperType_AudioUnit),
             "product entry opens only for wrappers with exact PDC proof");
    require (! local_blind_ui::productEntryEnabled (
                 juce::AudioProcessor::wrapperType_AAX)
                 && ! local_blind_ui::productEntryEnabled (
                     juce::AudioProcessor::wrapperType_Undefined),
             "AAX and unknown wrappers fail closed until host proof exists");

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
        require (! postEntry->isVisible(),
                 "POST Blind entry is consolidated into the operations menu");
        require (! preEntry->isVisible(), "PRE never consumes a second Blind UI slot");
    }
    require (post.localBlindEntryAvailable(),
             "POST Blind capability remains available to the operations menu");
    auto* contextEntry = dynamic_cast<juce::Button*> (
        post.findChildWithID ("observatory-meter-context"));
    require (contextEntry != nullptr, "Meter Context control remains in the shared header");
    bool contextMenuRequested = false;
    post.onContextMenu = [&] { contextMenuRequested = true; };
    contextEntry->onClick();
    require (contextMenuRequested && post.meterContext() == meter_context::defaultContext,
             "Meter Context opens an explanatory menu instead of changing immediately");

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

    local_blind::ProductSessionView idle;
    component.setMeterContext (meter_context::MeterContext::twoMix);
    component.setState (idle);
    require (labelText ("local-blind-title").contains ("2MIX")
                 && labelText ("local-blind-status").contains ("MIX / MASTER BUS")
                 && labelText ("local-blind-detail").contains ("continuous active sections"),
             "2MIX preflight states its use and Gain Match evidence");
    bool captureRequested = false;
    bool preflightContextRequested = false;
    component.onCapture = [&] { captureRequested = true; };
    component.onContextMenu = [&] { preflightContextRequested = true; };
    button ("local-blind-capture")->onClick();
    button ("local-blind-context")->onClick();
    require (captureRequested && preflightContextRequested
                 && button ("local-blind-close")->getButtonText() == "BACK",
             "preflight requires an explicit capture and keeps a way back");
    component.setMeterContext (meter_context::MeterContext::trackStem);
    require (labelText ("local-blind-title").contains ("TRACK / STEM")
                 && labelText ("local-blind-detail").contains ("short or sparse events"),
             "TRACK / STEM preflight states its different Gain Match evidence");
    component.setActionNotice ("START PLAYBACK BEFORE CAPTURE");
    require (labelText ("local-blind-result") == "START PLAYBACK BEFORE CAPTURE",
             "preflight action failures remain visible inside the isolated screen");
    component.clearActionNotice();
    for (const auto preset : observatory::sizePresets)
    {
        component.setPresentationContext (presentation::forEditor (preset.width, preset.height));
        component.setSize (preset.width, preset.height);
        if (preset.width == 300)
            require (button ("local-blind-context")->getButtonText() == "CONTEXT",
                     "minimum-size context action uses its complete compact label");
        for (int index = 0; index < component.getNumChildComponents(); ++index)
        {
            const auto* child = component.getChildComponent (index);
            require (! child->isVisible() || (! child->getBounds().isEmpty()
                        && component.getLocalBounds().contains (child->getBounds())),
                     "preflight controls remain usable at every editor size");
        }
        const auto previewDirectory = juce::SystemStats::getEnvironmentVariable (
            "KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
        if (previewDirectory.isNotEmpty())
        {
            juce::Image preview (juce::Image::ARGB, preset.width, preset.height, true);
            juce::Graphics graphics (preview);
            component.paintEntireComponent (graphics, true);
            auto output = juce::File (previewDirectory).getChildFile (
                "local-blind-preflight-" + juce::String (preset.width) + ".png")
                              .createOutputStream();
            require (output != nullptr
                         && output->setPosition (0) && output->truncate().wasOk()
                         && juce::PNGImageFormat().writeImageToStream (preview, *output),
                     "preflight preview can be written for visual inspection");
        }
    }
    component.setPresentationContext (presentation::forEditor (600, 400));
    component.setSize (600, 400);

    local_blind::ProductSessionView ready;
    ready.phase = local_blind::ProductSessionPhase::ready;
    ready.sampleRate = 48'000;
    ready.start = 48'000;
    ready.frames = 192'000;
    ready.lowerPostGainDb = -5.5;
    ready.trial.lowerPostApprovalRequired = true;
    ready.gainPolicy = local_blind::GainMatchPolicy::exactTrackEventEnergyV1;
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
    require (labelText ("local-blind-detail").contains ("play from before")
                 && ! labelText ("local-blind-detail").contains ("loop this exact range"),
             "the DAW may use preroll instead of sample-exact positioning and loop editing");
    require (start->getTitle().contains ("PRE is matched to POST with fixed gain")
                 && start->getTitle().contains ("Solo and routing stay unchanged"),
             "normal start describes the gain reference and preserves DAW mix context");

    local_blind::ProductSessionView listening = ready;
    listening.phase = local_blind::ProductSessionPhase::listening;
    listening.trial.phase = local_blind::TrialPhase::listening;
    listening.trial.activeStimulus = 1;
    component.setState (listening);
    require (labelText ("local-blind-title").contains ("TRACK / STEM"),
             "captured context stays visible from the frozen Gain Match policy");
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

    listening.trial.passComplete = true;
    listening.trial.heardOneComplete = true;
    component.setState (listening);
    require (labelText ("local-blind-status").contains ("PASS COMPLETE")
                 && labelText ("local-blind-detail").contains ("Select the other source")
                 && button ("local-blind-source-2")->isEnabled()
                 && ! button ("local-blind-answer-1")->isEnabled(),
             "a completed first pass guides the next explicit audition without disclosing assignment");
    listening.trial.canAnswer = true;
    listening.trial.heardTwoComplete = true;
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

    // Exercise the actual presentation at every product size, including the instructions that
    // connect two completed passes. Child rectangles alone cannot detect clipped text.
    for (auto phase : { local_blind::ProductSessionPhase::idle,
                        local_blind::ProductSessionPhase::capturing,
                        local_blind::ProductSessionPhase::preparing,
                        local_blind::ProductSessionPhase::ready,
                        local_blind::ProductSessionPhase::armed,
                        local_blind::ProductSessionPhase::listening,
                        local_blind::ProductSessionPhase::returnPending,
                        local_blind::ProductSessionPhase::revealed,
                        local_blind::ProductSessionPhase::returned })
        for (const auto preset : observatory::sizePresets)
        {
            auto state = ready;
            state.phase = phase;
            state.trial.activeStimulus = 1;
            state.trial.passComplete = phase == local_blind::ProductSessionPhase::listening;
            state.trial.lowerPostApprovalRequired = phase == local_blind::ProductSessionPhase::ready;
            state.lowerPostGainDb = -18.0;
            component.setState (state);
            component.setPresentationContext (presentation::forEditor (preset.width, preset.height));
            component.setSize (preset.width, preset.height);
            for (int index = 0; index < component.getNumChildComponents(); ++index)
            {
                const auto* child = component.getChildComponent (index);
                if (! child->isVisible()) continue;
                require (! child->getBounds().isEmpty()
                             && component.getLocalBounds().contains (child->getBounds()),
                         "every playback phase remains within the editor");
                if (const auto* action = dynamic_cast<const juce::TextButton*> (child))
                {
                    const auto font = monoFont (presentation::forEditor (preset.width, preset.height),
                                                typography::TextRole::action);
                    const auto required = font.getStringWidthFloat (action->getButtonText());
                    if (required > action->getWidth() - 12)
                        std::cerr << "Blind clipped action: " << preset.width << " "
                                  << action->getButtonText() << " needs=" << required
                                  << " available=" << action->getWidth() - 12 << '\n';
                    require (required <= action->getWidth() - 12,
                             "complete answer labels and explicit attenuation fit without shrinking");
                }
                if (const auto* label = dynamic_cast<const juce::Label*> (child))
                {
                    if (label->getText().isEmpty()) continue;
                    juce::AttributedString text (label->getText());
                    text.setFont (label->getFont());
                    juce::TextLayout layout;
                    const auto bounds = label->getBorderSize().subtractedFrom (label->getLocalBounds());
                    layout.createLayout (text, static_cast<float> (bounds.getWidth()));
                    if (layout.getHeight() > bounds.getHeight() + 1)
                        std::cerr << "Blind clipped label: " << preset.width << " phase="
                                  << static_cast<int> (phase) << " " << label->getComponentID()
                                  << " needs=" << layout.getHeight() << " available="
                                  << bounds.getHeight() << '\n';
                    require (layout.getHeight() <= bounds.getHeight() + 1,
                             "playback instructions remain readable without font compression");
                }
            }
            const auto directory = juce::SystemStats::getEnvironmentVariable (
                "KIRIN_HYPHA_COMPOSITE_PREVIEW_DIR", {});
            if (directory.isNotEmpty())
            {
                juce::Image preview (juce::Image::ARGB, preset.width, preset.height, true);
                juce::Graphics graphics (preview);
                component.paintEntireComponent (graphics, true);
                auto output = juce::File (directory).getChildFile (
                    "local-blind-phase-" + juce::String (static_cast<int> (phase))
                        + "-" + juce::String (preset.width) + ".png").createOutputStream();
                require (output != nullptr && output->setPosition (0) && output->truncate().wasOk()
                             && juce::PNGImageFormat().writeImageToStream (preview, *output),
                         "playback presentation preview is written");
            }
        }
}
}
