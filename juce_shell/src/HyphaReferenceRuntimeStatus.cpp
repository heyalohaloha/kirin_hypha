#include "HyphaReferenceRuntimeStatus.h"

#include "HyphaReferenceRuntimeView.h"

namespace hypha::reference_ui
{
RuntimeStatus runtimeStatus (const reference_audition::Snapshot& runtime, const State& state)
{
    RuntimeStatus result;
    using Runtime = hypha::reference_audition::RuntimeState;
    using Access = hypha::os_access::State;
    if (runtime.blindPhase == hypha::reference_audition::BlindPhase::invalidated)
        result.status = runtime.blindRequiredAAttenuationDb > 0.0
            ? "BLIND STOPPED / A HELD -"
                + juce::String (runtime.blindRequiredAAttenuationDb, 1)
                + " dB / RETURN A EXPLICITLY"
            : "BLIND STOPPED / RETURN A EXPLICITLY";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::active && !runtime.transportPlaying)
        result.status = "PAUSED / PLAY TO RESUME BLIND";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::active
        && runtime.activeBlindStimulus == 0)
        result.status = "PLAY WITHIN THE SONG / A REMAINS LIVE";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::starting)
        result.status = "BLIND / WAITING FOR FIRST AUDIBLE BLOCK";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::active)
        result.status = runtime.blindRequiredAAttenuationDb > 0.0
            ? "BLIND / A LOWERED "
                + juce::String (runtime.blindRequiredAAttenuationDb, 1)
                + " dB / RETURNS ON END"
            : "BLIND / SOURCE IDENTITY HIDDEN";
    else if (runtime.blindPhase == hypha::reference_audition::BlindPhase::revealed)
        result.status = "BLIND / REVEALED";
    else if (runtime.bSelected)
        result.status = juce::String (hypha::reference_ui::roleLetter (runtime.audibleComparisonSlot)) + " AUDITION / PRE DELTA PAUSED";
    else if (state.osAccess == Access::unowned)
        result.status = "REF REQUIRES KIRIN OS";
    else if (state.osAccess == Access::ownedDisconnected)
        result.status = "WAITING FOR KIRIN OS REFERENCE";
    else if (runtime.state == Runtime::ready)
        result.status = state.auditionBuffered ? "READY / A REMAINS LIVE"
            : runtime.auditionOutsideCue ? "OUTSIDE " + juce::String (hypha::reference_ui::roleLetter (runtime.comparisonSlot))
                + " CUE / MOVE OR CHOOSE LONGER CUE"
            : state.aAvailable ? "LOADING " + juce::String (hypha::reference_ui::roleLetter (runtime.comparisonSlot))
                + " AT PLAYHEAD / KEEP PLAYING" : "PLAY A TO AUDITION";
    else if (runtime.rejectionCode == "reference_selection_unavailable")
        result.status = "SAVED CHOICE UNAVAILABLE / CHOOSE AGAIN";
    else if (runtime.state == Runtime::verifying)
        result.status = "VERIFYING SOURCE";
    else if (runtime.state == Runtime::rejected)
        result.status = runtime_view::rejectedStatus (runtime.rejectionCode);
    else if (runtime.state == Runtime::waiting)
        result.status = runtime.rejectionCode == "reference_version_unselected" ? "CHOOSE VERSION V"
            : runtime.rejectionCode == "reference_alignment_waiting_for_content" ? "PLAY A / ALIGNING VERSION V"
            : runtime.rejectionCode == "reference_alignment_no_match" ? "NO VERIFIED MATCH / CHECK VERSION V"
            : runtime.rejectionCode == "reference_alignment_ambiguous" ? "PLAY ANOTHER PASSAGE TO ALIGN V"
            : runtime.rejectionCode == "reference_candidates_empty" ? "CHOOSE A SOURCE IN KIRIN OS"
            : runtime.rejectionCode == "reference_checks_empty" ? "ENABLE A CHECK IN KIRIN OS"
            : runtime.rejectionCode == "reference_source_unavailable" ? "SOURCE UNAVAILABLE / OPEN KIRIN OS"
            : "RECEIVING REFERENCE";
    else
        result.status = "OPEN KIRIN OS";
    // ボタンの文と押したときの意図は act だけが一緒に決める（押したときは state.action だけを実行する）。
    const auto act = [&result] (ActionKind kind, const juce::String& text) { result.action = { kind, {} }; result.actionText = text; };
    if (runtime.presetSelectionStatus == "pending")
    {
        result.request = true;
        result.status = "KIRIN OS PREPARING CHECK PRESET / A REMAINS LIVE";
        act (ActionKind::none, {});
    }
    else if (runtime.presetSelectionStatus == "prepared")
    {
        result.request = true;
        result.status = "CHECK PRESET READY / A REMAINS LIVE";
        act (ActionKind::none, {});
    }
    else if (runtime.presetSelectionStatus == "timed_out")
    {
        result.request = true;
        result.status = "KIRIN OS NEEDS MORE TIME / A REMAINS LIVE";
        act (ActionKind::retryPresetPreparation, "RETRY PREPARATION");
    }
    else if (runtime.presetSelectionStatus == "preset_setup_required")
    {
        result.request = true;
        result.status = "CHOOSE A REFERENCE IN KIRIN OS / A REMAINS LIVE";
        act (ActionKind::openReference, "OPEN REFERENCE");
    }
    else if (runtime.presetSelectionStatus == "source_unavailable")
    {
        result.request = true;
        result.status = "REFERENCE SOURCE NEEDS ATTENTION / A REMAINS LIVE";
        act (ActionKind::chooseSource, "CHOOSE SOURCE");
    }
    else if (runtime.presetSelectionStatus == "measurement_required")
    {
        result.request = true;
        result.status = "MEASURE THE REFERENCE SOURCE IN KIRIN OS / A REMAINS LIVE";
        act (ActionKind::measureSource, "MEASURE SOURCE");
    }
    else if (runtime.presetSelectionStatus == "request_stale"
             || runtime.presetSelectionStatus == "storage_unavailable"
             || runtime.presetSelectionStatus == "publication_failed")
    {
        result.request = true;
        result.status = "CHECK PRESET NOT REFRESHED / A REMAINS LIVE";
        act (ActionKind::retryPresetPreparation, "RETRY PREPARATION");
    }
    else if (runtime.presetSelectionStatus == "work_unavailable"
             || runtime.presetSelectionStatus == "request_invalid")
    {
        result.request = true;
        result.status = "REFERENCE SETUP NEEDS ATTENTION / A REMAINS LIVE";
        act (ActionKind::openReference, "OPEN REFERENCE");
    }
    else if (runtime.candidatePreparationStatus == "pending")
    {
        result.request = true;
        result.status = "KIRIN OS PREPARING REFERENCE / A REMAINS LIVE";
        act (ActionKind::none, {});
    }
    else if (runtime.candidatePreparationStatus == "prepared")
    {
        result.request = true;
        result.status = "REFERENCE READY / A REMAINS LIVE";
        act (ActionKind::none, {});
    }
    else if (runtime.candidatePreparationStatus == "timed_out")
    {
        result.request = true;
        result.status = "KIRIN OS NEEDS MORE TIME / A REMAINS LIVE";
        act (ActionKind::retryCandidatePreparation, "RETRY PREPARATION");
    }
    else if (runtime.candidatePreparationStatus == "source_unavailable")
    {
        result.request = true;
        result.status = "REFERENCE SOURCE NEEDS ATTENTION / A REMAINS LIVE";
        act (ActionKind::chooseSource, "CHOOSE SOURCE");
    }
    else if (runtime.candidatePreparationStatus == "measurement_required")
    {
        result.request = true;
        result.status = "MEASURE THE REFERENCE SOURCE IN KIRIN OS / A REMAINS LIVE";
        act (ActionKind::measureSource, "MEASURE SOURCE");
    }
    else if (runtime.candidatePreparationStatus == "request_stale"
             || runtime.candidatePreparationStatus == "storage_unavailable"
             || runtime.candidatePreparationStatus == "publication_failed")
    {
        result.request = true;
        result.status = "REFERENCE NOT REFRESHED / A REMAINS LIVE";
        act (ActionKind::retryCandidatePreparation, "RETRY PREPARATION");
    }
    else if (runtime.candidatePreparationStatus.isNotEmpty())
    {
        result.request = true;
        result.status = "REFERENCE SETUP NEEDS ATTENTION / A REMAINS LIVE";
        act (ActionKind::openReference, "OPEN REFERENCE");
    }
    else if (runtime.recoveryStatus == "pending")
    {
        result.request = true;
        result.status = "OPENING REFERENCE IN KIRIN OS";
        act (ActionKind::none, {});
    }
    else if (runtime.recoveryStatus == "opened")
    {
        result.request = true;
        result.status = "CONTINUE IN KIRIN OS";
        act (ActionKind::none, {});
    }
    else if (runtime.recoveryStatus == "exact_opened")
    {
        result.request = true;
        result.status = "KIRIN OS OPENED THE REFERENCE LOCATION";
        act (ActionKind::none, {});
    }
    else if (runtime.recoveryStatus == "safe_fallback_opened")
    {
        result.request = true;
        result.status = "KIRIN OS OPENED SAFE REFERENCE SETTINGS";
        act (ActionKind::none, {});
    }
    else if (runtime.recoveryStatus == "rejected")
    {
        result.request = true;
        result.status = "REFERENCE ITEM IS NO LONGER AVAILABLE";
        act (ActionKind::retryKirinOs, "TRY KIRIN OS AGAIN");
    }
    else if (runtime.recoveryStatus == "timed_out")
    {
        result.request = true;
        result.status = "KIRIN OS DID NOT RESPOND / A REMAINS LIVE";
        act (ActionKind::retryKirinOs, "TRY KIRIN OS AGAIN");
    }
    else if (state.blindLowerAApprovalRequired)
    {
        const auto attenuation = juce::String (state.blindRequiredAAttenuationDb, 1);
        result.request = true;
        result.status = "BLIND NEEDS HEADROOM / A RETURNS +" + attenuation + " dB ON END";
        act (state.blindLargeScreen ? ActionKind::approveBlindLowerA : ActionKind::none,
             state.blindLargeScreen ? "LOWER A " + attenuation + " dB & START" : juce::String {});
    }
    else if (hypha::reference_ui::opensKirinOsPreset (runtime))
        act (ActionKind::openReference, "OPEN REFERENCE");
    return result;
}
}
