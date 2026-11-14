#pragma once
#include <vector>
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaQueuedStartText.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
#include "../src/HyphaReferenceAuditionRecoveryText.h"
#include "../src/HyphaLiveCompareActionResult.h"
#include "../src/HyphaLocalBlindAdmissionText.h"
#include "../src/HyphaOutputOwnershipText.h"

namespace hypha::tests::listen_text
{
using namespace live_compare;
struct Notice { juce::String text; Status state; };
inline std::vector<Notice> notices()
{
    std::vector<Notice> result;
    const auto add = [&] (juce::String text, Status state = {})
    {
        if (text.isEmpty()) return;
        for (const auto& item : result) if (item.text == text) return;
        result.push_back ({ text, state });
    };
    // Exercise the presentation for every public reason, both live and after teardown.
    for (int reason = 0; reason <= static_cast<int> (RecoveryReason::contentAmbiguous); ++reason)
        for (int mode = 0; mode < 14; ++mode)
        {
            Status state;
            state.prolongedWait = mode != 13;
            state.active = mode != 2 && mode != 3 && mode != 4;
            state.preSelected = state.preWaiting = true;
            state.reason = state.observation = static_cast<RecoveryReason> (reason);
            state.interrupted = mode == 1;
            state.postActual = mode == 3 ? 0.1f : 1.0f;
            state.finishing = mode == 5;
            state.contentHeld = mode == 6;
            state.compensationOff = mode == 7;
            state.clockUnavailable = mode == 8;
            state.timingUnavailable = mode == 9;
            state.loopBlock = mode == 10 ? LoopPairingBlock::auCycle
                            : mode == 11 ? LoopPairingBlock::aaxLoop : LoopPairingBlock::none;
            state.matchHeld = mode == 12;
            if (mode == 12) state.preWaiting = false;
            add (live_compare_ui::namedPresentation (state, mode == 4 ? StartResult::comparisonBusy
                                                                                    : StartResult::started).instruction, state);
            add (live_compare_ui::currentNamedPresentation (state).instruction, state);
        }
    for (auto restriction : { ReaperChainRestriction::unavailable, ReaperChainRestriction::channels,
            ReaperChainRestriction::inputFx, ReaperChainRestriction::pdcMode, ReaperChainRestriction::routing })
    {
        Status state; state.active = state.preSelected = state.prolongedWait = true;
        state.observation = RecoveryReason::reaperDelayUnverified; state.reaperRestriction = restriction;
        add (live_compare_ui::namedPresentation (state).instruction, state);
    }
    add ("MATCH waits for more signal");
    add ("Repeated audio: checked per block");
    add ("Aligning A"); add (live_compare_ui::positionNotice (true)); add (live_compare_ui::candidateUnconfirmedNotice());
    add ("LISTEN is not active");
    add ("MATCH first");
    // B, C or V in REF holding the output past 3 s (currentListenPresentation).
    for (int slot = 1; slot <= 3; ++slot)
    {
        Status state; state.active = state.preSelected = state.prolongedWait = true;
        add (live_compare_ui::currentListenPresentation (state, StartResult::started, slot).instruction, state);
    }
    // Direct action results and numeric boundaries in the LISTEN rail, including entry refusal,
    // MATCH, AUTO and END. These are transient notices, not timing verdicts.
    for (const char* text : { "LISTEN could not start", "Choose the PRE first",
        "Mono / stereo only", "PRE not running; choose again", "PRE is multi-mono: insert it as stereo",
        "Select PRE again", "LISTEN ended; POST plays",
        "MATCH failed; try again", "MATCH over 24 dB", "POST back to normal", "MATCH off: original levels", "MATCH on",
        "Press RETURN first", "Returning POST to normal, then LISTEN", "Returning POST to normal, then BLIND", "BLIND did not start; A/B continues", "PRE over TP ceiling", "PRE held: latency changed", "AUTO on: within 0.5 dB",
        "AUTO off", "AUTO stopped: editor hidden", "AUTO at TP ceiling: PRE 12.3 dB quieter", "AUTO 6 dB from MATCH: PRE 12.3 dB quieter", "AUTO 6 dB from MATCH: PRE 12.3 dB louder", "AUTO at 24 dB: PRE 12.3 dB quieter", "AUTO at 24 dB: PRE 12.3 dB louder", "Delay compensation is off in Pro Tools",
        "End the current comparison first", "BLIND COMPARE COULD NOT START", live_compare_ui::realtimeBounceNotice }) add (text);
    // A press waiting for its start (HyphaQueuedStartText.h), for every start and wait.
    for (auto start : { live_compare_ui::QueuedStart::listen, live_compare_ui::QueuedStart::blind,
                        live_compare_ui::QueuedStart::candidate })
        for (auto wait : { live_compare_ui::QueuedWait::returning, live_compare_ui::QueuedWait::tail,
                           live_compare_ui::QueuedWait::stopped, live_compare_ui::QueuedWait::other })
            add (live_compare_ui::queuedStartNotice (start, wait));
    for (auto action : { live_compare_ui::AfterTail::referenceV, live_compare_ui::AfterTail::referenceC,
                         live_compare_ui::AfterTail::referenceB, live_compare_ui::AfterTail::keepPair })
        for (bool playing : { true, false }) add (live_compare_ui::afterTailNotice (action, playing));
    // Why a greyed control or a key did nothing, or why the view changed by itself (2026-11-11).
    for (const char* text : { "POST is multi-mono: insert it as stereo", "BLIND is not available in this plugin format",
        "Finish Keep / Record before PRE / POST Blind", "Select LR, MID, or SIDE to view Delta",
        "Delta has no M/S; showing one channel", "2MIX has no DRUM; showing METERS", "DRUM: choose TRACK", "Copied",
        "No TP events yet", "Select POST for TP events", "No earlier TP over -1 dBTP", "No later TP over -1 dBTP",
        "History is already live", "No history yet", "Select POST to copy", "Click the history to hold a point first",
        "VU calibration: connecting; try again in a moment", "DAW recording: showing Hybrid VU",
        "AUTO stopped at the TP limit", "AUTO stopped: press MATCH again", "No OS Guide from Kirin OS yet",
        "Capture could not be prepared; try again", "Available in POST", "NOTE requires an active Keep",
        "Stop playback to change connection", "Ended; POST back to normal within 0.5 s", "Keep waits for PRE; Stop cancels",
        "Press LOWER A & START to begin", "Nothing to choose yet", "Only one to choose", "Play A for 3 s to make a target",
        "16 targets: remove one first", "Choose a target to remove", "Preparing the song; try again in a moment",
        "A target is measured data with no audio. Compare it on C.", "ALL SHAPE uses every song. Choose SHAPE to pick songs.",
        "PRE input measurements", "Local measurements; PRE subtraction does not apply",
        "SHARP: exact POST - PRE sharpness. A paired PRE and DAW output latency are required",
        "LIVE: POST values on fixed scales, measured every 100 ms.",
        "RUN: absolute facts grouped by playback run within the selected history",
        "ATTACK: automatic paired comparison or POST-only events" }) add (text);
    for (const auto* db : { "-24.00", "0.00", "+24.00" })
    {
        add (juce::String ("MATCH: PRE ") + db + " dB");
        add (juce::String ("MATCH: POST ") + db + " dB");
        add (juce::String ("TP limit: PRE ") + db + " dB, need +24.00 dB");
    }
    for (const auto* offset : { "0.02", "170.67", "9999.99" })
        for (const auto* side : { " early", " late" }) add (juce::String ("PRE ") + offset + " ms" + side);
    for (const auto* offset : { "0.02", "170.67", "9999.99" })
    {
        // INV-LC10: the content timing notice; PRE still sounds at K.
        Status state; state.active = state.preSelected = state.prolongedWait = state.contentHeld = true;
        state.contentJumpLagFrames = 1;
        add (juce::String ("PRE ") + offset + " ms late / " + live_compare_ui::namedPresentation (state).instruction, state);
    }
    for (int n = 0; n <= static_cast<int> (live_compare_ui::AutoReadiness::blocked); ++n)
        add (live_compare_ui::autoReadinessNotice (static_cast<live_compare_ui::AutoReadiness> (n)));
    for (int n = 1; n <= static_cast<int> (local_blind::CaptureAdmission::referenceAudition); ++n)
        add (local_blind_ui::admissionText (static_cast<local_blind::CaptureAdmission> (n)));
    for (int n = 1; n <= static_cast<int> (output_owner::Reason::auditionRunning); ++n)
        add (output_owner::refusalText (static_cast<output_owner::Reason> (n)));
    return result;
}
struct BlindCase { LiveBlindStatus state; bool playing; float post; };
inline std::vector<BlindCase> blindCases()
{
    std::vector<BlindCase> result;
    std::vector<blind_ui::Screen> seen;
    const auto add = [&] (LiveBlindStatus state, bool playing, float post)
    {
        auto screen = live_blind_ui::screenFor (state, playing, post);
        if (!screen.guidanceShown) screen.cause = screen.recovery = {};
        for (const auto& previous : seen) if (previous == screen) return;
        seen.push_back (screen); result.push_back ({ state, playing, post });
    };
    for (auto restriction : { ReaperChainRestriction::unavailable, ReaperChainRestriction::channels,
            ReaperChainRestriction::inputFx, ReaperChainRestriction::pdcMode, ReaperChainRestriction::routing })
    {
        LiveBlindStatus state; state.stage = BlindStage::preparing; state.prolongedWait = true;
        state.observation = RecoveryReason::reaperDelayUnverified; state.reaperRestriction = restriction;
        add (state, true, 1.0f);
    }
    { LiveBlindStatus state; state.stage = BlindStage::preparing;
      state.signalWait = state.prolongedWait = true; add (state, true, 1.0f); }
    // RESULT while a content notice still stands, and an A/B RESULT with both facts (INV-LC10).
    for (bool candidate : { false, true })
    { LiveBlindStatus state; state.candidate = candidate; state.stage = BlindStage::active; state.lowerPostDb = -24.0;
      state.contentHeld = state.prolongedWait = true; state.contentLagFrames = 1; state.contentNoticeMs = 9999.99;
      state.trial.active = state.trial.revealed = true; state.trial.played = 3;
      state.trial.usedPostFallback = state.trial.fallbackForTiming = state.trial.fallbackForCeiling = candidate;
      add (state, true, 1.0f); add (state, true, 0.0630957f); }
    // A to B BLIND adds the chain to the END line; the longest level is 24 dB.
    for (auto stage : { BlindStage::approval, BlindStage::active, BlindStage::finishing, BlindStage::invalidated })
    { LiveBlindStatus state; state.candidate = true; state.stage = stage; state.lowerPostDb = -24.0;
      add (state, true, 1.0f); add (state, true, 0.0630957f); }
    for (int reason = 0; reason <= static_cast<int> (RecoveryReason::contentAmbiguous); ++reason)
    for (int stage = 1; stage <= static_cast<int> (BlindStage::failed); ++stage)
    for (int mode = 0; mode < 14; ++mode)
    {
        if (stage == static_cast<int> (BlindStage::finishing)
            && (reason != 0 || (mode >= 1 && mode <= 6))) continue;
        LiveBlindStatus state;
        state.stage = static_cast<BlindStage> (stage);
        state.reason = state.observation = static_cast<RecoveryReason> (reason);
        state.contentHeld = mode == 1;
        state.compensationOff = mode == 2;
        state.clockUnavailable = mode == 3;
        state.timingUnavailable = mode == 4;
        state.loopBlock = mode == 5 ? LoopPairingBlock::auCycle
                        : mode == 6 ? LoopPairingBlock::aaxLoop : LoopPairingBlock::none;
        state.prolongedWait = mode < 10;
        state.trial.revealed = mode == 7 || mode >= 11;
        state.contentNoticeMs = mode == 7 || mode >= 11 ? 9999.99 : 0.0; // RESULT: substitute and estimate, longest amount
        state.trial.usedPostFallback = mode >= 11;
        state.trial.fallbackForTiming = mode == 11 || mode == 13;
        state.trial.fallbackForCeiling = mode >= 12;
        state.trial.played = mode == 8 ? 3 : 0;
        state.lowerPostDb = -24.0;
        state.waiting = mode == 8 ? MatchFailure::notEnoughSignal
                      : mode == 9 ? MatchFailure::outOfRange : MatchFailure::none;
        add (state, true, 1.0f);
        add (state, false, 0.0630957f); // largest approved return, 24 dB
    }
    return result;
}
}
