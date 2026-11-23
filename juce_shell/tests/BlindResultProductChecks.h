#pragma once
#include "LiveCompareNoticeProductChecks.h"
#include "../src/HyphaBlindImageSheet.h"
#include "../src/HyphaLiveBlindMemory.h"

// One real PRE/POST pair, the shipping editor and the real audio path: what BLIND keeps once it is answered or
// revealed, and what ends it.
namespace hypha::tests::editor_product
{
struct BlindRig : NoticeRig
{
    juce::File names;
    explicit BlindRig (const juce::File& root) : names (root.getChildFile ("blind-names.txt"))
    {
        advance (0.3); selectPair(); advance (0.2);
        expectOrdinaryAudio = false;
        blind().setPreferenceFile (names);
    }
    live_blind_ui::Component& blind() { return editor.get()->*shortcutMember (ShortcutLiveBlindView {}); }
    live_compare::LiveBlindStatus status() const { return post->liveBlindStatus(); }
    void start (int rounds)
    {
        require (blind().setPreferenceRounds (rounds), "the rounds of the next trial");
        (editor.get()->*shortcutMember (ShortcutBlindStart {}))();
        for (int n = 0; n < 200 && status().stage != live_compare::BlindStage::active; ++n) advance (0.1);
        require (status().stage == live_compare::BlindStage::active, "a real MATCH starts the trial");
        advance (0.1);
    }
    void listen (int source)
    {
        require (blind().requestSource (source), "the source can be chosen");
        for (int n = 0; n < 80 && (status().trial.audible != source || (status().trial.played & source) == 0); ++n) advance (0.05);
        require (status().trial.audible == source, "the chosen source is heard");
    }
    int hearBoth() // the source that is PRE, found by listening: the view names no side before REVEAL
    {
        int pre = 0;
        for (int source : { 1, 2 }) { listen (source); if (post->liveCompareStatus().preAudible) pre = source; }
        require (pre != 0 && status().trial.played == 3, "both sources were heard and one of them is PRE");
        return pre;
    }
    void answer (int source)
    {
        const auto before = blind().trialSummary().total();
        blind().onGuess (source);
        require (blind().trialSummary().total() == before + 1, "the answer is accepted");
        advance (0.05);
    }
    void reveal()
    {
        (editor.get()->*shortcutMember (ShortcutBlindReveal {}))();
        require (status().trial.revealed, "REVEAL is earned");
        advance (0.05);
    }
    void end()
    {
        (editor.get()->*shortcutMember (ShortcutBlindEnd {}))();
        for (int n = 0; n < 40 && status().stage != live_compare::BlindStage::idle; ++n) advance (0.05);
        require (status().stage == live_compare::BlindStage::idle, "END ends the trial");
        advance (0.3);
    }
    void stopped (double seconds) { clock.playing = false; advance (seconds); clock.playing = true; advance (0.4); }
    juce::String imageNote()
    {
        blind().openImageSheet();
        require (blind().imageSheetOpen(), "SAVE IMAGE opens on the RESULT");
        const auto note = blind().imageSheet().facts().note;
        blind().closeImageSheet();
        return note;
    }
};

// INV-LC19: the one line about a substitute counts what stood in while listening and answering. A stop, a seek or
// another choice on the RESULT changes neither the RESULT nor the picture SAVE IMAGE makes of it.
inline void verifyBlindResultFixedAtReveal (const juce::File& sandbox)
{
    ScopedChainTimingStorage preferences (sandbox);
    BlindRig r (sandbox);
    const juce::String timing = "POST was used briefly when timing was unavailable";
    const auto afterReveal = [&r] (int pre, const juce::String& expected, const char* where)
    {
        const auto tells = [&] (const char* when)
        {
            require (r.blind().resultFacts().note == expected, (juce::String (where) + ": RESULT " + when).toRawUTF8());
            require (r.imageNote() == expected, (juce::String (where) + ": SAVE IMAGE " + when).toRawUTF8());
            require (r.blind().screen().unguidedInstruction.contains ("POST was used briefly") == expected.isNotEmpty(),
                     (juce::String (where) + ": screen " + when).toRawUTF8());
        };
        tells ("at REVEAL");
        r.listen (pre);
        r.clock.playing = false; r.advance (0.5); tells ("stopped on PRE");
        r.clock.playing = true; r.advance (0.5); tells ("played again");
        r.clock.position += 96000; r.advance (0.4); tells ("after a seek");
        r.listen (3 - pre); r.stopped (0.3); r.listen (pre); tells ("after other choices");
    };
    r.start (1);
    auto pre = r.hearBoth();
    require (! r.status().trial.usedPostFallback, "nothing stood in while both sources were heard");
    r.listen (pre); r.reveal();
    afterReveal (pre, {}, "one listen");
    r.end();
    // All rounds answered, then the DAW stopped on PRE before REVEAL: no answer was given on that POST.
    r.start (5);
    for (int round = 0; round < 5; ++round)
    {
        pre = r.hearBoth();
        require (! r.status().trial.usedPostFallback, "nothing stood in during the round");
        r.listen (pre); r.answer (1 + round % 2);
    }
    r.clock.playing = false; r.advance (0.4);
    r.reveal();
    r.clock.playing = true; r.advance (0.4);
    afterReveal (pre, {}, "five answers");
    r.end();
    // Negative control: POST really stood in for PRE during the trial, so RESULT says so, the same before and after.
    r.start (1);
    pre = r.hearBoth();
    r.listen (pre); r.stopped (0.3);
    r.hearBoth(); r.listen (pre); r.reveal();
    require (r.status().trial.usedPostFallback && r.status().trial.fallbackForTiming, "the substitute of the trial is told");
    afterReveal (pre, timing, "a real substitute");
    r.end();
}
}
