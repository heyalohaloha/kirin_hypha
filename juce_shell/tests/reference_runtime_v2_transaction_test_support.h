#pragma once

#include "reference_runtime_v2_analysis_test_support.h"

#include <functional>

namespace
{
    void verifyPublicationChangeRevokesGain (
        ref::RuntimeV2Controller& controller,
        std::function<void()>& beforeGateActivation,
        std::atomic<bool>& comparisonSuspended,
        const juce::File& presetFile,
        const juce::File& manifestFile,
        juce::var measuredPreset,
        const juce::String& presetId,
        const juce::String& revisionId)
    {
        measuredPreset.getDynamicObject()->getProperty ("checks").getArray()
            ->getReference (0).getDynamicObject()->setProperty (
                "comparison_mode", "original");
        std::atomic<bool> replacementPublished { false };
        beforeGateActivation = [&]
        {
            replacementPublished.store (
                writeJson (presetFile, measuredPreset)
                && writeJson (manifestFile,
                              makeRuntimeV2Manifest (
                                  presetId, revisionId, presetFile, 6)));
            for (int attempt = 0; attempt < 400; ++attempt)
            {
                const auto state = controller.snapshot();
                if (state.manifestRevision == 6
                    && state.comparisonMode == "original")
                    break;
                juce::Thread::sleep (10);
            }
        };
        require (! controller.selectB (-11.0, -2.0),
                 "a B activation must fail when its approved publication changes inside the output gate");
        beforeGateActivation = {};
        auto runtime = controller.snapshot();
        require (replacementPublished.load()
                 && runtime.manifestRevision == 6
                 && runtime.comparisonMode == "original"
                 && ! runtime.bSelected && ! comparisonSuspended.load(),
                 "publication change must revoke both the stale gain and the temporary output gate");
        require (controller.selectB (-11.0, -2.0),
                 "the replacement original-mode publication must remain selectable");
        runtime = controller.snapshot();
        require (std::abs (runtime.appliedGainDb) < 1.0e-9,
                 "replacement original mode must not inherit the previous loudness-match gain");
        controller.selectA();
    }

    void verifyBlindSourceReplacementReturn (
        ref::RuntimeV2Controller& controller,
        std::function<void()>& beforeGateActivation,
        std::atomic<bool>& comparisonSuspended,
        const juce::File& v2Root,
        const juce::File& presetFile,
        const juce::File& manifestFile,
        const juce::File& alignedBFile,
        const juce::String& alignedBFileHash,
        const juce::String& alignedBPcmHash,
        const juce::String& recordingId,
        const juce::String& presetId,
        const juce::String& revisionId,
        std::int64_t alignedBFrames,
        std::int64_t hostPosition,
        juce::AudioBuffer<float>& output)
    {
        const auto initial = controller.snapshot();
        const auto heldAGain = std::pow (
            10.0f, static_cast<float> (-initial.blindRequiredAAttenuationDb / 20.0));
        const juce::String gateRaceVersionId =
            "cdcdcdcd-cdcd-4dcd-8dcd-cdcdcdcdcdcd";
        const auto gateRaceSourceReceipt = stageRuntimeV2Artifact (
            v2Root, "sources", makeRuntimeV2WorkVersionSource (
                alignedBFile, alignedBFileHash, alignedBPcmHash, recordingId,
                gateRaceVersionId, alignedBFrames));
        const auto gateRacePreset = bindRuntimeV2WorkVersionPresetToSource (
            presetId, revisionId, gateRaceSourceReceipt, alignedBFileHash,
            alignedBPcmHash, recordingId, gateRaceVersionId, alignedBFrames);
        std::atomic<bool> gateRacePublished { false };
        beforeGateActivation = [&]
        {
            gateRacePublished.store (
                writeJson (presetFile, gateRacePreset)
                && writeJson (manifestFile, makeRuntimeV2Manifest (
                    presetId, revisionId, presetFile, 8)));
            for (int attempt = 0; attempt < 400
                 && controller.snapshot().manifestRevision != 8; ++attempt)
                juce::Thread::sleep (10);
        };
        require (! controller.approveBlindLowerAAndStart (-14.0, -6.0),
                 "Blind must reject a start whose source epoch changes inside the output gate");
        beforeGateActivation = {};
        for (int attempt = 0; attempt < 400
             && ! controller.snapshot().blindEligible; ++attempt)
            juce::Thread::sleep (10);
        require (gateRacePublished.load() && controller.snapshot().blindEligible
                 && ! comparisonSuspended.load(),
                 "the rejected stale Blind start must return its gate and prepare only the new epoch");
        require (controller.approveBlindLowerAAndStart (-14.0, -6.0),
                 "explicit approval must start the current lower-A Blind context");
        output.clear();
        require (controller.renderSelectedB (output, hostPosition, true),
                 "the approved Blind context must reach one real audio callback");

        const juce::String replacementVersionId =
            "efefefef-efef-4fef-8fef-efefefefefef";
        const auto replacementSourceReceipt = stageRuntimeV2Artifact (
            v2Root, "sources", makeRuntimeV2WorkVersionSource (
                alignedBFile, alignedBFileHash, alignedBPcmHash, recordingId,
                replacementVersionId, alignedBFrames));
        const auto replacementPreset = bindRuntimeV2WorkVersionPresetToSource (
            presetId, revisionId, replacementSourceReceipt, alignedBFileHash,
            alignedBPcmHash, recordingId, replacementVersionId, alignedBFrames);
        require (writeJson (presetFile, replacementPreset)
                 && writeJson (manifestFile,
                               makeRuntimeV2Manifest (
                                   presetId, revisionId, presetFile, 9)),
                 "a replacement source must publish before invalidating active Blind");
        for (int attempt = 0; attempt < 400; ++attempt)
        {
            const auto state = controller.snapshot();
            if (state.manifestRevision == 9
                && state.blindPhase == ref::BlindPhase::invalidated)
                break;
            juce::Thread::sleep (10);
        }
        const auto invalidated = controller.snapshot();
        require (invalidated.manifestRevision == 9
                 && invalidated.blindPhase == ref::BlindPhase::invalidated
                 && comparisonSuspended.load(),
                 "source replacement must invalidate Blind without releasing the lower-A gate");
        output.clear();
        output.setSample (0, 0, 0.5f);
        require (controller.renderSelectedB (output, hostPosition, true)
                 && std::abs (output.getSample (0, 0) - 0.5f * heldAGain) < 1.0e-6f,
                 "invalidated Blind must hold approved A attenuation across source replacement");
        controller.endBlind();
        require (comparisonSuspended.load(),
                 "the return request must retain the output gate until normal A is audible");
        output.clear();
        output.setSample (0, 0, 0.5f);
        require (controller.renderSelectedB (output, hostPosition, true)
                 && std::abs (output.getSample (0, 0) - 0.5f) < 1.0e-9f,
                 "the return callback must pass normal A unchanged");
        for (int attempt = 0; attempt < 100 && comparisonSuspended.load(); ++attempt)
            juce::Thread::sleep (10);
        require (! comparisonSuspended.load(),
                 "the worker must release the output gate only after consuming the return receipt");
    }
}
