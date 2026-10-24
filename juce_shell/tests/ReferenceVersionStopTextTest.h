#pragma once
#include "ReferenceJapaneseScreenTextTest.h"
#include "ReferenceADirectionTest.h"
#include "../src/HyphaReferencePendingUI.h"

namespace hypha::tests::reference_text_repairs
{
inline void verifyVersionStopPage (const juce::File& images = {})
{
    using reference_guide_contract::require;
    if (images != juce::File {}) require (images.createDirectory().wasOk(), "create V stop image evidence");
    auto state = a_direction::checkState();
    state.comparisonSlot = 1; state.versionReady = false; state.bSelected = false; state.blindLargeScreen = true;
    state.versionStep = reference_ui::SourceStep::aligning;
    state.pendingAudition = {1, reference_audition::PendingAuditionView::Stage::sourceChanged,
                             "reference_alignment_content_changed"};
    for (const auto* code : {"reference_alignment_content_changed", "reference_source_changed",
                             "reference_selection_changed", "reference_source_contract_rejected"})
    {
        state.pendingAudition.reasonCode = code;
        const auto guidance = reference_ui::guide (state);
        require (state.checkReady && guidance.shown,
                 "a stopped V explains its next action even when C is ready");
        for (const auto language : {i18n::Language::english, i18n::Language::japanese})
        {
            i18n::ScopedLanguage scoped (language);
            for (const auto size : observatory::sizePresets)
            {
                observatory::View shell (observatory::Role::post);
                shell.setSize (size.width, size.height); shell.setDomain (observatory::Domain::reference);
                shell.setExternalAnalysisBodyActive (true);
                reference_ui::Component panel;
                panel.setPresentationContext (presentation::forEditor (size.width, size.height));
                shell.addAndMakeVisible (panel); panel.setBounds (shell.analysisBodyBounds());
                panel.setState (state);
                const auto texts = a_direction::drawnTexts (panel).joinIntoString ("").removeCharacters (" \t\r\n");
                const auto detail = text_style::shownText (guidance.detail).removeCharacters (" \t\r\n");
                const auto compact = !observatory::isFullDensity (presentation::forEditor (size.width, size.height).density);
                const auto heading = text_style::shownText (guidance.heading).removeCharacters (" \t\r\n");
                require (texts.contains (heading) && texts.contains (detail) && (!compact || panel.guideFit().detail),
                         "V's reason and next operation are drawn whole: " + juce::String (code)
                            + " at " + juce::String (size.width) + " / " + juce::String (int (language))
                            + ", detail fit " + juce::String (panel.guideFit().detail ? 1 : 0) + ", drew " + texts);
                if (images != juce::File {})
                {
                    juce::Image image (juce::Image::ARGB, size.width, size.height, true);
                    juce::Graphics g (image); shell.paintEntireComponent (g, true);
                    const auto file = images.getChildFile ((language == i18n::Language::japanese ? "ja-" : "en-")
                        + juce::String (size.width) + "x" + juce::String (size.height) + "-" + juce::String (code) + ".png");
                    auto output = file.createOutputStream();
                    require (output && juce::PNGImageFormat().writeImageToStream (image, *output), "write V's visible stop guidance");
                }
            }
        }
    }
    state.comparisonSlot = 2;
    require (!reference_ui::guide (state).shown, "a stopped V never conceals the ready C page");
    state.comparisonSlot = 3;
    require (!reference_ui::guide (state).shown, "a stopped V never conceals the other reference page");
}

inline void verifyVersionStopText()
{
    using reference_guide_contract::require;
    using Stage = reference_audition::PendingAuditionView::Stage;
    reference_ui::State state;
    state.separateComparisons = true; state.comparisonSlot = 1;
    state.pendingAudition = {1, Stage::sourceChanged};
    const auto textFor = [&] (const char* code)
    {
        state.versionRejectionCode = code;
        return reference_ui::pendingAuditionText (state);
    };
    const auto changed = textFor ("reference_source_changed");
    require (changed.contains ("FILE") && changed.contains ("CHANGED") && changed.contains ("KIRIN OS"),
             "a verified file revision names the file and its preparation action");
    const auto position = textFor ("reference_alignment_content_changed");
    require (position != changed && position.contains ("POSITION") && position.contains ("PLAY A") && position.contains ("PRESS V"),
             "unconfirmed correspondence tells the user to play A and retry V, without claiming a changed file");
    state.pendingAudition.reasonCode = "reference_alignment_content_changed";
    state.versionRejectionCode.clear();
    require (reference_ui::pendingAuditionText (state) == position,
             "a later ready snapshot cannot erase the reason captured when V stopped");
    state.pendingAudition.reasonCode.clear();
    const auto removed = textFor ("reference_selection_unavailable");
    require (removed.contains ("VERSION") && removed.contains ("CHOOSE") && removed != changed,
             "a removed selection asks for an available version");
    const auto missing = textFor ("reference_source_unavailable");
    require (missing.contains ("FILE") && missing.contains ("CHECK") && missing != changed,
             "an unavailable file asks the user to check it");
    for (const auto stage : {Stage::sourceChanged, Stage::safetyChanged, Stage::startFailed,
                            Stage::sourceLevelUnavailable, Stage::ceilingExceeded})
        for (const auto* code : {"", "reference_alignment_no_match", "reference_source_changed",
            "reference_selection_unavailable", "reference_source_unavailable", "reference_library_rejected",
            "reference_source_contract_rejected", "reference_source_receipt_rejected", "channel_layout_unsupported",
            "reference_selection_changed", "reference_manifest_rejected", "reference_source_open_failed", "reference_alignment_source_unavailable"})
        {
            state.pendingAudition.stage = stage; state.versionRejectionCode = code;
            const auto text = reference_ui::pendingAuditionText (state);
            require (text.contains (" / ") && !text.endsWith ("SELECT AGAIN"), "every V stop carries a specific next action");
            if (juce::String (code) != "reference_source_changed")
                require (!text.contains ("FILE CHANGED"), "an interruption never invents a file revision");
            const auto translated = i18n::translate (text, i18n::Language::japanese);
            for (const auto& word : screen_text_japanese::englishWords (translated))
                require (screen_text_japanese::keptWords().count (word) != 0,
                         "the complete stop reason and next action have Japanese translations: " + translated);
            const auto guidance = reference_ui::guide (state);
            require (guidance.detail == text.fromFirstOccurrenceOf (" / ", false, false),
                     "the guide and status row give the same reason and next step");
        }
    verifyVersionStopPage();
    for (const auto stage : {Stage::play, Stage::checking, Stage::level})
    {
        state.pendingAudition.stage = stage;
        const auto text = reference_ui::pendingAuditionText (state);
        require (text.contains ("WAIT") && !text.contains ("STOPPED"), "automatic preparation still reads as a wait");
    }
}
}
