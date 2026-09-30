#pragma once

#include "../src/HyphaReferenceComponent.h"
#include "../src/HyphaReferencePendingUI.h"

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
    state.transportPlaying = true;
    state.auditionBuffered = true;
    state.aIntegratedLoudness = -14.3;
    state.aMaximumTruePeakDbtp = -1.2;
    return state;
}

inline std::vector<Case> cases()
{
    using Step = reference_ui::SourceStep;
    std::vector<Case> result;
    for (const auto slot : { 1, 2 })
    {
        auto state = library();
        state.versionId = "v4";
        state.versionArmable = state.checkArmable = true;
        state.versionStep = state.checkStep = Step::playDaw;
        state.pendingAudition = { slot, reference_audition::PendingAuditionView::Stage::play };
        state.status = reference_ui::pendingAuditionText (state);
        result.push_back ({ slot == 1 ? "queued_b" : "queued_c", state });
        state.pendingAudition.stage = reference_audition::PendingAuditionView::Stage::safetyChanged;
        state.status = reference_ui::pendingAuditionText (state);
        result.push_back ({ slot == 1 ? "queued_b_cancelled" : "queued_c_cancelled", state });
    }
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
        state.versionId = "v4";
        state.versionStep = Step::approveSampleRate;
        state.checkStep = Step::preparing;
        state.sampleRateApprovalRequired = true;
        state.sampleRateApprovalSlot = 1;
        state.sourceSampleRateHz = 44100;
        state.hostSampleRateHz = 48000;
        state.actionText = "APPROVE B 44.1 TO 48.0 kHz";
        result.push_back ({ "approve_b_rate", state });
        state.versionStep = Step::preparing;
        state.checkStep = Step::approveSampleRate;
        state.sampleRateApprovalSlot = 2;
        state.actionText = "APPROVE C 44.1 TO 48.0 kHz";
        result.push_back ({ "approve_c_rate", state });
        state.aAvailable = false;
        state.versionStep = Step::playDaw;
        result.push_back ({ "stopped_c_rate", state });
        state.aAvailable = true;
        state.versionReady = true;
        state.versionStep = Step::ready;
        state.bSelected = true;
        state.audibleComparisonSlot = 1;
        state.appliedGainDb = -2.5;
        state.viewBindings = { "spectrum_full", "spectrum_low" };
        // Synthetic, visibly distinct A and C curves for layout/color inspection only.
        auto measurement = std::make_shared<reference_audition::RuntimeDetailedMeasurement>();
        measurement->spectrum.emplace();
        for (int band = 0; band < 64; ++band)
        {
            const auto level = -22000 - band * 600 + juce::roundToInt (2000.0 * std::sin (band * 0.4));
            measurement->spectrum->bandCentersHz.push_back (20.0 * std::pow (1000.0, band / 63.0));
            measurement->spectrum->medianMillidbfs.push_back (level);
            measurement->spectrum->p10Millidbfs.push_back (level - 4000);
            measurement->spectrum->p90Millidbfs.push_back (level + 4000);
            state.liveSpectrumDbfs.push_back (static_cast<float> (level / 1000.0 + 5.0 * std::sin (band * 0.2)));
        }
        state.detailedMeasurement = measurement;
        state.liveSpectrumMinimumHz = 20; state.liveSpectrumMaximumHz = 20000;
        result.push_back ({ "b_audible_c_view", state });
    }
    {
        auto state = playing (library());
        state.versionId = "v4";
        state.versionStep = Step::loadingAudio;
        state.checkStep = Step::verifyingSource;
        state.status = "LOADING SOURCE AT PLAYHEAD / A REMAINS LIVE";
        result.push_back ({ "loading_audio", state });
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
