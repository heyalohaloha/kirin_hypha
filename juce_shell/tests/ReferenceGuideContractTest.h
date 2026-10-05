#pragma once

#include "../src/HyphaLanguage.h"
#include "../src/HyphaObservatoryView.h"
#include "../src/HyphaReferenceRuntimeView.h"
#include "../src/HyphaTextStyle.h"
#include "ReferenceControlLookup.h"
#include "ReferenceGuideStates.h"

#include <cstdlib>
#include <iostream>

// INV-S41: while neither B nor C can be heard the REFERENCE page says the next step and where A,
// B and C each stand, whole at every size and in both languages; a B or C that cannot be heard yet
// explains itself on hover and on a click instead of doing nothing.
namespace hypha::tests
{
namespace reference_guide_contract
{
inline void require (bool ok, const juce::String& message)
{
    if (ok) return;
    std::cerr << "Reference guide contract failed: " << message << '\n';
    std::exit (EXIT_FAILURE);
}

inline reference_ui::State named (const char* name)
{
    for (const auto& item : reference_review::cases())
        if (juce::String (item.name) == name) return item.state;
    require (false, juce::String ("missing state ") + name);
    return {};
}

inline void verifyGuideStates()
{
    using Step = reference_ui::SourceStep;
    using reference_ui::guide;
    auto state = named ("stopped");
    auto shown = guide (state);
    require (shown.shown && shown.heading == "Play the song in your DAW"
                 && shown.version == Step::chooseVersion && shown.check == Step::playDaw,
             "stopped: play first; C is ready once the DAW plays and B still needs a Version");
    shown = guide (named ("stopped_chosen"));
    require (shown.shown && shown.version == Step::playDaw && shown.check == Step::playDaw,
             "a ready B or C still waits for the DAW, never reads Ready while stopped");
    require (guide (named ("no_library")).heading == "Open Kirin OS"
                 && guide (named ("receiving")).heading == "Receiving from Kirin OS",
             "no library: open Kirin OS, or wait while a running Kirin OS sends it");
    shown = guide (named ("no_version"));
    require (! shown.shown && shown.version == Step::chooseVersion && shown.check == Step::ready,
             "one of B and C audible: the comparison stays and only the other explains itself");
    shown = guide (named ("no_check"));
    require (shown.shown && shown.heading == "Choose a Version for V"
                 && shown.check == Step::enableCheck,
             "neither audible while playing: B's step first, C's step in its row");
    require (! guide (named ("ready")).shown, "B and C audible: no guide");
    for (const auto* name : { "stopped", "no_check", "no_library" })
    {
        auto hidden = named (name);
        hidden.bSelected = true;
        require (! guide (hidden).shown, "an audible B or C is never covered");
        hidden = named (name);
        hidden.blindPhase = reference_ui::BlindPhase::active;
        require (! guide (hidden).shown, "Blind keeps its own screen");
        hidden = named (name);
        hidden.osAccess = os_access::State::unowned;
        require (! guide (hidden).shown, "without Kirin OS the access panel explains");
        hidden = named (name);
        hidden.comparisonSlot = 3;
        require (! guide (hidden).shown, "the B page shows its songs, never V's and C's start guide");
    }
    state = named ("ready");
    state.versionReady = false;
    require (guide (state).version == Step::loadingAudio,
             "a B whose runtime is ready but whose buffer is not yet confirmed names buffering");
    require (reference_ui::unavailableText (named ("no_library"), true) == "V: Open Kirin OS"
                 && reference_ui::unavailableText (named ("receiving"), false)
                        == "C: Waiting for Kirin OS"
                 && reference_ui::unavailableText (named ("stopped"), true) == "V: Choose a Version"
                 && reference_ui::unavailableText (named ("stopped"), false)
                        == "C: Ready when the DAW plays"
                 && reference_ui::unavailableText (named ("loading_audio"), true)
                        == "V: Loading audio here; keep playing",
             "the reasons on hover and after a click name the source and its step");
}

inline void verifySourceStepReasons()
{
    using namespace reference_audition;
    Snapshot comparison;
    auto b = std::make_shared<Snapshot>();
    auto c = std::make_shared<Snapshot>();
    b->libraryReceived = c->libraryReceived = true;
    b->presetId = "preset"; b->checkId = "check"; b->candidateId = "candidate";
    b->state = RuntimeState::ready; b->auditionBuffered = true;
    comparison.versionSelection = b; comparison.checkSelection = c;
    comparison.versions.push_back ({ "preset/check/candidate", "Version", {}, false });
    comparison.selectedVersionId = "preset/check/candidate";
    comparison.comparisonSlot = 2;
    reference_ui::State state = reference_review::playing (reference_review::library());
    c->state = RuntimeState::ready;
    c->auditionBuffered = false;
    reference_ui::runtime_view::setSourceSteps (state, comparison);
    require (state.checkStep == reference_ui::SourceStep::loadingAudio
                 && reference_ui::unavailableText (state, false)
                        == "C: Loading audio here; keep playing",
             "a verified source awaiting its playhead page names buffering, not an unknown failure");
    c->auditionOutsideCue = true;
    reference_ui::runtime_view::setSourceSteps (state, comparison);
    require (state.checkStep == reference_ui::SourceStep::outsideCue
                 && reference_ui::unavailableText (state, false)
                        == "C: Outside Cue; move or choose longer Cue",
             "a playhead outside the selected C Cue must not masquerade as buffering");
    b->state = RuntimeState::waiting;
    b->rejectionCode = "reference_alignment_no_match";
    reference_ui::runtime_view::setSourceSteps (state, comparison);
    require (state.versionStep == reference_ui::SourceStep::noMatchingPassage
                 && reference_ui::unavailableText (state, true)
                        == "V: No verified match here; check Version",
             "a completed non-match must explain the selected Version instead of asking to wait");
}

inline void verifyUnavailableButtons()
{
    reference_ui::Component component;
    component.setPresentationContext (presentation::forEditor (900, 600));
    component.setSize (876, 470);
    juce::String explained;
    bool selectedB = false, selectedC = false;
    component.onExplain = [&] (const juce::String& reason) { explained = reason; };
    component.onSelectB = [&] { selectedB = true; };
    component.onSelectC = [&] { selectedC = true; };
    auto* b = dynamic_cast<juce::TextButton*> (component.findChildWithID ("reference-b"));
    auto* c = dynamic_cast<juce::TextButton*> (component.findChildWithID ("reference-c"));
    require (b != nullptr && c != nullptr, "B and C buttons");
    component.setState (named ("no_version"));
    // 聴ける C は理由でなく、V・B と同じく何を聴くかを言う（300% では下の行の説明にもなる。2026-10-04）。
    require (b->isEnabled() && c->isEnabled() && b->getTooltip() == "V: Choose a Version"
                 && c->getTooltip() == "Audition the Check's song from Kirin OS (C).",
             "an unready B stays clickable and names its step on hover; a ready C says what it plays");
    b->onClick();
    require (explained == "V: Choose a Version" && ! selectedB,
             "a click on an unready B explains instead of selecting");
    c->onClick();
    require (selectedC, "a ready C is selected as before");
    explained.clear();
    component.setState (named ("ready"));
    b->onClick();
    require (selectedB && explained.isEmpty(), "a ready B is selected without a reason");
    auto viewed = named ("stopped");
    viewed.viewBindings = { "spectrum_full" };
    component.setState (viewed);
    require (reference_ui::guide (viewed).shown, "the guide takes the configured views' place");
}

// アクションのボタンは、どの大きさでも枠の中に収まり、文が切れない（承認の量と役まで言う長い文も）。
inline void verifyActionFits()
{
    for (const auto& preset : observatory::sizePresets)
    {
        observatory::View shell (observatory::Role::post);
        shell.setSize (preset.width, preset.height);
        shell.setDomain (observatory::Domain::reference);
        shell.setExternalAnalysisBodyActive (true);
        const auto body = shell.analysisBodyBounds();
        reference_ui::Component component;
        const auto context = presentation::forEditor (preset.width, preset.height);
        component.setPresentationContext (context);
        component.setSize (body.getWidth(), body.getHeight());
        for (const auto* text : { "RETRY PREPARATION", "LOWER A 8.0 DB & PLAY B" })
        {
            auto state = named ("loading_audio");
            state.actionText = text;
            state.action = { reference_ui::ActionKind::retryCandidatePreparation, {} };
            component.setState (state);
            auto* action = dynamic_cast<juce::TextButton*> (findReferenceControl (component, "reference-action"));
            require (action != nullptr && action->isVisible() && action->getBounds().getWidth() > 0
                         && component.getLocalBounds().contains (boundsWithin (component, *action)),
                     "the action remains inside every size");
            const auto font = hypha::labelFont (context, typography::TextRole::action,
                                                typography::Composition::information);
            require (text_style::shownWidth (font, action->getButtonText())
                         <= static_cast<float> (action->getWidth()),
                     "action label is whole at " + juce::String (preset.width)
                         + ": " + action->getButtonText() + " / "
                         + juce::String (text_style::shownWidth (font, action->getButtonText()))
                         + " > " + juce::String (action->getWidth()));
        }
    }
}

// Every guide state, at every size, in both languages: the heading and the reason are whole, and
// the full sizes show all three rows whole.
inline void verifyGuideFits()
{
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    {
        const i18n::ScopedLanguage shown (language);
        for (const auto& preset : observatory::sizePresets)
        {
            observatory::View shell (observatory::Role::post);
            shell.setSize (preset.width, preset.height);
            shell.setDomain (observatory::Domain::reference);
            shell.setExternalAnalysisBodyActive (true);
            const auto body = shell.analysisBodyBounds();
            for (const auto& item : reference_review::cases())
            {
                if (item.access || ! reference_ui::guide (item.state).shown) continue;
                reference_ui::Component component;
                component.setPresentationContext (presentation::forEditor (preset.width, preset.height));
                component.setSize (body.getWidth(), body.getHeight());
                component.setState (item.state);
                juce::Image image (juce::Image::ARGB, body.getWidth(), body.getHeight(), true);
                juce::Graphics graphics (image);
                component.paintEntireComponent (graphics, true);
                const auto& fit = component.guideFit();
                const auto where = juce::String (item.name) + " at " + juce::String (preset.width)
                    + (language == i18n::Language::japanese ? " (Japanese)" : " (English)");
                require (fit.heading, "heading whole: " + where);
                require (fit.detail, "reason whole: " + where);
                require (fit.rowCount == 0 || fit.rows, "rows whole: " + where);
                // 200% keeps B and C when the footer holds a button; the heading says A's step.
                require (preset.density != observatory::Density::inspection || fit.rowCount == 3,
                         "A, B and C at 300%: " + where);
                require (preset.density != observatory::Density::observatory || fit.rowCount >= 2,
                         "B and C at 200%: " + where);
            }
        }
    }
}
}

inline void verifyReferenceGuideContract()
{
    reference_guide_contract::verifyGuideStates();
    reference_guide_contract::verifySourceStepReasons();
    reference_guide_contract::verifyUnavailableButtons();
    reference_guide_contract::verifyActionFits();
    reference_guide_contract::verifyGuideFits();
}
}
