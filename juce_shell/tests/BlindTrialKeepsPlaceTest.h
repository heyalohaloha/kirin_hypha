#pragma once

#include "../src/HyphaBlindResultSharedCapture.h"
#include "BlindPreferenceComponentTest.h"
#include "TestTemporaryDirectory.h"

namespace hypha::tests
{
// A press lands where the eye left it. After an answer, ROUNDS and then the question leave the screen of a
// trial; the control under the pointer must never become END or REVEAL, and the rows that stay must not move.
// The page itself clears its frame line and does not crowd against the top when it has room.
template <typename Require>
void verifyBlindTrialKeepsItsPlaces (Require check)
{
    using blind_preference_component_test::Harness;
    const auto previews = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_BLIND_PLACES_PREVIEW_DIR", {});
    const auto memory = temporaryParent().getNonexistentChildFile ("blind-places-prefs", ".txt");
    const auto save = [&] (juce::Component& view, const juce::String& name)
    {
        if (previews.isEmpty()) return;
        const auto file = juce::File (previews).getChildFile (name + ".png");
        file.deleteFile();
        check (capture::writePng (view.createComponentSnapshot (view.getLocalBounds()), file) == capture::PngSave::saved,
               "blind page preview saved");
    };
    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const int width : { 300, 330, 375, 450, 600, 900 })
    for (const bool candidate : { false, true })
    for (const int rounds : { 1, 5, 10 })
    {
        const i18n::ScopedLanguage shown (language);
        Harness h (rounds, candidate, memory);
        auto& view = h.view;
        view.setSize (width, width * 2 / 3);
        live_compare::GainSnapshot gain;
        gain.coherent = gain.retained = true;
        gain.revision = 2;
        gain.pre = gain.post = 1.0f;
        gain.ceiling = 0.9f;
        gain.identity = { 1, 2, 3, 4, 48000 };
        view.setPreferenceApprovedGains (h.status.generation, gain);
        h.heard (3);
        const auto context = juce::String (language == i18n::Language::japanese ? "JA " : "EN ") + juce::String (width)
            + (candidate ? " A/B" : " PRE/POST") + " rounds=" + juce::String (rounds);
        const auto must = [&] (bool ok, const juce::String& why) { check (ok, (context + ": " + why).toRawUTF8()); };
        const auto at = [&] (const char* id)
        {
            const auto* control = view.findChildWithID (juce::String ("live-blind-") + id);
            return control && control->isVisible() ? control->getBounds() : juce::Rectangle<int>();
        };
        const auto page = juce::String (candidate ? "ab-" : "prepost-") + (language == i18n::Language::japanese ? "ja-" : "en-")
            + juce::String (width) + "-" + juce::String (rounds) + "x-";
        const auto clears = [&] (const juce::String& phase)
        {
            // Wherever the rows leave room, the page stands inside its frame line with room at both ends.
            if (width < 450) return;
            const auto title = at ("text-1"), end = at ("end");
            must (! title.isEmpty() && title.getY() >= blind_ui::trial_layout::frameClearance, phase + " title clears the frame line");
            must (! end.isEmpty() && view.getHeight() - end.getBottom() >= blind_ui::trial_layout::frameClearance
                      && view.getHeight() - end.getBottom() <= 2 * blind_ui::trial_layout::edgeInset,
                  phase + " END stands at the foot of the page");
        };
        save (view, page + "1-first");
        clears ("first");
        {
            // The count is chosen here: the plates keep their places while 1×, 5× and 10× are tried.
            const juce::Rectangle<int> plates[] { at ("rounds-1"), at ("rounds-5"), at ("rounds-10") };
            const char* const ids[] { "rounds-1", "rounds-5", "rounds-10" };
            for (const int other : { 1, 5, 10 })
            {
                Harness tried (other, candidate, memory);
                tried.view.setSize (width, width * 2 / 3);
                tried.view.setPreferenceApprovedGains (tried.status.generation, gain);
                tried.heard (3);
                for (int index = 0; index < 3; ++index)
                {
                    const auto* plate = tried.view.findChildWithID (juce::String ("live-blind-") + ids[index]);
                    must (plate && plate->isVisible() && (width >= 375 ? plate->getBounds() == plates[index]
                              : plate->getBounds().contains (plates[index].getCentre())), "ROUNDS keeps its place for " + juce::String (other));
                }
            }
        }
        if (rounds == 1)
        {
            h.reveal();
            const auto one = at ("source-1"), two = at ("source-2");
            must (! one.isEmpty() && one.getHeight() == two.getHeight() && one.getY() == two.getY(), "both answers stay one pair");
            if (width >= 600)
                must (one.getHeight() >= 80 && std::abs (one.getCentreY() - view.getHeight() / 2) < view.getHeight() / 5,
                      "the answers of one listen are the result, in the middle of the page");
            save (view, page + "4-result");
            continue;
        }
        const char* const pressed[] { "guess-1", "guess-2" };
        const auto source = at ("source-1"), end = at ("end"), reveal = at ("reveal");
        const juce::Rectangle<int> answers[] { at (pressed[0]), at (pressed[1]) };
        const bool roundsInline = ! at ("rounds-10").isEmpty() && at ("rounds-10").getY() >= end.getY();
        const auto keeps = [&] (const juce::String& phase)
        {
            for (const auto& answer : answers)
                must (! answer.isEmpty() && ! answer.intersects (at ("end")) && ! answer.intersects (at ("reveal")),
                      phase + " END and REVEAL never take the place of an answer");
            // The smallest windows fold the title while ROUNDS is shown; a press on SOURCE still lands on SOURCE.
            must (width >= 450 ? at ("source-1") == source : at ("source-1").contains (source.getCentre()),
                  phase + " SOURCE keeps its place");
            must (at ("end").getY() == end.getY() && at ("end").getHeight() == end.getHeight()
                      && at ("reveal").getY() == reveal.getY(), phase + " END and REVEAL keep their row");
            if (! roundsInline) must (at ("end") == end && at ("reveal") == reveal, phase + " END and REVEAL keep their places");
        };
        h.choose (2);
        h.heard (3);
        must (h.accepted == 1, "the first answer is taken");
        for (int index = 0; index < 2; ++index)
            must (at (pressed[index]) == answers[index], "the answers keep their places after the first answer");
        keeps ("second round");
        clears ("second round");
        save (view, page + "2-second");
        while (static_cast<int> (h.accepted) < rounds) { h.choose (1); h.heard (3); }
        must (view.screen().votingComplete, "every answer is taken");
        keeps ("after the last answer");
        save (view, page + "3-answered");
        h.reveal();
        save (view, page + "4-result");
    }
    if (previews.isNotEmpty())
        for (const auto language : { i18n::Language::english, i18n::Language::japanese })
            for (const int rounds : { 1, 10 })
                for (const bool candidate : { false, true })
                {
                    const i18n::ScopedLanguage shown (language);
                    blind_image::Facts facts;
                    facts.candidate = candidate;
                    facts.firstPre = true;
                    facts.selected = 2;
                    if (rounds > 1)
                    {
                        facts.preference.a = 6;
                        facts.preference.b = 4;
                        facts.preference.chance = blind_preference::probability (6, 4);
                    }
                    const auto file = juce::File (previews).getChildFile ("saved-" + juce::String (candidate ? "ab-" : "prepost-")
                        + (language == i18n::Language::japanese ? "ja-" : "en-") + juce::String (rounds) + "x.png");
                    file.deleteFile();
                    check (capture::writePng (blind_image::renderShared (facts), file) == capture::PngSave::saved, "saved image preview");
                }
}
}
