#pragma once

#include "HyphaReferenceComponent.h"
#include "reference_audition/ReferenceAuditionController.h"

// The Reference runtime's snapshot as the REFERENCE page states it: its readiness, source kind,
// rejections, options and Blind phase, and where B and C each stand (INV-S41).
namespace hypha::reference_ui::runtime_view
{
inline hypha::reference_ui::Readiness referenceReadiness (
    hypha::reference_audition::RuntimeState state) noexcept
{
    using Input = hypha::reference_audition::RuntimeState;
    using Output = hypha::reference_ui::Readiness;
    switch (state)
    {
        case Input::disconnected: return Output::disconnected;
        case Input::waiting:      return Output::waiting;
        case Input::verifying:    return Output::verifying;
        case Input::ready:        return Output::ready;
        case Input::rejected:     return Output::rejected;
    }
    return Output::disconnected;
}
inline juce::String sourceLabel (const juce::String& kind)
{
    if (kind == "work_version") return "WORK VERSION";
    if (kind == "catalog_track" || kind == "catalog") return "CATALOG";
    return "KIRIN OS";
}
inline juce::String rejectedStatus (const juce::String& code)
{
    if (code == "source_changed") return "SOURCE CHANGED / PREPARE AGAIN IN KIRIN OS";
    if (code == "source_open_failed" || code == "source_decode_failed"
        || code == "reference_source_open_failed"
        || code == "reference_source_decode_failed")
        return "SOURCE COULD NOT BE OPENED";
    if (code == "reference_source_changed") return "SOURCE CHANGED / VERIFY IN KIRIN OS";
    if (code == "reference_source_audio_mismatch")
        return "SOURCE FORMAT CHANGED / VERIFY IN KIRIN OS";
    return "PREPARE AGAIN IN KIRIN OS";
}
inline std::vector<hypha::reference_ui::SelectionOption> selectionOptions (
    const std::vector<hypha::reference_audition::RuntimeSelectionOption>& input)
{
    std::vector<hypha::reference_ui::SelectionOption> output;
    output.reserve (input.size());
    for (const auto& item : input) output.push_back ({
        item.id, item.label + (item.requiresPreparation ? "  /  PREPARE" : "") });
    return output;
}
inline hypha::reference_ui::BlindPhase referenceBlindPhase (
    hypha::reference_audition::BlindPhase phase,
    bool available) noexcept
{
    using Input = hypha::reference_audition::BlindPhase;
    using Output = hypha::reference_ui::BlindPhase;
    switch (phase)
    {
        case Input::active:      return Output::active;
        case Input::revealed:    return Output::revealed;
        case Input::invalidated: return Output::invalidated;
        case Input::starting:    return Output::starting;
        case Input::inactive:    return available ? Output::available : Output::unavailable;
    }
    return Output::unavailable;
}

// Where one slot stands from its own snapshot; `playing` is A as the page shows it.
inline SourceStep slotStep (const reference_audition::Snapshot& slot, bool playing)
{
    using Runtime = reference_audition::RuntimeState;
    const auto& code = slot.rejectionCode;
    if (slot.sampleRateApprovalRequired || code == "reference_sample_rate_approval_required")
        return SourceStep::approveSampleRate;
    switch (slot.state)
    {
        case Runtime::ready:
            return ! playing ? SourceStep::playDaw
                 : slot.auditionOutsideCue ? SourceStep::outsideCue
                 : slot.auditionBuffered ? SourceStep::ready : SourceStep::loadingAudio;
        case Runtime::verifying: return SourceStep::verifyingSource;
        case Runtime::rejected: return SourceStep::attention;
        case Runtime::waiting:
            if (code == "reference_alignment_waiting_for_content")
                return playing ? SourceStep::aligning : SourceStep::playDaw;
            if (code == "reference_alignment_no_match") return SourceStep::noMatchingPassage;
            if (code == "reference_alignment_ambiguous") return SourceStep::playAnotherPassage;
            if (code == "reference_checks_empty") return SourceStep::enableCheck;
            if (code == "reference_candidates_empty" || code == "reference_cues_empty")
                return SourceStep::chooseSource;
            if (code == "reference_source_unavailable" || code == "reference_selection_unavailable")
                return SourceStep::attention;
            return SourceStep::preparing;
        case Runtime::disconnected: break;
    }
    return SourceStep::waitingForKirinOs;
}

// B and C side by side: B needs a Version registered and chosen, and the chosen one applied.
inline void setSourceSteps (State& state, const reference_audition::Snapshot& comparison)
{
    const auto& version = comparison.versionSelection ? *comparison.versionSelection : comparison;
    const auto& check = comparison.checkSelection ? *comparison.checkSelection : comparison;
    const auto chosen = version.presetId + "/" + version.checkId + "/" + version.candidateId;
    state.versionStep = ! version.libraryReceived ? SourceStep::waitingForKirinOs
        : comparison.versions.empty() ? SourceStep::registerVersion
        : comparison.selectedVersionId.isEmpty() ? SourceStep::chooseVersion
        : comparison.selectedVersionId != chosen ? SourceStep::preparing
        : slotStep (version, state.aAvailable);
    state.checkStep = ! check.libraryReceived ? SourceStep::waitingForKirinOs
                                              : slotStep (check, state.aAvailable);
}

// Approval belongs to the source, never to whichever slot happens to own the detail pane.
// The action belongs to the displayed source. The other source still names its need on its row
// and button; inspecting it must not require starting audition first.
inline void setSampleRateApproval (State& state, const reference_audition::Snapshot& comparison)
{
    const bool versionPending = state.versionStep == SourceStep::approveSampleRate;
    const bool checkPending = state.checkStep == SourceStep::approveSampleRate;
    state.sampleRateApprovalSlot = comparison.comparisonSlot == 1 && versionPending ? 1
        : comparison.comparisonSlot == 2 && checkPending ? 2 : 0;
    state.sampleRateApprovalRequired = state.sampleRateApprovalSlot != 0;
    if (! state.sampleRateApprovalRequired) return;
    const auto& source = state.sampleRateApprovalSlot == 1
        ? (comparison.versionSelection ? *comparison.versionSelection : comparison)
        : (comparison.checkSelection ? *comparison.checkSelection : comparison);
    state.sourceSampleRateHz = source.sourceSampleRateHz;
    state.hostSampleRateHz = source.hostSampleRateHz;
}
}
