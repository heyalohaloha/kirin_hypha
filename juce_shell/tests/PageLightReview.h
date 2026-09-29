#pragma once

#include "CompactReviewShowcase.h"
#include "FreqHistoryReview.h"

#include <array>
#include <cstdlib>

// Look review of the editor's light (2026-09-29): the VU, then LEVEL, TIME HISTORY, DRUM, FREQ and
// SPACE, where only each page's main window is lit at its edge as on the VU chassis, and cards and
// lanes stay quiet. Written only when KIRIN_HYPHA_LIGHTING_REVIEW_DIR names a directory.
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
    {
        material_cache::Lifetime editorMaterial;
        for (const auto size : { std::array<int, 2> { 900, 600 }, std::array<int, 2> { 400, 266 } })
        {
            const auto width = size[0], height = size[1];
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
            shell.setManualHybridVuVisible (true);
            written = written && freq_showcase::writePng (name ("vu"), renderShell (shell));
            shell.setManualHybridVuVisible (false);
            shell.setDomain (observatory::Domain::time);
            written = written && freq_showcase::writePng (name ("time_history"), renderShell (shell));
            {
                auto attack = drum();
                written = written && freq_showcase::writePng (name ("drum"),
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
            }
            shell.setDomain (observatory::Domain::space);
            written = written && freq_showcase::writePng (name ("space"), renderShell (shell));
        }
    }
    return written;
}
}
