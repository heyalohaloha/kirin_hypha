#pragma once

#include "../src/HyphaReferenceComponent.h"

#include <vector>

// The REFERENCE page in each state met on the way to B and C, with the status and the B and C
// steps the editor derives for it (PluginEditorReference.cpp, HyphaReferenceRuntimeView.h). The
// guide's contract and its look review both use these.
namespace hypha::tests::reference_review
{
struct Case
{
    const char* name;
    reference_ui::State state;
    bool access = false;
};

inline reference_ui::State library()
{
    reference_ui::State state;
    state.separateComparisons = true;
    state.osAccess = os_access::State::ready;
    state.osOnline = state.libraryReceived = true;
    state.readiness = reference_ui::Readiness::ready;
    state.comparisonSlot = 2;
    state.title = "Low End";
    state.alignmentLabel = "REFERENCE CUE";
    state.versions = { { "v4", "Mix v4" }, { "v3", "Mix v3" }, { "v2", "Mix v2" } };
    state.presets = { { "preset-a", "Quick Reference" }, { "preset-b", "Mastering" } };
    state.presetId = "preset-a";
    state.presetName = "Quick Reference";
    state.checks = { { "low/ref-a", "Low end  /  Ref Song A" }, { "dyn/ref-a", "Dynamics  /  Ref Song A" } };
    state.checkId = "low/ref-a";
    state.checkLabel = "Low end";
    state.cues = { { "cue-a", "Full track" }, { "cue-b", "Chorus" } };
    state.cueId = "cue-a";
    state.cueLabel = "Full track";
    return state;
}

inline reference_ui::State playing (reference_ui::State state)
{
    state.aAvailable = true;
    state.auditionBuffered = true;
    state.aIntegratedLoudness = -14.3;
    state.aMaximumTruePeakDbtp = -1.2;
    return state;
}

inline std::vector<Case> cases()
{
    using Step = reference_ui::SourceStep;
    std::vector<Case> result;
    {
        Case unowned { "unowned", {}, true };
        unowned.state.separateComparisons = true;
        unowned.state.status = "REF REQUIRES KIRIN OS";
        result.push_back (unowned);
    }
    {
        // Licensed, but no Reference library has arrived: Kirin OS is closed or too old.
        reference_ui::State state;
        state.separateComparisons = true;
        state.osAccess = os_access::State::ownedDisconnected;
        state.readiness = reference_ui::Readiness::waiting;
        state.status = "WAITING FOR KIRIN OS REFERENCE";
        result.push_back ({ "no_library", state });
        state.osOnline = true;
        result.push_back ({ "receiving", state });
    }
    {
        auto state = library();
        state.auditionBuffered = false;
        state.versionStep = Step::chooseVersion;
        state.checkStep = Step::ready;
        state.status = "PLAY A TO AUDITION";
        result.push_back ({ "stopped", state });
        state.versionId = "v4";
        state.versionStep = Step::ready;
        result.push_back ({ "stopped_chosen", state });
    }
    {
        auto state = playing (library());
        state.checkReady = true;
        state.versionStep = Step::chooseVersion;
        state.checkStep = Step::ready;
        state.status = "READY / A REMAINS LIVE";
        result.push_back ({ "no_version", state });
    }
    {
        auto state = playing (library());
        state.checkReady = true;
        state.versionId = "v4";
        state.comparisonSlot = 1;
        state.title = "Mix v4";
        state.readiness = reference_ui::Readiness::waiting;
        state.osAccess = os_access::State::connectedUnprepared;
        state.versionStep = Step::aligning;
        state.checkStep = Step::ready;
        state.status = "PLAY A / ALIGNING VERSION B";
        result.push_back ({ "aligning", state });
    }
    {
        auto state = playing (library());
        state.checks.clear();
        state.checkId.clear();
        state.readiness = reference_ui::Readiness::waiting;
        state.osAccess = os_access::State::connectedUnprepared;
        state.versionStep = Step::chooseVersion;
        state.checkStep = Step::enableCheck;
        state.status = "ENABLE A CHECK IN KIRIN OS";
        result.push_back ({ "no_check", state });
    }
    {
        auto state = playing (library());
        state.checkReady = state.versionReady = true;
        state.versionId = "v4";
        state.versionStep = state.checkStep = Step::ready;
        state.blindPhase = reference_ui::BlindPhase::available;
        state.status = "READY / A REMAINS LIVE";
        result.push_back ({ "ready", state });
    }
    return result;
}
}
