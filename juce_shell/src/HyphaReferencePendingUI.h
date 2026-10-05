#pragma once
#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
// 再生中でも、準備が自動で進む段階（確認・読み込み・準備・位置合わせ）なら押した役を待たせ、準備でき次第鳴らす
// （押したことを捨てない。待ちの上限は HyphaReferencePreparationWatch.h が見張る）。
inline bool settlesByItself (SourceStep step) noexcept
{
    return step == SourceStep::verifyingSource || step == SourceStep::loadingAudio
        || step == SourceStep::preparing || step == SourceStep::aligning;
}

inline bool canQueueSource (const State& state, bool version)
{
    const bool waits = state.transportPlaying ? settlesByItself (version ? state.versionStep : state.checkStep)
                                              : (version ? state.versionArmable : state.checkArmable);
    return state.separateComparisons && state.libraryReceived && waits
        && state.osAccess != os_access::State::unowned && !isBlindSession (state.blindPhase);
}

// B（REF）も止まっているあいだに押せば、再生で鳴る。再生中は準備が自動で進む段階なら待たせる。
inline bool canQueueReference (const State& state)
{
    const bool waits = state.transportPlaying ? settlesByItself (state.referenceStep) : state.referenceArmable;
    return state.separateComparisons && state.libraryReceived && waits
        && state.osAccess != os_access::State::unowned && !isBlindSession (state.blindPhase);
}

inline juce::String pendingAuditionReason (const State& state)
{
    using Stage = reference_audition::PendingAuditionView::Stage;
    switch (state.pendingAudition.stage)
    {
        case Stage::play: return "PLAY DAW";
        case Stage::checking:
        {
            const auto step = state.pendingAudition.slot == 1 ? state.versionStep
                            : state.pendingAudition.slot == 3 ? state.referenceStep : state.checkStep;
            return step == SourceStep::ready ? "VERIFYING PLAYBACK" : stepText (step);
        }
        case Stage::level: return "MEASURING A LEVEL";
        case Stage::sourceChanged: return "SOURCE CHANGED";
        case Stage::safetyChanged: return "PLAYBACK CHANGED";
        case Stage::startFailed: return "SAFE SWITCH UNAVAILABLE";
        case Stage::sourceLevelUnavailable: return "PREPARE SOURCE LEVEL IN KIRIN OS";
        case Stage::ceilingExceeded: return "MATCH EXCEEDS SAFE LEVEL";
        case Stage::none: break;
    }
    return {};
}

inline juce::String pendingAuditionHeading (const State& state)
{
    return juce::String (roleLetter (state.pendingAudition.slot))
        + (state.pendingAudition.waiting() ? " WAIT" : " STOPPED");
}

inline juce::String pendingAuditionText (const State& state)
{
    if (state.pendingAudition.stage == reference_audition::PendingAuditionView::Stage::none) return {};
    return pendingAuditionHeading (state) + " / " + pendingAuditionReason (state)
        + (state.pendingAudition.waiting() ? "" : " / SELECT AGAIN");
}
}
