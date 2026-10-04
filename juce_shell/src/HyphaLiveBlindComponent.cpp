#include "HyphaLiveBlindComponent.h"
#include "HyphaLiveCompareRecoveryText.h"
#include <cmath>

namespace hypha::live_blind_ui
{
using Stage = live_compare::BlindStage;

blind_ui::Screen screenFor (const live_compare::LiveBlindStatus& current, bool playing, float actualPost)
{
    blind_ui::Screen screen;
    const bool live = current.stage == Stage::active && ! current.trial.invalidated;
    const bool revealed = live && current.trial.revealed;
    const bool finishing = current.stage == Stage::finishing;
    const bool stopped = current.stage == Stage::invalidated || current.trial.invalidated;
    const auto guidance = live_compare_ui::blindRecovery (current, stopped);
    const bool waiting = (current.stage == Stage::preparing || current.stage == Stage::settling)
        && (current.contentHeld || current.compensationOff
            || (playing && current.observation != live_compare::RecoveryReason::none));
    screen.guidanceShown = stopped || waiting;
    const auto visibleCause = stopped ? guidance.reason : current.contentHeld ? live_compare::RecoveryReason::contentChanged
        : current.compensationOff ? live_compare::RecoveryReason::compensationOff : guidance.reason;
    screen.cause = live_compare_ui::cause (visibleCause);
    screen.recovery = guidance.instruction;
    screen.title = revealed ? "BLIND RESULT" : "LIVE BLIND";
    const auto returnDb = actualPost > 0.0f ? -20.0 * std::log10 (actualPost) : 0.0;
    juce::String instruction, explanation = returnDb > 0.05
        ? "END returns +" + juce::String (returnDb, 1) + " dB" : "END restores normal level";
    if (current.stage == Stage::failed)
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
    else if (waiting && (current.observation == live_compare::RecoveryReason::loopUnproven
        || current.observation == live_compare::RecoveryReason::loopTooShort
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
    screen.approveShown = current.stage == Stage::approval;
    screen.approveTitle = "Lower POST and begin Blind";
    screen.approveDescription = explanation;
    screen.endEnabled = ! finishing;
    screen.endTitle = "End and restore normal level";
    return screen;
}
}
