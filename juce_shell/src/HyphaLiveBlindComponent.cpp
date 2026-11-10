#include "HyphaLiveBlindComponent.h"
#include "HyphaLiveCompareRecoveryText.h"
#include "HyphaPairingLoopText.h"
#include <cmath>

namespace hypha::live_blind_ui
{
using Stage = live_compare::BlindStage;

blind_ui::Screen screenFor (const live_compare::LiveBlindStatus& current, bool playing, float actualPost, bool repeatingAudio)
{
    blind_ui::Screen screen;
    const bool live = current.stage == Stage::active && !current.trial.invalidated;
    const bool revealed = live && current.trial.revealed;
    const bool finishing = current.stage == Stage::finishing;
    const bool stopped = current.stage == Stage::invalidated || current.trial.invalidated;
    const auto guidance = live_compare_ui::blindRecovery (current, stopped);
    screen.guidanceShown = stopped || current.prolongedWait;
    screen.cause = screen.guidanceShown && !current.signalWait ? live_compare_ui::cause (guidance.reason) : "";
    screen.recovery = screen.guidanceShown ? guidance.instruction : "";
    screen.title = revealed ? "BLIND RESULT" : "LIVE BLIND";
    const auto returnDb = actualPost > 0.0f ? -20.0 * std::log10 (actualPost) : 0.0;
    const juce::String normalLevel = "END restores normal level";
    juce::String instruction, explanation = returnDb > 0.05
        ? "END returns +" + juce::String (returnDb, 1) + " dB" : normalLevel;
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
        instruction = current.candidate ? "Lower B to match levels?" : "Lower POST to match levels?";
        explanation = "Lower " + juce::String (std::abs (current.lowerPostDb), 1)
            + " dB; END returns the same amount";
    }
    else if (revealed)
        instruction = "Sources revealed; keep comparing";
    else if (live)
        instruction = current.trial.played == 3 ? "Reveal the sources when ready" : "Try both sources while playing";
    else
        instruction = "";
    if (revealed && current.trial.usedPostFallback)
        instruction = ! current.trial.fallbackForCeiling ? "POST was used briefly when timing was unavailable"
            : ! current.trial.fallbackForTiming ? "POST played where PRE went over the ceiling"
            : "POST played for timing and the PRE ceiling";
    if (revealed && current.candidate && current.trial.usedPostFallback)
        instruction = "Chain audio was used briefly";
    if (current.candidate && current.stage == Stage::preparing && current.matchSecondsRemaining > 0)
    {
        instruction = "MATCH: " + juce::String (current.matchSecondsRemaining) + " s remaining";
        if (current.signalWait) screen.guidanceShown = false;
    }
    screen.instruction = screen.guidanceShown ? juce::String() : instruction;
    // One sentence: END says once what it gives back, the level and then the A to B chain.
    if (current.candidate)
        explanation += explanation == normalLevel ? " and the A to B chain" : " and restores the A to B chain";
    screen.detail = explanation;
    // The rise on END is named before anything else (INV-LC14).
    if (live && !revealed && repeatingAudio && ! (returnDb > 0.05)) screen.detail = "Repeated audio: checked per block";
    const char* firstA = current.candidate ? "1: A" : "1: PRE";
    const char* firstB = current.candidate ? "1: B" : "1: POST";
    const char* secondA = current.candidate ? "2: A" : "2: PRE";
    const char* secondB = current.candidate ? "2: B" : "2: POST";
    screen.sourceOne = revealed ? (current.trial.firstPre ? firstA : firstB) : "SOURCE 1";
    screen.sourceTwo = revealed ? (current.trial.firstPre ? secondB : secondA) : "SOURCE 2";
    screen.sourceOneEnabled = screen.sourceTwoEnabled = live;
    // Before the first source is ready the two buttons stand unlit, so the screen is not END alone.
    screen.sourcesShown = live || (! screen.guidanceShown && (current.stage == Stage::preparing || current.stage == Stage::settling));
    screen.audible = current.trial.selected; // selected button, not an audio receipt
    screen.revealShown = live && ! revealed;
    screen.revealEnabled = current.trial.played == 3;
    // Keep an earned REVEAL reachable; ALIGN OFF only replaces an unavailable action.
    screen.alignOffShown = !stopped && !finishing && current.prolongedWait
        && (guidance.reason == live_compare::RecoveryReason::contentAmbiguous
            || guidance.reason == live_compare::RecoveryReason::preLate)
        && !(screen.revealShown && screen.revealEnabled);
    screen.approveShown = current.stage == Stage::approval && !current.compensationOff;
    screen.approveTitle = current.candidate ? "Lower B and begin Blind" : "Lower POST and begin Blind";
    screen.approveDescription = explanation;
    screen.endEnabled = ! finishing;
    screen.endTitle = current.candidate ? "End A/B and restore the chain" : "End and restore normal level";
    return screen;
}
}
