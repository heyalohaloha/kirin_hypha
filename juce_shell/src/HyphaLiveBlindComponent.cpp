#include "HyphaLiveBlindComponent.h"
#include "HyphaLiveCompareRecoveryText.h"
#include "HyphaPairingLoopText.h"
#include <cmath>

namespace hypha::live_blind_ui
{
using Stage = live_compare::BlindStage;

blind_ui::Screen screenFor (const live_compare::LiveBlindStatus& current, bool playing, float actualPost)
{
    blind_ui::Screen screen;
    const bool cycleUnverified = current.loopBlock == live_compare::LoopPairingBlock::auCycle;
    const bool live = current.stage == Stage::active && ! current.trial.invalidated && ! current.contentHeld
        && ! cycleUnverified;
    const bool revealed = live && current.trial.revealed;
    const bool finishing = current.stage == Stage::finishing;
    const bool stopped = current.stage == Stage::invalidated || current.trial.invalidated;
    const auto guidance = live_compare_ui::blindRecovery (current, stopped);
    const bool waiting = (cycleUnverified && current.stage != Stage::idle && ! finishing && ! stopped)
        || current.contentHeld || (current.stage == Stage::approval && current.compensationOff)
        || ((current.stage == Stage::preparing || current.stage == Stage::settling)
        && (current.compensationOff || (playing && current.observation != live_compare::RecoveryReason::none)));
    screen.guidanceShown = stopped || waiting || current.timingUnavailable || current.clockUnavailable;
    const auto visibleCause = stopped ? guidance.reason : current.contentHeld ? live_compare::RecoveryReason::contentChanged
        : current.compensationOff ? live_compare::RecoveryReason::compensationOff : guidance.reason;
    // An uncertified AAX host looping a range is not the host boundary: loop playback off compares.
    screen.cause = current.contentHeld ? live_compare_ui::cause (live_compare::RecoveryReason::contentChanged)
                   : visibleCause == live_compare::RecoveryReason::reaperLoopTooShort
                       || visibleCause == live_compare::RecoveryReason::reaperDelayUnverified ? live_compare_ui::cause (visibleCause)
                   : current.timingUnavailable && current.loopBlock == live_compare::LoopPairingBlock::none
                       && guidance.reason != live_compare::RecoveryReason::cycleUnverified ? host_availability::timingText()
                                            : live_compare_ui::cause (visibleCause);
    screen.recovery = guidance.instruction;
    screen.title = revealed ? "BLIND RESULT" : "LIVE BLIND";
    const auto returnDb = actualPost > 0.0f ? -20.0 * std::log10 (actualPost) : 0.0;
    juce::String instruction, explanation = returnDb > 0.05
        ? "END returns +" + juce::String (returnDb, 1) + " dB" : "END restores normal level";
    if (current.contentHeld)
        instruction = "Comparison paused; POST plays";
    else if (waiting && current.loopBlock != live_compare::LoopPairingBlock::none)
        instruction = pairing_loop_text::status (current.loopBlock);
    else if (!stopped && current.observation == live_compare::RecoveryReason::reaperFirstCycle)
        instruction = "Checking loop timing; POST plays";
    else if (!stopped && current.observation == live_compare::RecoveryReason::reaperDelayUnverified)
        instruction = "Loop delay unverified; POST; END closes";
    else if (!stopped && current.observation == live_compare::RecoveryReason::reaperLoopTooShort)
        instruction = "Use a longer loop; POST plays";
    else if ((current.timingUnavailable || current.clockUnavailable) && guidance.reason != live_compare::RecoveryReason::cycleUnverified)
        instruction = "Comparison unavailable; POST plays";
    else if (current.compensationOff)
        instruction = "Waiting for comparison; POST plays";
    else if (current.stage == Stage::failed)
        instruction = current.waiting == live_compare::MatchFailure::outOfRange
            ? "MATCH over 24 dB" : "MATCH failed; try again";
    else if (stopped)
    {
        instruction = current.reason == live_compare::RecoveryReason::outputTaken
            ? "Blind stopped; output released" : "Blind stopped; POST output";
        const auto db = actualPost > 0.0f ? -20.0 * std::log10 (actualPost) : 0.0;
        explanation = db > 0.05 ? "END returns +" + juce::String (db, 1) + " dB" : explanation;
    }
    else if (finishing)
        instruction = playing ? "Returning to normal level" : "Return waiting for audio";
    else if (current.stage == Stage::approval)
    {
        instruction = "Lower POST to match levels?";
        explanation = "Lower " + juce::String (std::abs (current.lowerPostDb), 1)
            + " dB; END returns the same amount";
    }
    else if (revealed)
        instruction = "Sources revealed; keep comparing";
    else if (live)
        instruction = current.trial.played == 3 ? "Reveal the sources when ready" : "Try both sources while playing";
    else if (! playing)
        instruction = "Play the DAW to begin";
    else if (waiting && (current.observation == live_compare::RecoveryReason::loopTooShort
        || current.observation == live_compare::RecoveryReason::loopClockUnavailable))
        instruction = current.observation == live_compare::RecoveryReason::loopTooShort
            ? "Loop is too short for verified timing" : "DAW timing is unavailable for this loop";
    else if (waiting)
        instruction = "Waiting for comparison; POST plays";
    else if (current.waiting == live_compare::MatchFailure::outOfRange)
        instruction = "MATCH over 24 dB";
    else if (current.waiting == live_compare::MatchFailure::notEnoughSignal)
        instruction = "MATCH needs more signal";
    else
        instruction = "Matching levels; POST plays";
    screen.instruction = instruction;
    screen.detail = explanation;
    screen.sourceOne = revealed ? (current.trial.firstPre ? "1: PRE" : "1: POST") : "SOURCE 1";
    screen.sourceTwo = revealed ? (current.trial.firstPre ? "2: POST" : "2: PRE") : "SOURCE 2";
    screen.sourcesShown = screen.sourceOneEnabled = screen.sourceTwoEnabled = live;
    screen.audible = current.trial.audible;
    screen.revealShown = live && ! revealed;
    screen.revealEnabled = current.trial.played == 3;
    screen.approveShown = current.stage == Stage::approval && ! current.contentHeld
        && ! cycleUnverified && ! current.timingUnavailable && ! current.clockUnavailable && ! current.compensationOff;
    screen.approveTitle = "Lower POST and begin Blind";
    screen.approveDescription = explanation;
    screen.endEnabled = ! finishing;
    screen.endTitle = "End and restore normal level";
    return screen;
}
}
