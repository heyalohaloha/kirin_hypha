#pragma once
#include "HyphaReferenceComponent.h"
#include "reference_audition/ReferenceTargetCapture.h"

namespace hypha::reference_ui
{
inline void applyMeasuredTarget (State& state, reference_audition::TargetSnapshot targets, bool licensed)
{
    state.targetsEnabled = licensed;
    state.targets = std::move (targets);
    const auto* target = state.targets.collection.find (state.targets.collection.selected);
    state.targetSelected = licensed && target != nullptr && state.comparisonSlot == 2;
    if (! state.targetSelected) return;
    state.aKirin = state.visualTimeline ? state.visualTimeline->aKirin : nullptr;
    const auto levels = state.visualTimeline ? reference_audition::targetWindowLevels (*state.visualTimeline)
                                            : reference_audition::LiveWindowLoudness {};
    state.aWindowLoudness = state.aIntegratedLoudness = levels.lufs;
    state.aMaximumTruePeakDbtp = levels.maxTruePeak;
    state.aWindowBlocks = levels.blocks;
    state.aWindowNeededBlocks = 30;
    state.separateComparisons = true;
    state.title = target->name;
    state.checkLabel = "TARGET";
    state.candidateName = target->name;
    state.cueLabel.clear(); state.cues.clear();
    state.cueKirin = std::make_shared<const reference_audition::KirinSpectrumWindow> (target->spectrum);
    state.cueLoudness = target->integrated;
    // Spectrum/Balance average channel power; normalize the LUFS channel sum to A's layout.
    const auto channels = state.visualTimeline ? state.visualTimeline->aTickChannels : 0;
    state.cueLoudnessOffsetLu = channels >= 1 && channels <= 2
        ? 10.0 * std::log10 (double (channels) / target->channels) : 0.0;
    state.cuePart = reference_audition::CuePart::whole;
    state.sourceDurationSeconds = std::numeric_limits<double>::quiet_NaN();
    state.cueStartSeconds = state.cueEndSeconds = state.cuePlayheadSeconds = state.sourceDurationSeconds;
    state.cueMeasurement.reset(); state.detailedMeasurement.reset(); state.profiles.clear();
    state.viewBindings = { "spectrum_full" }; state.presentationLayout = "main";
    state.listeningChecks.clear();
    state.checkReady = state.checkArmable = false;
    state.comparisonMode = "loudness_match";
    state.cueSubstitute = CueSubstitute::none;
    state.adjustedBIntegratedLoudness = target->integrated + state.cueLoudnessOffsetLu;
    state.adjustedBMaximumTruePeakDbtp = target->maximumTruePeak;
    state.actionText.clear(); state.action = {}; state.status = "DATA ONLY"; state.kirinOsRequest = false;
    state.preparationOverdue.clear(); state.rolePreparation = {};
}
}
