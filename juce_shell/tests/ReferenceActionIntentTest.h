#pragma once

#include "../src/HyphaReferenceAction.h"

#include <cstdlib>
#include <iostream>

// REF のアクション：A を下げる承認の申し出は、見ているページの役の MATCH の結果から作る（別の役の申し出を、その
// ページのボタンで承認しない）。OPEN REFERENCE は Kirin OS の Preset（C）を指すときだけ出す。
namespace hypha::tests
{
inline void verifyReferenceActionIntent()
{
    const auto check = [] (bool condition, const char* what)
    {
        if (condition) return;
        std::cerr << "Reference action intent failed: " << what << '\n';
        std::exit (EXIT_FAILURE);
    };
    using namespace reference_audition;
    Snapshot runtime;
    auto v = std::make_shared<Snapshot>(), c = std::make_shared<Snapshot>(), b = std::make_shared<Snapshot>();
    runtime.versionSelection = v; runtime.checkSelection = c; runtime.referenceSelection = b;
    b->matchFailure = MatchFailure::ceilingExceeded; b->neededAttenuationDb = -8.0; b->playbackIdentity = "b-song";
    b->selectionGeneration = 4; b->matchFailureSerial = 7;
    const auto onB = reference_ui::lowerAOfferFor (runtime, 3);
    check (onB && onB->slot == 3 && onB->db < -7.9 && onB->playbackIdentity == "b-song" && onB->selectionGeneration == 4
               && onB->failureSerial == 7,
           "B's page offers B's own approval, bound to its source, selection and failure");
    check (! reference_ui::lowerAOfferFor (runtime, 1) && ! reference_ui::lowerAOfferFor (runtime, 2),
           "B's offer never appears on the V or C page");
    runtime.bSelected = true; runtime.audibleComparisonSlot = 2;
    check (! reference_ui::lowerAOfferFor (runtime, 3), "no offer while another role sounds");
    runtime.bSelected = false;
    b->matchFailure = MatchFailure::none;
    check (! reference_ui::lowerAOfferFor (runtime, 3), "a later MATCH that fits withdraws the offer");

    runtime.libraryReceived = true; runtime.presetId = "preset"; runtime.state = RuntimeState::waiting;
    runtime.comparisonSlot = 2;
    check (reference_ui::opensKirinOsPreset (runtime), "C waiting on its Kirin OS Preset can open it");
    runtime.rejectionCode = "reference_selection_unavailable";
    check (! reference_ui::opensKirinOsPreset (runtime), "a saved choice that is gone does not open Kirin OS");
    runtime.rejectionCode = "reference_alignment_waiting_for_content";
    runtime.comparisonSlot = 1;
    check (! reference_ui::opensKirinOsPreset (runtime), "V's ordinary wait does not open Kirin OS");
    runtime.comparisonSlot = 3;
    check (! reference_ui::opensKirinOsPreset (runtime), "a B song does not open Kirin OS");
}
}
