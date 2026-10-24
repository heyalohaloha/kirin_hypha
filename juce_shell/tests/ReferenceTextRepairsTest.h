#pragma once
#include "ReferenceADirectionTest.h"
#include "ReferenceHeaderFitTest.h"
#include "ReferenceVersionStopTextTest.h"
#include "../src/HyphaReferenceDisplayText.h"
#include "../src/HyphaReferenceGainText.h"
#include "../src/HyphaReferenceNotices.h"

namespace hypha::tests::reference_text_repairs
{
using reference_guide_contract::require;

inline void sizePanel (reference_ui::Component& panel, int width, int height)
{
    const auto viewport = observatory::displayViewport (width, height);
    require (viewport.width == 900 && viewport.height == 600,
             "every Inspection magnification retains the contracted logical text layout");
    observatory::View shell (observatory::Role::post);
    shell.setSize (viewport.width, viewport.height); shell.setDomain (observatory::Domain::reference);
    shell.setExternalAnalysisBodyActive (true);
    panel.setVisible (true); panel.setPresentationContext (presentation::forEditor (viewport.width, viewport.height));
    panel.setSize (shell.analysisBodyBounds().getWidth(), shell.analysisBodyBounds().getHeight());
}

inline std::vector<observatory::EditorSize> inspectionSizes()
{
    std::vector<observatory::EditorSize> sizes {{900, 600}};
    for (const float dpi : {1.0f, 1.25f, 1.5f, 2.0f})
        for (int index = 0; index < 16; ++index)
        {
            const auto size = observatory::magnifiedStep (dpi, index);
            if (size.width == 0) break;
            if (std::none_of (sizes.begin(), sizes.end(), [&] (const auto& item) { return item.width == size.width; }))
                sizes.push_back (size);
        }
    return sizes;
}

inline reference_ui::State bState()
{
    auto state = a_direction::checkState();
    state.comparisonSlot = 3; state.blindLargeScreen = true;
    state.songSets = {{"set", "Reference set"}}; state.songSetId = "set";
    state.songs = {{"one/one/one", "First source"}, {"two/two/two", "Second source"}};
    state.songId = state.songs.front().id;
    for (int n = 0; n < 2; ++n)
    {
        reference_ui::SongFact fact;
        fact.prepared = true; fact.lufsI = -22;
        fact.centersHz = state.aKirin->centersHz; fact.medianDb = state.aKirin->medianDb;
        fact.part = reference_audition::CuePart::chorus;
        fact.partStartSeconds = 62; fact.partEndSeconds = 84;
        state.songFacts.push_back (fact);
    }
    state.aWindowLoudness = -16.1;
    state.referenceReady = state.referenceArmable = true;
    state.referenceStep = reference_ui::SourceStep::ready;
    return state;
}

inline void requireWhole (const juce::StringArray& drawn, const juce::String& text)
{
    const auto expected = text_style::shownText (text);
    require (drawn.contains (expected), "complete visible text: " + expected + "\n  drew: " + drawn.joinIntoString (" | "));
}

inline void verifyApprovalPreview()
{
    auto state = bState();
    state.action = {reference_ui::ActionKind::lowerAAndPlay, {3, -5.5, "bound-source", 7, 9}};
    reference_ui::Component panel; sizePanel (panel, 900, 600); panel.setState (state);
    auto* list = dynamic_cast<reference_ui::SongList*> (panel.findChildWithID ("reference-song-list"));
    require (list && list->rows().size() >= 2, "the actual B list has both independent sources");
    require (std::abs (list->rows()[0].gainDb - 5.5) < 1.0e-9,
             "the selected MATCH remains bound to the offered A window");
    requireWhole (a_direction::drawnTexts (panel), "A " + reference_ui::gainText (-5.5));
    require (std::abs (list->rows()[1].gainDb - 5.9) < 1.0e-9, "other sources retain their current MATCH preview");
    state.heldAttenuationDb = -2; panel.setState (state);
    require (std::abs (list->rows()[0].gainDb - 3.5) < 1.0e-9, "MATCH retains the existing lowered-A display basis");
    state.action.offer.slot = 2; panel.setState (state);
    require (! std::isfinite (list->rows()[0].lowerAOfferDb), "a C offer never labels the B source");
    require (std::abs (list->rows()[0].gainDb - 3.9) < 1.0e-9, "an offer for C never changes B's preview");
    state.bSelected = true; state.audibleComparisonSlot = 3; state.appliedGainDb = 1.2;
    state.action.offer.slot = 3; panel.setState (state);
    require (std::abs (list->rows()[0].gainDb + 0.8) < 1.0e-9, "a playing source still shows its actual applied gain");
}

inline void verifyLegends (bool check)
{
    if (check) verifyRegularHeaderSizes();
    for (auto language : {i18n::Language::english, i18n::Language::japanese})
    {
        i18n::ScopedLanguage scoped (language);
        for (const auto& preset : inspectionSizes())
        {
            reference_ui::Component panel; sizePanel (panel, preset.width, preset.height);
            auto state = check ? a_direction::checkState() : bState();
            state.blindLargeScreen = true;
            if (check)
            {
                state.cuePart = reference_audition::CuePart::chorus;
                state.viewBindings = {"spectrum_full", "spectrum_low"};
                state.aWindowLoudness = state.cueLoudness = std::numeric_limits<double>::quiet_NaN();
            }
            panel.setState (state);
            const auto drawn = a_direction::drawnTexts (panel);
            if (check)
            {
                verifyHeaderLayouts (panel, state);
                requireWhole (drawn, "LOW FREQUENCY");
                for (const auto& text : {"A LAST 30 S", "C CHORUS", "LEVEL NOT MATCHED"}) requireWhole (drawn, text);
            }
            else
            {
                requireWhole (drawn, "REFERENCE RANGE");
                requireWhole (drawn, "B CHORUS 1:02-1:24");
                state.songFacts.front().part = reference_audition::CuePart::loudest;
                panel.setState (state);
                const auto longer = a_direction::drawnTexts (panel);
                requireWhole (longer, "B LOUDEST 30 S 1:02-1:24");
                requireWhole (longer, "REFERENCE RANGE");
            }
        }
    }
}

inline void verifyCMatchFit()
{
    for (auto language : {i18n::Language::english, i18n::Language::japanese})
    {
        i18n::ScopedLanguage scoped (language);
        for (const auto& preset : inspectionSizes())
        {
            reference_ui::Component panel; sizePanel (panel, preset.width, preset.height);
            auto state = a_direction::checkState(); state.blindLargeScreen = true;
            state.cueLoudness = -22; state.aWindowLoudness = -16.3;
            state.aWindowBlocks = state.aWindowNeededBlocks = 300;
            panel.setState (state);
            requireWhole (a_direction::drawnTexts (panel), reference_ui::matchReadout (state));
        }
    }
}

inline void verifyOnsetMeaning()
{
    for (auto language : {i18n::Language::english, i18n::Language::japanese})
    {
        i18n::ScopedLanguage scoped (language);
        auto state = a_direction::checkState(); state.blindLargeScreen = true;
        state.viewBindings = {"transient"};
        auto timeline = std::make_shared<reference_audition::VisualTimeline>();
        timeline->binding.matchWindowBlocks = 300; timeline->aTickChannels = 2;
        auto ticks = std::make_shared<std::vector<KirinReferenceVisualBin>>();
        for (int i = 0; i < 300; ++i) ticks->push_back (a_direction::smallBin (i));
        timeline->aTicks = ticks; state.visualTimeline = timeline;
        auto measured = a_direction::largeMeasurement();
        for (size_t i = 0; i < measured->transient->onsetStrengthQ15.size(); ++i)
            measured->transient->onsetStrengthQ15[i] = i % 4 == 0 ? 23920 : 0;
        state.cueMeasurement = measured;
        reference_ui::Component panel; sizePanel (panel, 900, 600); panel.setState (state);
        const auto drawn = a_direction::drawnTexts (panel);
        const auto title = drawn.indexOf (text_style::shownText ("ONSET (RISE PER HOP)"));
        require (title >= 0, "the onset fixture really reaches the rendered metric");
        const auto median = text_style::shownText ("MEDIAN");
        bool labelled = false;
        for (int i = title + 1; i < drawn.size(); ++i) labelled |= drawn[i] == median;
        require (labelled, "the onset number names its median while its bar shows a range");
    }
}

inline void verifyCueLabels()
{
    auto state = a_direction::checkState();
    state.cuePart = reference_audition::CuePart::chorus;
    state.cueId = "automatic-cue"; state.cueLabel = "Chorus candidate";
    state.cues = {{state.cueId, state.cueLabel}, {"authored-cue", "Bridge focus"}};
    state.checkId = "authored-check/song";
    state.checkLabel = juce::String::fromUTF8 (u8"低域の確認");
    state.checks = {{state.checkId, state.checkLabel}};
    reference_ui::prepareDisplayNames (state, i18n::Language::japanese);
    require (state.cueLabel == i18n::translate ("C CHORUS", i18n::Language::japanese)
        && state.cues.front().label == state.cueLabel, "an automatic chorus uses the existing Japanese C part label");
    require (state.cues.back().label == "Bridge focus" && state.checkLabel == juce::String::fromUTF8 (u8"低域の確認"),
             "authored Cue names and Japanese Check names supplied by Kirin OS stay intact");
}
inline void exportSnapshots (const juce::File& directory)
{
    require (directory.createDirectory().wasOk(), "create the requested layout evidence directory");
    exportHeaderSnapshots (directory.getChildFile ("c-headers"), inspectionSizes());
    verifyVersionStopPage (directory.getChildFile ("v-stops"));
    for (auto language : {i18n::Language::english, i18n::Language::japanese})
    {
        i18n::ScopedLanguage scoped (language);
        for (const auto& preset : inspectionSizes())
        {
            if (preset.width != 900) continue;
            for (int scene = 0; scene < 3; ++scene)
            {
                auto state = scene == 0 ? bState() : a_direction::checkState();
                state.blindLargeScreen = true;
                if (scene == 0) state.songFacts.front().part = reference_audition::CuePart::loudest;
                if (scene == 1)
                {
                    state.viewBindings = {"spectrum_full", "spectrum_low"};
                    state.cuePart = reference_audition::CuePart::chorus;
                    state.cueLoudness = state.aWindowLoudness = std::numeric_limits<double>::quiet_NaN();
                }
                if (scene == 2) { state.cueLoudness = -22; state.aWindowLoudness = -16.3; }
                reference_ui::Component panel; sizePanel (panel, preset.width, preset.height); panel.setState (state);
                juce::Image image (juce::Image::ARGB, panel.getWidth(), panel.getHeight(), true);
                juce::Graphics g (image); panel.paintEntireComponent (g, true);
                auto file = directory.getChildFile ((language == i18n::Language::japanese ? "ja-" : "en-")
                    + juce::String (preset.width) + "-" + juce::String (scene) + ".png");
                auto output = file.createOutputStream();
                require (output && juce::PNGImageFormat().writeImageToStream (image, *output), "write native text layout evidence");
            }
        }
    }
}

}
