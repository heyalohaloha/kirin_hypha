#pragma once

#include "../src/HyphaBlindTrialLayout.h"
#include "../src/HyphaLanguage.h"
#include "BlindPreferenceComponentTest.h"
#include "EditorReachabilityCheck.h"
#include "TestTemporaryDirectory.h"

namespace hypha::tests
{
template <typename Require>
void verifyBlindTrialLayout (Require check)
{
    using live_compare::BlindStage;
    using namespace blind_ui::trial_layout;
    enum class Notice { none, repeating, changed, unmeasured, notRemeasured, repeatingAndUnmeasured };
    const auto require = [&] (bool ok, const juce::String& why) { check (ok, why.toRawUTF8()); };
    const auto preferenceFile = temporaryParent().getNonexistentChildFile ("blind-layout-prefs", ".txt");
    const auto label = [] (juce::Component& view, const char* suffix)
    {
        return dynamic_cast<const juce::Label*> (view.findChildWithID (juce::String ("live-blind-") + suffix));
    };

    for (const auto language : { i18n::Language::english, i18n::Language::japanese })
    for (const auto width : { 300, 303, 330, 375, 450, 600, 900 })
    for (const auto scale : { 1.0f, 1.5f })
    for (const bool candidate : { false, true })
    for (const auto rounds : { 1, 5, 10 })
    for (const bool guess : { false, true })
    for (const bool voting : { false, true })
    for (const bool lowered : { false, true })
    for (const auto notice : { Notice::none, Notice::repeating, Notice::changed, Notice::unmeasured,
                              Notice::notRemeasured, Notice::repeatingAndUnmeasured })
    {
        // 450% is the real 900x600 component transformed by the 1.5 host display factor.
        // GUESS and a 5/10 preference trial are separate modes, never simultaneous in product.
        if ((scale == 1.5f && width != 900) || (guess && rounds != 1) || (voting && rounds == 1 && !guess))
            continue;
        const i18n::ScopedLanguage shown (language);
        blind_preference_component_test::Harness h (rounds, candidate, preferenceFile);
        h.view.setSize (width, width * 2 / 3);
        h.view.setGuessing (guess);
        h.heard (voting ? 3 : 1);
        // Also hit the locked post-vote rounds row on a second ballot: the selected round
        // count did not disappear; its rectangle is now occupied by round progress.
        if (voting && rounds > 1) { h.choose (1); h.heard (3); }
        h.status.contentUnmeasured = notice == Notice::unmeasured || notice == Notice::repeatingAndUnmeasured;
        h.status.contentUnapplied = notice == Notice::changed;
        h.status.contentNotRemeasured = notice == Notice::notRemeasured;
        const bool repeating = notice == Notice::repeating || notice == Notice::repeatingAndUnmeasured;
        h.view.setState (h.status, true, lowered ? 0.616595f : 1.0f, repeating);

        const auto context = juce::String (language == i18n::Language::japanese ? "JA" : "EN")
            + " " + juce::String (width) + "@" + juce::String (scale) + " rounds=" + juce::String (rounds)
            + " candidate=" + juce::String (int (candidate)) + " guess=" + juce::String (int (guess))
            + " voting=" + juce::String (int (voting)) + " lowered=" + juce::String (int (lowered))
            + " notice=" + juce::String (int (notice));
        const auto must = [&] (bool ok, const juce::String& why) { require (ok, context + ": " + why); };
        const auto explanation = label (h.view, "text-3");
        const auto note = label (h.view, "notice");
        const auto whole = [&] (const juce::Label* item, const char* name)
        {
            must (item && item->isVisible() && item->getFont().getHeight() >= 12.0f, name);
            if (!item) return;
            const auto body = item->getBorderSize().subtractedFrom (item->getLocalBounds());
            must (text_style::shownWrappedHeight (item->getText(), item->getFont(), body.getWidth())
                      <= body.getHeight(), juce::String (name) + " full text " + text_style::shownText (item->getText()));
        };
        whole (explanation, "END explanation visible at 12px or larger");
        must (explanation && explanation->getText().contains (lowered ? "4.2 dB" : "END restores normal level"),
              "END return level retained");
        if (notice != Notice::none)
        {
            whole (note, "notice present in a separate measured row");
            must (explanation && note && explanation->getBottom() <= note->getY(), "read END before the notice");
            if (repeating) must (note && note->getText().contains ("Repeated audio: checked per block"), "periodic notice survives");
            if (h.status.contentUnmeasured)
                must (note && note->getText().contains (live_compare_ui::unmeasuredNotice())
                           && note->getText().contains (live_compare_ui::chainDelayNotice()), "unmeasured notice and advice survive");
            if (h.status.contentUnapplied) must (note && note->getText().contains (live_compare_ui::unappliedNotice()), "delay change survives");
            if (h.status.contentNotRemeasured)
                must (note && note->getText().contains (live_compare_ui::notRemeasuredNotice()), "unremeasured notice survives");
        }
        else must (note && !note->isVisible(), "ordinary trial does not show a blank notice");

        int actionHeight = 0;
        for (const auto* child : h.view.getChildren())
        {
            if (!child->isVisible()) continue;
            const auto id = child->getComponentID();
            must (!child->getBounds().isEmpty() && h.view.getLocalBounds().contains (child->getBounds()), "contained " + id);
            for (const auto* other : h.view.getChildren())
                if (other != child && other->isVisible())
                    must (!other->getBounds().intersects (child->getBounds()), "no overlap " + id + " " + other->getComponentID());
            if (dynamic_cast<const juce::Button*> (child))
            {
                if (id.contains ("-source-")) must (child->getHeight() >= sourceMinimum, "SOURCE floor");
                else
                {
                    if (!actionHeight) actionHeight = child->getHeight();
                    must (child->getHeight() == actionHeight, "common control height " + id);
                    must (child->getHeight() >= minimumActionHeight, "control target floor " + id);
                }
            }
        }
        juce::Component surface;
        surface.addAndMakeVisible (h.view);
        h.view.setTransform (juce::AffineTransform::scale (scale));
        surface.setSize (juce::roundToInt (width * scale), juce::roundToInt (width * 2 / 3 * scale));
        must (h.view.getBoundsInParent() == surface.getLocalBounds(), "exact transformed component bounds");
        must (editor_reachability::check (surface, [&] (const juce::String& why) { must (false, why); }) >= 3,
              "SOURCE and END hit by mouse and keyboard at actual scale");
    }
}
}
