#include "HyphaVersionBlindScreen.h"

namespace hypha::reference_ui
{
blind_ui::Screen versionBlindScreen (const State& state)
{
    blind_ui::Screen screen;
    const bool revealed = state.blindPhase == BlindPhase::revealed;
    const bool active = state.blindPhase == BlindPhase::active;
    const bool bothHeard = state.blindStimulusOneHeard && state.blindStimulusTwoHeard;
    screen.title = revealed ? "BLIND RESULT" : "VERSION BLIND";
    if (state.blindPhase == BlindPhase::invalidated)
        screen.instruction = "Blind stopped; A plays";
    else if (state.blindPhase == BlindPhase::starting)
        screen.instruction = state.transportPlaying ? "Preparing the sources; A plays" : "Play the DAW to begin";
    else if (revealed)
        screen.instruction = "Sources revealed; keep comparing";
    else if (active && state.blindPaused)
        screen.instruction = "Play the DAW to continue";
    else if (active && state.blindOutsideSong)
        screen.instruction = "Play within the song; A plays";
    else if (active)
        screen.instruction = bothHeard ? "Reveal the sources when ready" : "Try both sources while playing";
    // A を下げて始めた Blind は、終了で A が戻る量を言う（PRE/POST Blind の「終了すると音量が上がります」と同じ文）。
    screen.detail = state.blindRequiredAAttenuationDb > 0.05
        ? "END returns +" + juce::String (state.blindRequiredAAttenuationDb, 1) + " dB" : juce::String ("END returns to A");
    const juce::String compared = state.separateComparisons ? "V" : "B";
    screen.sourceOne = revealed ? "1: " + (state.blindOneIsComparison ? compared : juce::String ("A")) : juce::String ("SOURCE 1");
    screen.sourceTwo = revealed ? "2: " + (state.blindOneIsComparison ? juce::String ("A") : compared) : juce::String ("SOURCE 2");
    screen.sourcesShown = active || revealed;
    screen.sourceOneEnabled = ! state.blindPaused && state.pendingBlindStimulus != 1;
    screen.sourceTwoEnabled = ! state.blindPaused && state.pendingBlindStimulus != 2;
    screen.audible = state.activeBlindStimulus;
    screen.revealShown = active;
    screen.revealEnabled = bothHeard;
    screen.endTitle = "End Blind Compare and return to live A.";
    return screen;
}
}
