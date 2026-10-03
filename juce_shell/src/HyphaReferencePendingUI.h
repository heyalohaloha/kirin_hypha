#pragma once
#include "HyphaReferenceComponent.h"

namespace hypha::reference_ui
{
inline bool canQueueSource (const State& state, bool version)
{
    return state.separateComparisons && state.libraryReceived && !state.transportPlaying
        && state.osAccess != os_access::State::unowned && !isBlindSession (state.blindPhase)
        && (version ? state.versionArmable : state.checkArmable)
        && (state.workflow.mode == reference_audition::WorkflowView::Mode::normal
            || state.workflow.status == reference_audition::WorkflowView::Status::resumeAvailable);
}

// H10: B（REF）も止まっているあいだに押せば、再生で鳴る。
inline bool canQueueReference (const State& state)
{
    return state.separateComparisons && state.libraryReceived && !state.transportPlaying
        && state.osAccess != os_access::State::unowned && !isBlindSession (state.blindPhase)
        && state.referenceArmable
        && (state.workflow.mode == reference_audition::WorkflowView::Mode::normal
            || state.workflow.status == reference_audition::WorkflowView::Status::resumeAvailable);
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
        case Stage::approval: return "APPROVE CONVERSION";
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
