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
    enum class Delay { none, changed, unmeasured, notRemeasured };
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
    for (const auto delay : { Delay::none, Delay::changed, Delay::unmeasured, Delay::notRemeasured })
    for (const bool repeating : { false, true })
    for (const bool prePaused : { false, true })
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
        h.status.contentUnmeasured = delay == Delay::unmeasured;
        h.status.contentUnapplied = delay == Delay::changed;
        h.status.contentNotRemeasured = delay == Delay::notRemeasured;
        h.status.prolongedWait = prePaused;
        h.status.observation = prePaused ? live_compare::RecoveryReason::notWritten : live_compare::RecoveryReason::none;
        h.view.setState (h.status, true, lowered ? 0.616595f : 1.0f, repeating);

        const auto context = juce::String (language == i18n::Language::japanese ? "JA" : "EN")
            + " " + juce::String (width) + "@" + juce::String (scale) + " rounds=" + juce::String (rounds)
            + " candidate=" + juce::String (int (candidate)) + " guess=" + juce::String (int (guess))
            + " votePhase=" + juce::String (votePhase) + " lowered=" + juce::String (int (lowered))
            + " delay=" + juce::String (int (delay)) + " repeating=" + juce::String (int (repeating))
            + " prePaused=" + juce::String (int (prePaused));
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
        const auto* cause = label (h.view, "text-4");
        const auto* remedy = label (h.view, "text-5");
        const auto& actual = h.view.screen();
        const auto guide = actual.cause + (actual.cause.isNotEmpty() && actual.recovery.isNotEmpty() ? "\n" : "") + actual.recovery;
        const bool isPrePause = prePaused && !candidate;
        juce::StringArray priority;
        if (isPrePause)
        {
            must (actual.cause == live_compare_ui::preProcessingCause && actual.recovery.isNotEmpty(), "PRE pause is a full cause/remedy");
            priority.add (guide);
        }
        if (delay == Delay::changed) priority.add (live_compare_ui::unappliedNotice());
        if (repeating) priority.add ("Repeated audio: checked per block");
        if (delay == Delay::unmeasured)
            priority.add (juce::String (live_compare_ui::unmeasuredNotice()) + " / " + live_compare_ui::chainDelayNotice());
        if (delay == Delay::notRemeasured) priority.add (live_compare_ui::notRemeasuredNotice());
        if (prePaused && !isPrePause)
        {
            must (actual.guidanceShown && guide.isNotEmpty(), "candidate pair keeps its own recovery message");
            priority.add (guide);
        }
        const auto isInline = [&] (const juce::String& raw)
        {
            if (guide.isNotEmpty() && raw == guide)
                return cause && remedy && (actual.cause.isEmpty() || cause->isVisible())
                       && (actual.recovery.isEmpty() || remedy->isVisible());
            return note && note->isVisible() && note->getText().contains (raw);
        };
        if (note && note->isVisible())
        {
            whole (note, "visible notice fits in a measured row");
            must (explanation && explanation->getBottom() <= note->getY(), "read END before notices");
        }
        if (isPrePause && isInline (guide))
        {
            whole (cause, "complete PRE pause cause");
            whole (remedy, "complete PRE pause remedy");
        }
        // Measure the input notices, not the potentially folded product label's text or height.
        const auto usableWidth = width - 2 * juce::jlimit (6, 24, width / 40);
        const auto measured = [&] (const juce::Label* style, const juce::String& text)
        {
            if (!style || text.isEmpty()) return 0;
            const auto border = style->getBorderSize();
            const auto innerWidth = juce::jmax (1, usableWidth - border.getLeftAndRight());
            return static_cast<int> (std::ceil (text_style::shownWrappedHeight (text, style->getFont(), innerWidth)))
                + border.getTopAndBottom() + 2;
        };
        auto inlineInput = priority;
        if (guide.isNotEmpty()) inlineInput.removeString (guide);
        const auto allNoticesNeeded = measured (note, inlineInput.joinIntoString ("\n"))
            + measured (cause, actual.cause) + measured (remedy, actual.recovery);
        auto room = h.view.getHeight() - (actual.sourcesShown ? sourceMinimum : 0)
            - minimumActionHeight * (1 + int (actual.guessShown) + int (actual.approveShown))
            - measured (explanation, actual.endExplanation.isNotEmpty() ? actual.endExplanation : actual.detail)
            - (finalVote ? measured (label (h.view, "text-2"), actual.instruction) : 0);
        if (const auto* outcome = label (h.view, "guess-outcome"); actual.guessShown && outcome && outcome->isVisible())
            if (const auto* question = label (h.view, "guess-question"); question && question->getY() != outcome->getY())
                room -= measured (outcome, actual.guessOutcome);

        if (allNoticesNeeded <= room)
            for (const auto& raw : priority)
                must (isInline (raw), "all notices must show directly when their full measured text fits");
        // Identical demanding content at both widths: compact folds, wide restores every row.
        const auto foldingPair = language == i18n::Language::japanese && scale == 1.0f && !candidate
            && rounds == 5 && votePhase == 2 && lowered && delay == Delay::changed && repeating && prePaused;
        if (foldingPair && width == 300)
            must (allNoticesNeeded > room && more && more->isVisible(), "300px folds this fixed full stack");
        if (foldingPair && width == 900)
            must (allNoticesNeeded <= room && more && !more->isVisible(), "900px restores the same full stack");
        int concealed = 0;
        for (const auto& raw : priority)
        {
            if (isInline (raw)) continue;
            ++concealed;
            const auto translated = text_style::shownText (raw);
            must (more && more->isVisible() && more->getDescription().contains (translated), "hidden notice retains full text for help and accessibility");
        }
        if (concealed > 0)
        {
            must (priority.size() >= 2 && isInline (priority[0]) && concealed == priority.size() - 1,
                  "only the highest-priority complete notice is retained when the full stack does not fit");
            // This offscreen fixture has no native peer. The shortcut fixture checks the live AX handler.
            must (more && more->isAccessible() && more->getTooltip().isEmpty()
                  && more->getButtonText() == "+" + juce::String (concealed) && more->getWantsKeyboardFocus(),
                  "the +N control counts exactly the hidden notices and exposes their full descriptions");
        }
        else must (more && !more->isVisible(), "no overflow badge when every notice fits");

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
