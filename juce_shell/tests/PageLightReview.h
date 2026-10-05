#pragma once

#include "CompactReviewShowcase.h"
#include "FreqHistoryReview.h"
#include "ReferenceGuideStates.h"
#include "PsbLightContract.h"

#include <cstdlib>

// Look review of the editor's light (2026-09-29; stage 2, 2026-10-01): the VU, then every page at
// the five editor sizes, where each page's main window stands in the VU chassis's bronze frame lit
// by the editor's one key light, and cards and lanes stay quiet. Written only when
// KIRIN_HYPHA_LIGHTING_REVIEW_DIR names a directory.
namespace hypha::tests
{
inline bool writeLightingReview()
{
    const auto* path = std::getenv ("KIRIN_HYPHA_LIGHTING_REVIEW_DIR");
    if (path == nullptr)
        return true;
    const juce::File directory { path };
    if (! directory.createDirectory())
        return false;
    using namespace compact_review;
    bool written = true;
    material_cache::Lifetime editorMaterial;
    for (const auto& preset : observatory::sizePresets)
    {
        const auto width = preset.width, height = preset.height;
        const auto name = [&directory, width] (const char* page) {
            return directory.getChildFile (juce::String (width) + "_" + page + ".png"); };
        observatory::View shell (observatory::Role::post);
        shell.setSize (width, height);
        shell.setObservatoryFrame (frame(), true);
        shell.setWatchDisplay (watch(), true);
        shell.setHistory (history());
        shell.setConnection ("PAIR DRUM", COL_LED_BLUE, observatory::ConnectionState::paired);
        shell.setMeterContext (meter_context::MeterContext::trackStem);
        shell.setDomain (observatory::Domain::level);
        written = written && freq_showcase::writePng (name ("level"), renderShell (shell));
        shell.setMeterContext (meter_context::MeterContext::twoMix);
        written = written && freq_showcase::writePng (name ("level_2mix"), renderShell (shell));
        shell.setMeterContext (meter_context::MeterContext::trackStem);
        shell.setManualHybridVuVisible (true);
        written = written && freq_showcase::writePng (name ("vu"), renderShell (shell));
        shell.setManualHybridVuVisible (false);
        shell.setDomain (observatory::Domain::time);
        written = written && freq_showcase::writePng (name ("time_history"), renderShell (shell));
        {
            PerceptualComponent sharp;
            sharp.setSignalActive (true);
            sharp.setBatch (sharpness());
            sharp.presentationTickAt (60'000.0);
            written = written && freq_showcase::writePng (name ("time_sharp"),
                compose (shell, sharp, analysis_navigation::Page::perceptual));
            AbsoluteComponent timeline;
            timeline.setSignalActive (true);
            timeline.setBatchAt (live(), 60'000.0);
            written = written && freq_showcase::writePng (name ("time_live"),
                compose (shell, timeline, analysis_navigation::Page::absolute));
            auto attack = drum();
            attack->presentationTickAt (60'000.0);
            written = written && freq_showcase::writePng (name ("time_drum"),
                compose (shell, *attack, analysis_navigation::Page::attack));
        }
        shell.setDomain (observatory::Domain::frequency);
        {
            SpectrumComponent absolute;
            absolute.setAbsoluteObservation (true);
            absolute.setSignalActive (true);
            for (int index = 0; index < 240; ++index)
                absolute.setSnapshot (freq_history_review::frame (index, freq_history_review::mixDbfs));
            written = written && freq_showcase::writePng (name ("freq"),
                compose (shell, absolute, analysis_navigation::Page::spectrum));
            SpectrumComponent psb;
            psb.setSignalActive (true);
            psb.setAbsoluteObservation (true);
            psb.setPresentationContext (presentation::forEditor (375, 250));
            psb.setSize (340, 180);
            psb_light_contract::selectPsb (psb);
            psb.setPsbSnapshot (psb_light_contract::observation());
            written = written && freq_showcase::writePng (name ("freq_psb"),
                compose (shell, psb, analysis_navigation::Page::spectrum));
        }
        shell.setDomain (observatory::Domain::space);
        written = written && freq_showcase::writePng (name ("space"), renderShell (shell));
        shell.setDomain (observatory::Domain::reference);
        {
            reference_ui::Component reference;
            reference.setPresentationContext (presentation::forEditor (width, height));
            for (const auto& example : reference_review::cases())
                if (juce::String (example.name) == "b_audible_c_view")
                {
                    reference.setState (example.state);
                    written = written && freq_showcase::writePng (name ("ref"),
                        compose (shell, reference, analysis_navigation::Page::meters));
                    auto version = example.state;
                    version.comparisonSlot = 1;
                    reference.setState (version);
                    written = written && freq_showcase::writePng (name ("ref_version"),
                        compose (shell, reference, analysis_navigation::Page::meters));
                }
        }
    }
    return written;
}
}
