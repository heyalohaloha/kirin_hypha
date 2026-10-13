#pragma once
#include <vector>
#include "../src/HyphaLiveBlindComponent.h"
#include "../src/HyphaLiveCompareRecoveryText.h"
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
    for (int reason = 0; reason <= static_cast<int> (RecoveryReason::reaperLoopTooShort); ++reason)
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
    // Direct action results and numeric boundaries in the LISTEN rail, including entry refusal,
    // MATCH, AUTO, PIN and END. These are transient notices, not timing verdicts.
    for (const char* text : { "LISTEN could not start", "Choose the PRE first",
        "Mono / stereo only", "Paired PRE unavailable", "PRE is multi-mono: insert it as stereo",
        "Select PRE again", "LISTEN ended; POST plays",
        "MATCH failed; try again", "MATCH over 24 dB", "POST back to normal",
        "Press RETURN first", "PRE over TP ceiling", "PRE held: latency changed", "PIN waits for PRE",
        "PIN needs 4 s of play", "Last 4 s not one range", "PIN failed; try again", "AUTO on: within 0.5 dB",
        "AUTO off", "AUTO stopped: editor hidden", "AUTO stopped: TP ceiling", "AUTO stopped: over 6 dB", "Delay compensation is off in Pro Tools",
        "End the current comparison first", "BLIND COMPARE COULD NOT START" }) add (text);
    for (const auto* db : { "-24.00", "0.00", "+24.00" })
    {
        add (juce::String ("MATCH: PRE ") + db + " dB");
        add (juce::String ("MATCH: POST ") + db + " dB");
        add (juce::String ("TP limit: PRE ") + db + " dB, need +24.00 dB");
    }
    for (const auto* offset : { "0.02", "170.67", "9999.99" })
        for (const auto* side : { " early", " late" }) add (juce::String ("PRE ") + offset + " ms" + side);
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
    for (int reason = 0; reason <= static_cast<int> (RecoveryReason::reaperLoopTooShort); ++reason)
    for (int stage = 1; stage <= static_cast<int> (BlindStage::failed); ++stage)
    for (int mode = 0; mode < 12; ++mode)
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
        state.trial.revealed = mode == 7 || mode == 11;
        state.trial.usedPostFallback = mode == 11;
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
