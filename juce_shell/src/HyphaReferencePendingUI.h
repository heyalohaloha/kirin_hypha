#pragma once
#include "HyphaReferenceComponent.h"
#include "HyphaReferenceStages.h"

namespace hypha::reference_ui
{
// 再生中でも、準備が自動で進む段階（確認・読み込み・準備・位置合わせ）なら押した役を待たせ、準備でき次第鳴らす
// （押したことを捨てない。待ちの上限は HyphaReferencePreparationWatch.h が見張る）。
inline bool settlesByItself (SourceStep step) noexcept
{
    return automaticStage (step) && step != SourceStep::waitingForKirinOs;  // 段階の表で上限のある段階
}

inline bool canQueueSource (const State& state, bool version)
{
    const bool waits = state.transportPlaying ? (settlesByItself (version ? state.versionStep : state.checkStep)
                                                  || (version && state.versionStep == SourceStep::playAnotherPassage))
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
    if (state.pendingAudition.reasonCode == "reference_page_wait_slow")
        return "CHECKING";
    if (state.pendingAudition.reasonCode == "reference_audio_read_failed")
        return "AUDIO READ FAILED / CHECK THE FILE IN KIRIN OS";
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

inline juce::String versionStoppedText (const State& state)
{
    using Stage = reference_audition::PendingAuditionView::Stage;
    const auto& code = state.pendingAudition.reasonCode.isNotEmpty() ? state.pendingAudition.reasonCode : state.versionRejectionCode;
    juce::String reason, action = "PLAY A, THEN PRESS V";
    switch (state.pendingAudition.stage)
    {
        case Stage::sourceChanged:
            if (code == "reference_audio_read_failed")
            { reason = "AUDIO READ FAILED"; action = "CHECK THE FILE IN KIRIN OS"; }
            else if (code == "source_changed" || code == "reference_source_changed")
            { reason = "REFERENCE FILE CHANGED"; action = "PREPARE V AGAIN IN KIRIN OS"; }
            else if (code == "reference_selection_unavailable" || code == "reference_version_unselected"
                || code == "reference_candidates_empty" || code == "reference_checks_empty")
            { reason = "SELECTED VERSION UNAVAILABLE"; action = "CHOOSE ANOTHER VERSION"; }
            else if (code == "reference_alignment_source_unavailable")
            { reason = "REFERENCE FILE UNAVAILABLE"; action = "CHECK THE FILE IN KIRIN OS"; }
            else if (code.startsWith ("reference_alignment_"))
                reason = "POSITION NOT CONFIRMED";
            else if (code == "reference_selection_changed")
            { reason = "SELECTED VERSION CHANGED"; action = "CHECK THE VERSION, THEN PRESS V"; }
            else if (code == "reference_source_unavailable" || code == "source_open_failed" || code == "source_decode_failed"
                || code == "reference_source_open_failed" || code == "reference_source_decode_failed")
            { reason = "REFERENCE FILE UNAVAILABLE"; action = "CHECK THE FILE IN KIRIN OS"; }
            else if (code == "channel_layout_unsupported")
            { reason = "REFERENCE NEEDS A STEREO TRACK"; action = "USE A STEREO TRACK"; }
            else if (code == "reference_source_contract_rejected")
            { reason = "REFERENCE FORMAT NOT SUPPORTED"; action = "UPDATE KIRIN OS AND HYPHA"; }
            else if (code == "reference_source_audio_mismatch" || code == "reference_source_receipt_rejected"
                || code.startsWith ("reference_library_") || code.startsWith ("reference_manifest_")
                || code.startsWith ("reference_preset_") || code == "reference_global_preset_catalog_rejected")
            { reason = "REFERENCE DATA NOT VERIFIED"; action = "PREPARE V AGAIN IN KIRIN OS"; }
            else reason = "PLAYBACK NOT VERIFIED";
            break;
        case Stage::safetyChanged: reason = "PLAYBACK INTERRUPTED"; break;
        case Stage::sourceLevelUnavailable:
            reason = "V LEVEL NOT MEASURED"; action = "PREPARE V AGAIN IN KIRIN OS"; break;
        case Stage::ceilingExceeded:
            reason = "MATCH EXCEEDS SAFE LEVEL"; action = "USE LOWER A TO LISTEN"; break;
        case Stage::startFailed:
            if (code == "reference_position_outside_source")
            { reason = "PLAYHEAD OUTSIDE VERSION"; action = "MOVE INSIDE VERSION, THEN PRESS V"; }
            else reason = "CANNOT START SAFELY";
            break;
        case Stage::none: case Stage::play: case Stage::checking: case Stage::level: return {};
    }
    return "V STOPPED / " + reason + " / " + action;
}

inline juce::String pendingAuditionText (const State& state)
{
    if (state.pendingAudition.stage == reference_audition::PendingAuditionView::Stage::none) return {};
    if (state.pendingAudition.slot == 1 && !state.pendingAudition.waiting()) return versionStoppedText (state);
    return pendingAuditionHeading (state) + " / " + pendingAuditionReason (state)
        + (state.pendingAudition.waiting() ? "" : " / SELECT AGAIN");
}
}
