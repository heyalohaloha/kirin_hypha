#pragma once

#include "ReferenceControlLookup.h"
#include "ReferenceGuideContractTest.h"
#include "../src/HyphaReferencePendingUI.h"
#include "../src/HyphaReferenceVisualLayout.h"

namespace hypha::tests
{
inline void verifyReferenceVisualNavigation()
{
    using namespace reference_guide_contract;
    for (const int count : { 1, 2, 3 }) for (const auto* layout : { "main", "equal" })
        for (const auto size : { juce::Point<float> (560, 106), juce::Point<float> (860, 280) })
        {
            const juce::Rectangle<float> area (0, 0, size.x, size.y);
            const auto cells = reference_ui::referenceVisualCells (area, count, layout);
            for (int index = 0; index < count; ++index)
            {
                const auto cell = cells[static_cast<size_t> (index)];
                require (area.contains (cell) && cell.getHeight() >= 100,
                    "configured Reference charts retain room for actual curves at full sizes");
                for (int other = 0; other < index; ++other)
                    require (!cell.intersects (cells[static_cast<size_t> (other)]), "configured charts never overlap");
            }
        }
    for (const auto& size : observatory::sizePresets)
    {
        observatory::View shell (observatory::Role::post);
        shell.setSize (size.width, size.height);
        shell.setDomain (observatory::Domain::reference);
        shell.setExternalAnalysisBodyActive (true);
        reference_ui::Component panel;
        panel.setVisible (true);
        panel.setPresentationContext (presentation::forEditor (size.width, size.height));
        panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
        for (const auto* name : { "queued_b_rate_c_view", "queued_c_rate_b_view" })
        {
            panel.setState (named (name));
            auto* action = dynamic_cast<juce::TextButton*> (findReferenceControl (panel, "reference-action"));
            require (action && action->isVisible() && !action->getBounds().isEmpty()
                && panel.getComponentAt (boundsWithin (panel, *action).getCentre()) == action,
                "pending approval has a directly reachable action in the other visual pane at every size");
            int approvals = 0; panel.onAction = [&] { ++approvals; };
            action->onClick(); require (approvals == 1, "one click reaches the pending approval");
            panel.onAction = {};
        }
        auto state = named ("ready");
        state.comparisonSlot = 1;
        state.checks = { { "low/ref-a", "Low end  /  The actual C song" } };
        state.checkId = "low/ref-a";
        panel.setState (state);
        // 2026-10-04：VIEW（音を変えずに見せる比較だけを替える）の行は無い。見せる比較は開いている役の画面が決める。
        require (panel.findChildWithID ("reference-visual-slot") == nullptr, "no VIEW row takes chart height");
        auto* preset = panel.findChildWithID ("reference-preset");
        auto* cue = panel.findChildWithID ("reference-cue");
        auto* singleCheck = panel.findChildWithID ("reference-selection-value-2");
        require (preset && cue && singleCheck, "visual navigation controls exist");
        // H13: 300% の V の画面は VERSION・CHECK SET（C と共用）・タブ。C の曲と Cue は C の画面で選ぶ。
        for (const auto* control : { preset, cue, singleCheck })
        {
            const bool shown = size.width < 900 || control == preset;
            require (control->isVisible() == shown && (! shown || (!control->getBounds().isEmpty()
                && panel.getLocalBounds().contains (control->getBounds()))),
                "C Preset, Cue and source stay reachable from A/B at " + juce::String (size.width));
        }
        require (! cue->isVisible() || !preset->getBounds().intersects (cue->getBounds()), "navigation has distinct hit targets");
        int audioChanges = 0, displayed = 0;
        panel.onSelectA = panel.onSelectB = panel.onSelectC = [&] { ++audioChanges; };
        panel.onSelectVisualSlot = [&] (int slot) { displayed = slot; };
        state.comparisonSlot = 2;
        state.bSelected = true;
        state.audibleComparisonSlot = 1;
        panel.setState (state);
        auto* b = dynamic_cast<juce::TextButton*> (panel.findChildWithID ("reference-b"));
        auto* c = dynamic_cast<juce::TextButton*> (panel.findChildWithID ("reference-c"));
        require (b && c && b->getToggleState() && !c->getToggleState(),
            "B remains the audible indicator while C is displayed");
        require (!b->getBounds().intersects (c->getBounds())
            && panel.getComponentAt (b->getBounds().getCentre()) == b
            && panel.getComponentAt (c->getBounds().getCentre()) == c,
            "A/B/C keep separate unobstructed primary hit targets");
        state.checkReady = false;
        state.checkStep = reference_ui::SourceStep::approveSampleRate;
        state.comparisonSlot = 1;
        panel.setState (state);
        c->onClick();
        require (displayed == 2 && audioChanges == 0,
            "an unready C opens its explanation and controls without selecting audio");
        state.transportPlaying = false;
        state.versionArmable = state.checkArmable = true;
        state.pendingAudition = { 2, reference_audition::PendingAuditionView::Stage::play };
        panel.setState (state);
        c->onClick();
        require (audioChanges == 1 && !c->getToggleState() && c->getButtonText() == "C"
                     && static_cast<bool> (c->getProperties()["waiting"]),
            "stopped C accepts an explicit queue action, marked as waiting without an ellipsis, distinct from audible selection");
        auto* a = dynamic_cast<juce::TextButton*> (panel.findChildWithID ("reference-a"));
        require (a && a->isEnabled(), "A remains reachable to cancel a stopped queue");
        a->onClick();
        require (audioChanges == 2, "A cancellation reaches the control plane");
        state.pendingAudition.stage = reference_audition::PendingAuditionView::Stage::sourceChanged;
        panel.setState (state);
        require (reference_ui::pendingAuditionText (state).startsWith ("C STOPPED")
            && c->getButtonText() == "C", "a cancelled switch cannot appear to be still waiting");
        state.blindPhase = reference_ui::BlindPhase::active;
        panel.setState (state);
        require (!singleCheck->isVisible() && !preset->isVisible()
            && !cue->isVisible(), "Blind conceals the display switch and all source selectors");
    }
    auto stopped = named ("approve_c_rate");
    stopped.aAvailable = false;
    stopped.versionStep = reference_ui::SourceStep::playDaw;
    require (reference_ui::guide (stopped).heading == "Play the song in your DAW",
        "C approval cannot hide that B only needs DAW playback");
    stopped.viewBindings = { "spectrum_full", "spectrum_low" };
    stopped.detailedMeasurement = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
    require (!reference_ui::guide (stopped).shown,
        "OS-prepared A/C display evidence is not replaced by an audio approval guide");
}
}
