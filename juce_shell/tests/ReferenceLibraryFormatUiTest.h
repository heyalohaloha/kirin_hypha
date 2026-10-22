#pragma once
#include "../src/HyphaReferenceRuntimeView.h"
#include "../src/HyphaReferenceStatusModel.h"
#include <cstdlib>
#include <iostream>
namespace hypha::tests
{
inline void verifyReferenceLibraryFormatStatus()
{
    namespace view = reference_ui::runtime_view;
    const auto check = [] (bool ok, const char* message) {
        if (! ok) { std::cerr << "Library format UI: " << message << '\n'; std::exit (1); }
    };
    const auto option = [] (const juce::String& id, const juce::String& label) {
        reference_audition::RuntimeSelectionOption item; item.id = id; item.label = label; return item;
    };
    for (const auto* code : {"reference_library_rejected", "reference_manifest_rejected"})
        for (bool warm : {false, true})
            for (bool online : {false, true})
                for (int slot : {1, 2, 3})
                {
                    reference_audition::Snapshot runtime;
                    runtime.state = reference_audition::RuntimeState::rejected; runtime.rejectionCode = code;
                    runtime.libraryReceived = warm; runtime.osOnline = online;
                    if (warm) runtime.versions.push_back (option ("saved-version", "Saved Version"));
                    reference_ui::State state;
                    state.osAccess = os_access::State::ready; state.separateComparisons = true;
                    state.comparisonSlot = slot; state.aAvailable = true; state.readiness = view::referenceReadiness (runtime.state);
                    state.status = warm ? juce::String ("OPEN KIRIN OS") : view::rejectedStatus (code);
                    view::setSourceSteps (state, runtime);
                    state.referenceStep = view::referenceStep (runtime, runtime, true);
                    state.referenceRejectionCode = view::rejectionCodeOf (runtime);
                    const auto line = reference_ui::referenceStatusLine (state, true);
                    check (line.kind == reference_ui::StatusKind::unable && line.text.contains ("FORMAT UNREADABLE")
                        && line.text.contains ("UPDATE KIRIN OS") && line.text.contains ("PUBLISH AGAIN"),
                        "every role explains an unreadable cold/warm publication before opening or choosing a library");
                    const auto& reason = slot == 1 ? state.versionRejectionCode : slot == 2 ? state.checkRejectionCode : state.referenceRejectionCode;
                    check (reason == code, "the runtime rejection reaches every role");
                    // A valid subsequent publication must remove the prior rejection.
                    runtime.state = reference_audition::RuntimeState::ready; runtime.rejectionCode.clear(); runtime.libraryReceived = true;
                    runtime.presetId = "p"; runtime.checkId = "c"; runtime.candidateId = "s";
                    runtime.selectedVersionId = "p/c/s"; runtime.versions = {option ("p/c/s", "Version")}; runtime.auditionBuffered = true;
                    state.readiness = view::referenceReadiness (runtime.state); state.status.clear();
                    view::setSourceSteps (state, runtime);
                    state.referenceStep = view::referenceStep (runtime, runtime, true);
                    state.referenceRejectionCode = view::rejectionCodeOf (runtime);
                    check (state.versionRejectionCode.isEmpty() && state.checkRejectionCode.isEmpty() && state.referenceRejectionCode.isEmpty()
                        && ! reference_ui::referenceStatusLine (state, true).text.contains ("FORMAT UNREADABLE"), "a valid publication clears stale format reasons");
                }
    std::cout << "library format status: cold/warm, online/offline, every role and recovery PASS\n";
}
}
