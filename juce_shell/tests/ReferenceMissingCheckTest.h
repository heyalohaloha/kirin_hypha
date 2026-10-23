#pragma once
#include "ReferenceControlLookup.h"
#include "ReferenceGuideContractTest.h"
#include "../src/HyphaReferenceRuntimeStatus.h"
#include "../src/HyphaReferenceStages.h"

namespace hypha::tests
{
inline void verifyMissingCheckNavigation()
{
    using reference_guide_contract::require;
    for (const auto& size : observatory::sizePresets)
    {
        if (size.width < 900) continue;
        observatory::View shell (observatory::Role::post);
        shell.setSize (size.width, size.height); shell.setDomain (observatory::Domain::reference);
        shell.setExternalAnalysisBodyActive (true);
        reference_ui::Component panel;
        panel.setVisible (true);
        panel.setPresentationContext (presentation::forEditor (size.width, size.height));
        panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
        auto state = reference_guide_contract::named ("ready");
        state.separateComparisons = state.blindLargeScreen = true;
        state.presets = { {"set", "CHECK SET"}, {"other", "OTHER SET"} }; state.presetId = "set";
        state.checks = { {"low/song", "Low end"}, {"dynamics/song", "Dynamics"} };
        state.checkId = "low/song";
        for (int page : {1, 3})
            for (auto step : {reference_ui::SourceStep::ready, reference_ui::SourceStep::preparing,
                              reference_ui::SourceStep::sourceUnavailable})
            {
                state.comparisonSlot = page; state.checkStep = step;
                state.checkReady = step == reference_ui::SourceStep::ready;
                state.checkArmable = true; state.transportPlaying = true;
                state.aAvailable = state.auditionBuffered = true;
                panel.setState (state);
                panel.onSelectVisualSlot = [&] (int slot) { state.comparisonSlot = slot; panel.setState (state); };
                panel.onSelectC = [] {}; // The output owner may refuse. Display still has to open.
                auto* c = dynamic_cast<juce::TextButton*> (panel.findChildWithID ("reference-c"));
                require (c && c->isEnabled() && panel.getComponentAt (c->getBounds().getCentre()) == c,
                         "C remains a reachable page entry even when its source is missing");
                c->onClick();
                require (state.comparisonSlot == 2, "C opens its page before a refused or queued audition");
                auto* preset = panel.findChildWithID ("reference-preset");
                require (preset && preset->isVisible() && !preset->getBounds().isEmpty(),
                         "the CHECK SET selector stays reachable after returning from B or V");
            }
    }
}

inline void verifyMissingCheckText()
{
    using reference_guide_contract::require;
    auto state = reference_guide_contract::named ("ready");
    state.checkReady = false; state.checkStep = reference_ui::SourceStep::sourceUnavailable;
    const auto openInstruction = juce::String (reference_ui::stageOf (reference_ui::SourceStep::openKirinOs).brief);
    require (!reference_ui::unavailableText (state, false).containsIgnoreCase (openInstruction),
             "a missing source must not claim Kirin OS needs opening");
    reference_audition::Snapshot runtime;
    runtime.state = reference_audition::RuntimeState::waiting;
    runtime.rejectionCode = "reference_source_unavailable";
    require (!reference_ui::runtimeStatus (runtime, state).status.containsIgnoreCase (openInstruction),
             "the missing-source status must name its own repair rather than app launch");
}
}
