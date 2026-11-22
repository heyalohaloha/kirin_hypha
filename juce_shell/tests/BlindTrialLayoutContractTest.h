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
    for (const int votePhase : { 0, 1, 2, 3 })
    for (const bool lowered : { false, true })
    for (const auto notice : { Notice::none, Notice::repeating, Notice::changed, Notice::unmeasured,
                              Notice::notRemeasured, Notice::repeatingAndUnmeasured })
    {
        // 450% is the real 900x600 component transformed by the 1.5 host display factor.
        // GUESS and a 5/10 preference trial are separate modes, never simultaneous in product.
        const bool finalVote = votePhase == 3, locked = votePhase >= 2;
        if ((scale == 1.5f && width != 900) || (guess && rounds != 1)
            || (votePhase != 0 && rounds == 1 && !guess) || (locked && rounds == 1))
            continue;
        const i18n::ScopedLanguage shown (language);
        blind_preference_component_test::Harness h (rounds, candidate, preferenceFile);
        h.view.setSize (width, width * 2 / 3);
        h.view.setGuessing (guess);
        h.heard (votePhase != 0 ? 3 : 1);
        // Walk the real state machine before the first vote, as votes lock the count,
        // and through every answer before the separate REVEAL action.
        if (locked)
        {
            const auto votes = finalVote ? rounds : 1;
            for (int vote = 0; vote < votes; ++vote) { h.heard (3); h.choose (1); }
            if (!finalVote) h.heard (3);
        }
        h.status.contentUnmeasured = notice == Notice::unmeasured || notice == Notice::repeatingAndUnmeasured;
        h.status.contentUnapplied = notice == Notice::changed;
        h.status.contentNotRemeasured = notice == Notice::notRemeasured;
        const bool repeating = notice == Notice::repeating || notice == Notice::repeatingAndUnmeasured;
        h.view.setState (h.status, true, lowered ? 0.616595f : 1.0f, repeating);

        const auto context = juce::String (language == i18n::Language::japanese ? "JA" : "EN")
            + " " + juce::String (width) + "@" + juce::String (scale) + " rounds=" + juce::String (rounds)
            + " candidate=" + juce::String (int (candidate)) + " guess=" + juce::String (int (guess))
            + " votePhase=" + juce::String (votePhase) + " lowered=" + juce::String (int (lowered))
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
        auto* more = dynamic_cast<juce::Button*> (h.view.findChildWithID ("live-blind-more-notices"));
        const auto hasNotice = [&] (const juce::String& raw)
        {
            const bool inlineText = note && note->isVisible() && note->getText().contains (raw);
            const auto hiddenText = text_style::shownText (raw);
            return inlineText || (more && more->isVisible() && more->getTooltip().contains (hiddenText)
                && more->getDescription().contains (hiddenText));
        };
        if (notice != Notice::none)
        {
            if (note && note->isVisible())
            {
                whole (note, "visible notice fits in a separate measured row");
                must (explanation && explanation->getBottom() <= note->getY(), "read END before the notice");
            }
            if (repeating) must (hasNotice ("Repeated audio: checked per block"), "full periodic notice is readable");
            if (h.status.contentUnmeasured)
                must (hasNotice (juce::String (live_compare_ui::unmeasuredNotice()) + " / " + live_compare_ui::chainDelayNotice()),
                      "full unmeasured notice and advice are readable");
            if (h.status.contentUnapplied) must (hasNotice (live_compare_ui::unappliedNotice()), "full delay change is readable");
            if (h.status.contentNotRemeasured)
                must (hasNotice (live_compare_ui::notRemeasuredNotice()), "full unremeasured notice is readable");
            if (more && more->isVisible())
            {
                const auto* accessibility = more->getAccessibilityHandler();
                must (note && note->isVisible() && more->getTooltip().isNotEmpty()
                    && accessibility && accessibility->getDescription() == more->getTooltip(),
                    "overflow retains the highest-priority full notice, and reads out all hidden text");
                must (more->getButtonText().startsWithChar ('+') && more->getWantsKeyboardFocus(),
                      "overflow is a +N keyboard control");
            }
        }
        else must (note && !note->isVisible() && more && !more->isVisible(), "ordinary trial has no notice badge");

        if (h.view.screen().roundsEnabled)
        {
            const auto* count = label (h.view, "rounds-label");
            must (count && count->isVisible(), "changeable count remains visible");
            if (count)
            {
                const auto body = count->getBorderSize().subtractedFrom (count->getLocalBounds());
                must (text_style::shownWidth (count->getFont(), count->getText()) <= body.getWidth(),
                    "ROUNDS fits on the header even at minimum width");
            }
        }
        if (locked)
        {
            const auto* voteQuestion = label (h.view, "guess-question");
            const auto* progress = label (h.view, "guess-outcome");
            const auto* count = label (h.view, "rounds-label");
            must (count && !count->isVisible(), "locked rounds never use a second row");
            if (finalVote)
            {
                const auto* instruction = label (h.view, "text-2");
                must (h.accepted == static_cast<unsigned> (rounds) && h.view.screen().votingComplete
                    && h.view.screen().revealEnabled && voteQuestion && !voteQuestion->isVisible()
                    && progress && !progress->isVisible(), "after the last vote only the pending REVEAL action remains");
                must (instruction && instruction->isVisible() && instruction->getFont().getHeight() >= 12.0f
                    && instruction->getText() == "All rounds answered; reveal when ready", "mandatory final instruction replaces vote");
                if (instruction)
                {
                    const auto body = instruction->getBorderSize().subtractedFrom (instruction->getLocalBounds());
                    must (text_style::shownWrappedHeight (instruction->getText(), instruction->getFont(), body.getWidth())
                        <= body.getHeight(), "mandatory final instruction fully fits");
                }
            }
            else
            {
                must (voteQuestion && voteQuestion->isVisible() && progress && progress->isVisible()
                    && voteQuestion->getY() == progress->getY(), "locked round progress shares the question row");
                whole (progress, "only inline round progress is present and fits");
            }
        }
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
