#include "BlindPreferenceSessionTest.h"
#include "../../src/live_compare/LiveBlindSession.h"
#include "../../src/live_compare/LiveCompareRecovery.h"
#include "../../src/HyphaLiveCompareRecoveryText.h"
#include <cstdlib>
#include <iostream>

static void require (bool value, const char* message)
{
    if (! value) { std::cerr << message << '\n'; std::exit (1); }
}

int main()
{
    runBlindPreferenceSessionTests();
    using namespace hypha::live_compare;
    NamedSelection selection;
    selection.select (true);
    const auto old = selection.command();
    selection.select (true);
    selection.fail (old, RecoveryReason::ceiling);
    require (selection.command().pre() && selection.command().reason() == RecoveryReason::none,
             "old fault cannot clear or diagnose a newer PRE selection");
    const auto currentSelection = selection.command();
    selection.fail (currentSelection, RecoveryReason::nonFinite);
    selection.end (RecoveryReason::preUnavailable);
    require (! selection.command().pre() && selection.command().reason() == RecoveryReason::nonFinite,
             "first failure and selection closure are one receipt");
    selection.select (true);
    const auto beforeEnd = selection.command();
    selection.end();
    selection.fail (beforeEnd, RecoveryReason::ceiling);
    require (! selection.command().pre() && selection.command().reason() == RecoveryReason::none,
             "explicit END defeats a delayed fault without fabricating a cause");

    using namespace hypha::live_compare_ui;
    Status status;
    status.compensationOff = true;
    require (namedPresentation (status).action == RecoveryAction::none,
             "untouched inactive comparison does not announce an internal PDC condition");
    status.compensationOff = false;
    for (auto reason : { RecoveryReason::restored, RecoveryReason::formatChanged, RecoveryReason::pairChanged })
    {
        status.reason = reason;
        require (namedPresentation (status).action != RecoveryAction::endBlind
            && std::string (namedRecovery (status)).find ("END") == std::string::npos,
            "inactive unity never directs a nonexistent END");
        status.postTarget = 0.5f;
        require (namedPresentation (status).action == RecoveryAction::returnLevel, "held output directs actual RETURN");
        status.postTarget = 1.0f;
    }
    status.contentHeld = true; status.reason = RecoveryReason::none;
    require (namedPresentation (status).action == RecoveryAction::none, "after END a content timing notice has no remedy");
    LiveBlindStatus stopped;
    stopped.reason = RecoveryReason::contentChanged; stopped.contentHeld = true;
    require (blindRecovery (stopped, true).action == RecoveryAction::endBlind, "an invalidated trial needs END, not a content notice");
    stopped.contentHeld = false;
    require (blindRecovery (stopped, true).action == RecoveryAction::endBlind
        && blindRecovery (stopped, true).reason == RecoveryReason::contentChanged,
        "resolved blocker changes the action, never rewrites history");
    status.active = true; status.prolongedWait = true; status.timingUnavailable = true;
    require (namedPresentation (status).action == RecoveryAction::none
        && std::string (namedRecovery (status)) != chainDelayNotice(),
             "unavailable timing, which plays POST, outranks a content notice (INV-LC10)");
    status.contentHeld = false;
    require (namedPresentation (status).action == RecoveryAction::none,
             "unsupported timing never promises automatic admission");
    stopped.prolongedWait = true; stopped.timingUnavailable = true;
    require (blindRecovery (stopped, false).action == RecoveryAction::none,
             "unsupported Blind retains END rather than endless automatic progress");
    BlindSession unavailable;
    require (! unavailable.startWith ([] () -> bool { throw 1; })
        && ! unavailable.command().active(), "CSPRNG failure never publishes a fallback assignment");
    for (bool firstPre : { false, true })
    {
        BlindSession trial;
        trial.start (firstPre);
        const auto first = trial.command();
        require (first.pre() == firstPre && first.stimulus() == 1, "both random mappings work");
        trial.observe (first, false);
        require (! trial.reveal(), "a crossfade alone is not a played source");
        trial.observe (first, true);
        require (! trial.reveal(), "one source is not enough");
        trial.observe (first, false);
        require (trial.view().audible == 0 && trial.view().played == 1,
                 "temporary POST fallback withdraws only the current audible receipt");
        trial.observe (first, true);
        require (trial.view().audible == 1 && trial.view().played == 1,
                 "the next wholly stable block restores the same source receipt");
        require (trial.select (2), "select second");
        const auto second = trial.command();
        require (second.pre() != firstPre, "second is the other source");
        trial.observe (first, true);
        require (trial.view().audible == 0, "late first receipt is not a second receipt");
        require (! trial.reveal(), "late receipt does not unlock reveal");
        trial.observe (second, true);
        require (trial.view().played == 3 && trial.reveal(), "both sources permit reveal without an answer");
        require (trial.view().revealed && trial.view().firstPre == firstPre, "reveal agrees with rendered mapping");
        require (! trial.reveal(), "reveal only once");
        trial.invalidate (second, RecoveryReason::callbackGap);
        trial.invalidate (second, RecoveryReason::unknown);
        require (trial.view().invalidated && ! trial.select (1), "fault after reveal cannot restart");
        require (trial.view().reason == RecoveryReason::callbackGap, "reason and invalidation are inseparable after reveal");
        trial.end();
        require (trial.view().reason == RecoveryReason::callbackGap, "teardown preserves the terminal cause");
        trial.start (! firstPre);
        trial.observe (second, true);
        trial.invalidate (second, RecoveryReason::callbackGap);
        require (trial.view().played == 0 && ! trial.view().invalidated && trial.view().reason == RecoveryReason::none,
                 "old trial cannot affect new trial or its reason");
        const auto current = trial.command();
        trial.end();
        trial.observe (current, true);
        trial.invalidate (current, RecoveryReason::ceiling);
        require (trial.view().reason == RecoveryReason::none, "END seals a clean trial against late failure");
        require (! trial.command().active() && ! trial.reveal(), "END defeats delayed audio receipts");
        trial.start (firstPre);
        require (! trial.select (0) && ! trial.select (3) && ! trial.reveal(), "bad commands are refused");
    }
    // POST の代替は、聞こえる長さ（連続 25 ms 以上）だけ REVEAL で知らせる（折り返しの 1 callback、1024@44.1/48k や
    // 2048@96k の単発は知らせない。2048@48k や連続 25 ms 以上は知らせる）。
    {
        BlindSession brief; brief.start (true);          // SOURCE 1 = PRE が要求されている
        const auto cmd = brief.command();
        brief.observe (cmd, true);
        brief.fallback (cmd, 512, 48000.0);              // 10.7 ms
        require (! brief.view().usedPostFallback, "one short callback of POST during PRE is not disclosed");
        brief.observe (cmd, true);                       // PRE に戻った：次の倒れは別の run
        brief.fallback (cmd, 512, 48000.0);
        brief.observe (cmd, false);                      // 戻りの crossfade は run を切らない
        brief.fallback (cmd, 687, 48000.0);              // 1199 frames = 24.98 ms
        require (! brief.view().usedPostFallback, "separate short runs never add up; just under 25 ms stays undisclosed");
        brief.fallback (cmd, 1, 48000.0);                // 1200 frames = 25 ms
        require (brief.view().usedPostFallback, "25 ms of contiguous POST during PRE is disclosed");
        require (brief.view().fallbackForTiming && ! brief.view().fallbackForCeiling, "the result names timing only when timing did it");
        BlindSession ceiling;
        ceiling.start (true);
        ceiling.fallback (ceiling.command(), 1200, 48000.0, true);
        require (ceiling.view().usedPostFallback && ceiling.view().fallbackForCeiling && ! ceiling.view().fallbackForTiming,
                 "the ceiling is named when the ceiling did it");
        BlindSession mixed;
        mixed.start (true);
        mixed.fallback (mixed.command(), 600, 48000.0, true);
        mixed.fallback (mixed.command(), 600, 48000.0);
        require (mixed.view().fallbackForCeiling && mixed.view().fallbackForTiming, "a run of both names both");
        for (const auto [frames, rate] : { std::pair { 1024, 48000.0 }, std::pair { 1024, 44100.0 }, std::pair { 2048, 96000.0 } })
        {
            BlindSession single; single.start (true);
            single.fallback (single.command(), frames, rate);
            require (! single.view().usedPostFallback, "a single callback shorter than 25 ms is not disclosed");
            single.observe (single.command(), true);
            single.fallback (single.command(), frames, rate);
            require (! single.view().usedPostFallback, "two runs separated by audible PRE are not added together");
        }
        BlindSession one; one.start (true);
        one.fallback (one.command(), 2048, 48000.0);     // 42.7 ms の 1 callback
        require (one.view().usedPostFallback, "a single long callback is disclosed");
        BlindSession postSide; postSide.start (false);   // SOURCE 1 = POST
        postSide.fallback (postSide.command(), 96000, 48000.0);
        require (! postSide.view().usedPostFallback, "POST while POST is requested is not a fallback");
        BlindSession across; across.start (true);
        across.fallback (across.command(), 512, 48000.0);
        across.reset(); across.start (true);
        across.fallback (across.command(), 512, 48000.0);
        require (! across.view().usedPostFallback, "a new trial starts its own run");
    }
    // REVEAL fixes what RESULT tells: a stop, a seek or another choice afterwards adds nothing (INV-LC19).
    const auto hear = [&] (BlindSession& trial)
    {
        for (int source : { 1, 2 })
        {
            require (trial.select (source), "select a source to hear");
            trial.observe (trial.command(), true);
        }
    };
    for (bool firstPre : { false, true })
    {
        const int preSource = firstPre ? 1 : 2;
        const auto standIn = [&] (BlindSession& trial, bool ceiling = false)
        {
            require (trial.select (preSource), "the source that is PRE");
            for (int block = 0; block < 60; ++block) trial.fallback (trial.command(), 512, 48000.0, ceiling); // a stopped DAW
        };
        BlindSession clean; clean.start (firstPre); hear (clean);
        require (clean.reveal() && ! clean.view().usedPostFallback, "nothing stood in while listening");
        standIn (clean); standIn (clean, true);
        require (clean.view().revealed && ! clean.view().usedPostFallback && ! clean.view().fallbackForTiming
                 && ! clean.view().fallbackForCeiling, "POST after REVEAL never joins RESULT");
        clean.invalidate (clean.command(), RecoveryReason::pairChanged);
        require (clean.view().invalidated && clean.view().reason == RecoveryReason::pairChanged && ! clean.view().usedPostFallback,
                 "the fixed facts leave the terminal reason intact");
        // Negative control: what really stood in before REVEAL stays told, with its own cause only.
        BlindSession real; real.start (firstPre); hear (real); standIn (real); hear (real);
        require (real.view().usedPostFallback && real.reveal() && real.view().usedPostFallback
                 && real.view().fallbackForTiming && ! real.view().fallbackForCeiling, "a real substitute stays in RESULT");
        standIn (real, true);
        require (real.view().fallbackForTiming && ! real.view().fallbackForCeiling, "a later ceiling never rewrites its cause");
        // A short run left open at REVEAL does not become a disclosure afterwards.
        BlindSession open; open.start (firstPre); hear (open);
        require (open.select (preSource), "PRE for the open run");
        open.fallback (open.command(), 1000, 48000.0);
        require (open.reveal(), "REVEAL in an open run");
        open.fallback (open.command(), 1000, 48000.0);
        require (! open.view().usedPostFallback, "a run that passes 25 ms after REVEAL is not told");
        // A preference round: up to its answer it counts, after the answer and after REVEAL it does not.
        BlindSession answered; answered.start (firstPre); hear (answered);
        const auto epoch = answered.command().epoch();
        require (answered.acceptPreference (1, epoch).has_value(), "the round is answered");
        standIn (answered);
        require (! answered.view().usedPostFallback && answered.revealPreference (epoch)
                 && ! answered.view().usedPostFallback, "a stop between the last answer and REVEAL is not told");
        standIn (answered);
        require (! answered.view().usedPostFallback, "nor is one after REVEAL");
        BlindSession during; during.start (firstPre); hear (during); standIn (during); hear (during);
        const auto round = during.command().epoch();
        require (during.acceptPreference (1, round).has_value() && during.view().usedPostFallback
                 && during.revealPreference (round) && during.view().usedPostFallback, "POST during the answered round stays told");
        BlindSession next; next.start (firstPre); hear (next);
        const auto before = next.command().epoch();
        require (next.acceptPreference (1, before).has_value()
                 && next.nextPreferenceWith (before, [firstPre] { return firstPre; }) == BlindSession::NextPreference::started,
                 "the next round starts");
        standIn (next);
        require (next.view().usedPostFallback && next.view().epoch == before + 1, "an answered round never hides the next one's facts");
        next.start (firstPre); hear (next);
        require (! next.view().usedPostFallback && next.reveal() && ! next.view().usedPostFallback, "a new trial starts clean");
    }
    std::cout << "Live Blind session: PASS (both mappings, late receipts, faults, END, restart, brief fallback, RESULT fixed at REVEAL)\n";
}
