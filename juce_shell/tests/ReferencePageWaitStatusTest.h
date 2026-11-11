#pragma once
#include "../src/HyphaReferenceRuntimeStatus.h"
#include "../src/HyphaReferenceRuntimeView.h"
#include "../src/HyphaReferenceStatusModel.h"
#include "../src/HyphaReferencePendingUI.h"
#include "ReferenceControlLookup.h"
#include <stdexcept>

namespace hypha::tests
{
inline void verifyReferencePageWaitStatus()
{
    reference_audition::Snapshot runtime;
    runtime.state = reference_audition::RuntimeState::ready;
    runtime.bSelected = true;
    runtime.sourceKind = "work_version";
    runtime.comparisonSlot = runtime.audibleComparisonSlot = 1;
    reference_ui::State ui;
    ui.osAccess = os_access::State::ready;
    ui.aAvailable = true;
    ui.auditionBuffered = runtime.auditionBuffered = false;
    const auto waiting = reference_ui::runtimeStatus (runtime, ui).status;
    if (!waiting.containsIgnoreCase ("loading") || !waiting.contains ("V")
        || waiting.contains ("AUDITION"))
        throw std::runtime_error ("a selected Version waiting for pages must show loading, not audible output");
    for (const bool selectedRole : { false, true })
    {
        runtime.bSelected = selectedRole;
        runtime.auditionPages = reference_audition::AuditionPageState::outsideSource;
        if (reference_ui::runtimeStatus (runtime, ui).status.containsIgnoreCase ("loading"))
            throw std::runtime_error ("an out-of-range Version reports its range rather than loading");
        if (reference_ui::runtime_view::slotStep (runtime, true) != reference_ui::SourceStep::outsideVersion)
            throw std::runtime_error ("an out-of-range Version keeps its range step");
    }
    runtime.bSelected = true;
    runtime.auditionPages = reference_audition::AuditionPageState::sourceUnavailable;
    if (!reference_ui::runtimeStatus (runtime, ui).status.contains ("RETRYING"))
        throw std::runtime_error ("a sustained source outage reports automatic retry");
    runtime.auditionPages = reference_audition::AuditionPageState::waiting;
    ui.auditionBuffered = runtime.auditionBuffered = true;
    if (!reference_ui::runtimeStatus (runtime, ui).status.contains ("AUDITION"))
        throw std::runtime_error ("the selected Version reports audition once pages are ready");
    // Inspecting another ready role must not hide the selected role's page wait.
    runtime.separateComparisons = true;
    auto selected = std::make_shared<reference_audition::Snapshot>();
    runtime.versionSelection = runtime.checkSelection = runtime.referenceSelection = selected;
    for (const int slot : { 1, 2, 3 })
    {
        runtime.audibleComparisonSlot = slot;
        selected->auditionBuffered = false;
        if (!reference_ui::runtimeStatus (runtime, ui).status.containsIgnoreCase ("loading"))
            throw std::runtime_error ("waiting status follows the selected role across views");
        selected->auditionBuffered = true;
        if (!reference_ui::runtimeStatus (runtime, ui).status.contains ("AUDITION"))
            throw std::runtime_error ("ready status follows the selected role across views");
    }
    // Feed each role's runtime stage through the final status row and the actual buttons.
    for (const int slot : { 1, 2, 3 })
    for (const int viewed : { 1, 2, 3 })
    {
        selected->state = reference_audition::RuntimeState::ready;
        selected->sourceKind = "work_version";
        selected->libraryReceived = true;
        runtime.libraryReceived = true;
        selected->presetId = "p"; selected->checkId = "c"; selected->candidateId = "s";
        runtime.selectedVersionId = "p/c/s";
        reference_audition::RuntimeSelectionOption option; option.id = "p/c/s"; option.label = "Version";
        runtime.versions = { option };
        reference_audition::RuntimeSongSetOption set; set.id = "set"; set.name = "Set"; set.rank = 1;
        runtime.songSets = { set };
        runtime.audibleComparisonSlot = slot; runtime.comparisonSlot = viewed;
        ui.separateComparisons = ui.libraryReceived = ui.transportPlaying = ui.blindLargeScreen = true;
        ui.comparisonSlot = viewed; ui.audibleComparisonSlot = slot;
        ui.tracking = reference_audition::TrackingState::fixed;
        ui.bSelected = true;
        const auto update = [&] {
            reference_ui::runtime_view::setSourceSteps (ui, runtime);
            ui.referenceStep = reference_ui::runtime_view::referenceStep (runtime, *selected, true);
            ui.status = reference_ui::runtimeStatus (runtime, ui).status;
            ui.audibleHeard = selected->auditionBuffered;  // 編集画面と同じ：鳴らしている役の音が届いているか
        };
        selected->auditionBuffered = false;
        selected->auditionPages = reference_audition::AuditionPageState::sourceUnavailable;
        update();
        const auto retry = reference_ui::referenceStatusLine (ui);
        if (retry.kind != reference_ui::StatusKind::waiting || ! retry.text.contains ("RETRYING"))
            throw std::runtime_error ("the final status row prioritizes automatic source recovery over MATCHED AND FIXED");
        for (const auto* code : { "reference_source_changed", "reference_source_audio_mismatch", "reference_source_decode_failed" })
        {
            selected->sourceFailureReason = code; update();
            const auto confirmed = reference_ui::referenceStatusLine (ui);
            if (confirmed.kind != reference_ui::StatusKind::unable || confirmed.text.contains ("RETRYING")
                || !confirmed.text.contains ("KIRIN OS"))
                throw std::runtime_error ("confirmed source changes and format errors reach the final row with the repair action");
        }
        selected->sourceFailureReason.clear(); update();
        ui.bSelected = false;
        reference_ui::Component panel;
        panel.setSize (900, 470);
        panel.setState (ui);
        int requested = 0, explained = 0;
        panel.onSelectB = [&] { requested = 1; };
        panel.onSelectC = [&] { requested = 2; };
        panel.onSelectRef = [&] { requested = 3; };
        panel.onExplain = [&] (const juce::String&) { ++explained; };
        auto* button = dynamic_cast<juce::Button*> (findReferenceControl (panel,
            slot == 1 ? "reference-b" : slot == 2 ? "reference-c" : "reference-ref"));
        if (!button) throw std::runtime_error ("missing real source button");
        button->onClick();
        if (requested != slot || explained != 0)
            throw std::runtime_error ("a real source button queues the choice while the monitor retries");
        ui.bSelected = true;
        selected->auditionPages = reference_audition::AuditionPageState::outsideSource;
        update();
        if (!reference_ui::referenceStatusLine (ui).text.contains ("OUTSIDE"))
            throw std::runtime_error ("the final status row prioritizes range over audition text");
        selected->auditionPages = reference_audition::AuditionPageState::readFailed;
        update();
        if (reference_ui::referenceStatusLine (ui).kind != reference_ui::StatusKind::unable
            || reference_ui::canQueueSource (ui, true) || reference_ui::canQueueSource (ui, false)
            || reference_ui::canQueueReference (ui))
            throw std::runtime_error ("a confirmed decoder failure remains distinct from automatic source recovery");
        selected->auditionPages = reference_audition::AuditionPageState::ready;
        selected->auditionBuffered = true; update();
        if (!reference_ui::referenceStatusLine (ui).text.contains ("MATCHED AND FIXED"))
            throw std::runtime_error ("the final status row clears the notice when the same source returns");
        selected->pendingSourceFailure = "reference_source_revalidating";
        update();
        const auto nextSource = reference_ui::referenceStatusLine (ui);
        // 2026-11-11：見ている画面と鳴っている役が違えば、その 2 つを先に言う。
        const auto page = viewed == slot ? juce::String() : juce::String (reference_ui::roleLetter (viewed))
            + juce::String::fromUTF8 (u8" PAGE · ") + reference_ui::roleLetter (slot) + " PLAYING  /  ";
        if (nextSource.kind != reference_ui::StatusKind::waiting
            || nextSource.text != page + "NEXT SOURCE UNAVAILABLE / RETRYING")
            throw std::runtime_error ("the final status row reports a failed requested source while the previous source plays");
        for (const auto* code : { "reference_source_cache_full", "reference_source_copy_failed" })
        {
            selected->pendingSourceFailure = code; update();
            const auto storage = reference_ui::referenceStatusLine (ui);
            if (storage.kind != reference_ui::StatusKind::waiting || storage.text.contains ("SOURCE UNAVAILABLE")
                || storage.text != page + reference_ui::runtime_view::rejectedStatus (code))
                throw std::runtime_error ("temporary storage failures reach the final row without blaming the source");
            auto failed = *selected; failed.state = reference_audition::RuntimeState::waiting; failed.rejectionCode = code;
            if (reference_ui::runtime_view::slotStep (failed, true)
                != reference_ui::runtime_view::rejectedStep (code))
                throw std::runtime_error ("initial storage failure has the same recoverable stage");
            const auto step = reference_ui::runtime_view::slotStep (failed, true);
            for (const bool playing : { false, true })
            {
                auto queued = ui; queued.bSelected = false; queued.transportPlaying = playing;
                queued.versionArmable = queued.checkArmable = queued.referenceArmable = false;
                queued.versionStep = queued.checkStep = queued.referenceStep = step;
                panel.setState (queued); requested = explained = 0;
                button->onClick();
                if (requested != slot || explained != 0)
                    throw std::runtime_error ("each real source button queues initial storage recovery even while stopped and without a playback identity");
            }
        }
        for (const auto* code : { "reference_source_changed", "reference_source_audio_mismatch", "reference_source_decode_failed" })
        {
            selected->pendingSourceFailure = code; update();
            const auto changed = reference_ui::referenceStatusLine (ui);
            if (changed.kind != reference_ui::StatusKind::unable || !changed.text.contains ("KIRIN OS")
                || changed.text.contains ("RETRYING"))
                throw std::runtime_error ("confirmed changes of the next source reach the final row while the current choice remains selected");
        }
        selected->pendingSourceFailure.clear();
        for (const auto* code : { "reference_source_cache_full", "reference_source_copy_failed" })
        {
            selected->sourceStorageNotice = code; update();
            const auto available = reference_ui::referenceStatusLine (ui);
            if (available.kind != reference_ui::StatusKind::ready || !available.text.contains ("MATCHED AND FIXED")
                || !available.text.contains ("REFERENCE AVAILABLE"))
                throw std::runtime_error ("storage fallback is information alongside the audible role and match state");
        }
        // 保存先の知らせが文を替えても、鳴らしている役の音が届いていない（ページの不足）間は A が鳴っていると言う。
        if (viewed != slot)
        {
            selected->auditionBuffered = false; update();
            if (! reference_ui::referenceStatusLine (ui).text.startsWith (juce::String (reference_ui::roleLetter (viewed))
                    + juce::String::fromUTF8 (u8" PAGE \u00b7 A PLAYING")))
                throw std::runtime_error ("a storage notice does not hide that A plays while the playing role's pages starve");
            selected->auditionBuffered = true; update();
        }
        selected->sourceStorageNotice.clear();
        selected->pendingSourceFailure.clear(); update();
        if (!reference_ui::referenceStatusLine (ui).text.contains ("MATCHED AND FIXED"))
            throw std::runtime_error ("the final status row clears the requested-source notice after recovery");
    }
    // Blind output can fall back to A with activeStimulus == 0 while its choice is held.
    // Read the actual Blind role's page status even when another role is inspected.
    for (const auto phase : { reference_audition::BlindPhase::starting, reference_audition::BlindPhase::active })
    for (const bool playing : { false, true })
    for (const int viewed : { 1, 2, 3 })
    {
        runtime.blindPhase = phase;
        ui.blindPhase = phase == reference_audition::BlindPhase::starting
            ? reference_ui::BlindPhase::starting : reference_ui::BlindPhase::active;
        runtime.transportPlaying = ui.transportPlaying = playing;
        runtime.comparisonSlot = ui.comparisonSlot = viewed;
        runtime.bSelected = ui.bSelected = false;
        runtime.activeBlindStimulus = 0;
        runtime.auditionPages = reference_audition::AuditionPageState::ready;
        selected->auditionPages = reference_audition::AuditionPageState::sourceUnavailable;
        ui.status = reference_ui::runtimeStatus (runtime, ui).status;
        const auto retry = reference_ui::referenceStatusLine (ui);
        if (retry.kind != reference_ui::StatusKind::waiting || retry.text != "SOURCE UNAVAILABLE / RETRYING")
            throw std::runtime_error ("Blind source recovery reaches the final row without revealing the hidden source or blaming position");
        selected->auditionPages = reference_audition::AuditionPageState::ready;
        runtime.activeBlindStimulus = 1;
        ui.status = reference_ui::runtimeStatus (runtime, ui).status;
        if (reference_ui::referenceStatusLine (ui).text.contains ("RETRYING"))
            throw std::runtime_error ("the Blind source notice clears automatically after recovery");
    }
}
}
