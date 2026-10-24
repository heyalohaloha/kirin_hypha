#pragma once
#include "../src/HyphaReferenceRuntimeStatus.h"
#include "../src/HyphaReferenceRuntimeView.h"
#include <stdexcept>

namespace hypha::tests
{
inline void verifyReferencePageWaitStatus()
{
    reference_audition::Snapshot runtime;
    runtime.state = reference_audition::RuntimeState::ready;
    runtime.bSelected = true;
    runtime.comparisonSlot = runtime.audibleComparisonSlot = 1;
    reference_ui::State ui;
    ui.osAccess = os_access::State::ready;
    ui.aAvailable = true;
    ui.auditionBuffered = runtime.auditionBuffered = false;
    const auto waiting = reference_ui::runtimeStatus (runtime, ui).status;
    if (!waiting.containsIgnoreCase ("loading") || !waiting.contains ("V")
        || waiting.contains ("AUDITION"))
        throw std::runtime_error ("a selected Version waiting for pages must show loading, not audible output");
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
}
}
