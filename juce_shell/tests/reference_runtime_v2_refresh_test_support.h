#pragma once

#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceRuntimeV2PlaybackIdentity.h"

namespace
{
    [[maybe_unused]] void verifyPlaybackIdentityDependencies()
    {
        ref::RuntimePreset preset;
        ref::RuntimeCheck check;
        ref::RuntimeCandidate candidate;
        ref::RuntimeCue cue;
        const auto key = [&] { return ref::runtimeSelectionPlaybackIdentity (preset, check, candidate, cue); };
        const auto initial = key();
        for (auto* field : { &preset.workId, &preset.sourcePresetArtifact.presetId,
                            &check.checkId, &check.mode, &check.comparisonMode,
                            &candidate.candidateId, &candidate.sourceKind,
                            &candidate.sourceIdentityKey, &candidate.sourceWorkId,
                            &candidate.sourceRecordingId, &candidate.sourceVersionId,
                            &candidate.sourceArtifact.relativePath, &candidate.sourceArtifact.sha256,
                            &cue.cueId })
        {
            *field = "changed";
            require (key() != initial, "every selected identity/condition must revoke the old publication");
            field->clear();
        }
        for (auto* field : { &candidate.sourceArtifact.bytes, &cue.sampleRateHz,
                            &cue.startSample, &cue.endSample })
        {
            *field = 1;
            require (key() != initial, "source receipt and complete Cue clock must bind playback");
            *field = 0;
        }
        cue.loopEnabled = true;
        require (key() != initial, "loop policy changes must revoke playback");
        cue.loopEnabled = false;
        preset.name = check.label = candidate.displayName = cue.label = "new label";
        preset.sourcePresetArtifact.revisionId = "new revision";
        preset.sourcePresetArtifact.sha256 = "new snapshot";
        check.viewBindings.push_back ("waveform");
        check.profileBindings.emplace_back();
        candidate.cues.emplace_back();
        candidate.defaultCueId = "other default";
        require (key() == initial, "presentation and unselected contents must not become audio dependencies");
    }

    void verifyUnrelatedPublicationPreservesPlayback (
        ref::RuntimeV2Controller& controller, const juce::File& presetFile,
        const juce::File& manifestFile, const juce::var& presetValue,
        const juce::String& presetId, const juce::String& revisionId,
        std::int64_t hostPosition, juce::AudioBuffer<float>& output)
    {
        const auto before = controller.snapshot();
        output.clear();
        require (controller.renderSelectedB (output, hostPosition, true),
                 "refresh fixture must have audible output before publication");
        const juce::AudioBuffer<float> beforeAudio (output);
        auto updated = presetValue.clone();
        updated.getDynamicObject()->setProperty ("name", "Updated Check Preset label");
        auto* checks = updated.getDynamicObject()->getProperty ("checks").getArray();
        auto extraCheck = checks->getReference (0).clone();
        extraCheck.getDynamicObject()->setProperty ("check_id", "12121212-1212-4212-8212-121212121212");
        checks->add (extraCheck);
        auto* candidates = checks->getReference (0).getDynamicObject()->getProperty ("candidates").getArray();
        auto extraCandidate = candidates->getReference (0).clone();
        extraCandidate.getDynamicObject()->setProperty ("candidate_id", "13131313-1313-4313-8313-131313131313");
        auto* identity = extraCandidate.getDynamicObject()->getProperty ("source_identity").getDynamicObject();
        if (identity->hasProperty ("version_id"))
            identity->setProperty ("version_id", "14141414-1414-4414-8414-141414141414");
        else
            identity->setProperty ("catalog_reference_id", "catalog:additional-reference");
        auto* receipt = extraCandidate.getDynamicObject()->getProperty ("source_artifact").getDynamicObject();
        const auto root = manifestFile.getParentDirectory().getParentDirectory();
        auto extraSource = juce::JSON::parse (root.getChildFile ("sources")
            .getChildFile (receipt->getProperty ("sha256").toString() + ".json"));
        require (extraSource.isObject(), "additional candidate must reuse verified source bytes with a distinct identity");
        extraSource.getDynamicObject()->setProperty ("source_identity",
            extraCandidate.getDynamicObject()->getProperty ("source_identity").clone());
        const auto extraReceipt = stageRuntimeV2Artifact (root, "sources", extraSource);
        receipt->setProperty ("relative_path", extraReceipt.relativePath);
        receipt->setProperty ("sha256", extraReceipt.sha256);
        receipt->setProperty ("bytes", static_cast<juce::int64> (extraReceipt.bytes));
        candidates->add (extraCandidate);
        const auto renewA = [&] {
            if (before.blindPhase != ref::BlindPhase::inactive)
                require (renewRuntimeABinding (ref::RuntimeABindingRepository (root).bindingFile (runtimeId),
                    { runtimeId, workId, 42 }, before.aRecordingId), "simulated OS must keep the active A lease alive");
        };
        renewA();
        const auto nextRevision = before.manifestRevision + 1;
        require (writeJson (presetFile, updated)
                 && writeJson (manifestFile, makeRuntimeV2Manifest (
                     presetId, revisionId, presetFile, nextRevision)),
                 "unrelated Check and B options must publish before refresh");
        for (int attempt = 0; attempt < 400
             && controller.snapshot().manifestRevision != nextRevision; ++attempt)
        {
            if (attempt % 100 == 0) renewA();
            controller.observeTransport (hostPosition, true, true);
            output.clear();
            controller.renderSelectedB (output, hostPosition, true);
            juce::Thread::sleep (10);
        }
        const auto after = controller.snapshot();
        std::cout << "refresh revision " << before.manifestRevision << " -> " << after.manifestRevision
                  << ", checks/candidates " << after.checks.size() << "/" << after.candidates.size()
                  << ", B " << before.bSelected << " -> " << after.bSelected
                  << ", Blind " << static_cast<int> (before.blindPhase) << " -> " << static_cast<int> (after.blindPhase)
                  << ", gain " << before.appliedGainDb << " -> " << after.appliedGainDb << '\n';
        require (after.manifestRevision == nextRevision && after.checks.size() == 2
                 && after.candidates.size() == 2 && after.presetName == "Updated Check Preset label"
                 && after.bSelected == before.bSelected && after.blindPhase == before.blindPhase
                 && after.candidateId == before.candidateId && after.cueId == before.cueId
                 && std::memcmp (&after.appliedGainDb, &before.appliedGainDb, sizeof (double)) == 0,
                 "new options must appear without changing current B, Blind, Cue or gain");
        output.clear();
        require (controller.renderSelectedB (output, hostPosition, true),
                 "unrelated publication must not return audible playback to A");
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            require (std::memcmp (output.getReadPointer (channel), beforeAudio.getReadPointer (channel),
                                 static_cast<size_t> (output.getNumSamples()) * sizeof (float)) == 0,
                     "refresh must preserve all rendered samples and the established playback anchor");
    }

    [[maybe_unused]] void verifyIsolatedBlindRefresh (const juce::File& sandbox)
    {
        const juce::String presetId = "88888888-8888-4888-8888-888888888888";
        const juce::String revisionId = "99999999-9999-4999-8999-999999999999";
        const juce::String recordingId = "22222222-2222-4222-8222-222222222222";
        const juce::String versionId = "cdcdcdcd-cdcd-4dcd-8dcd-cdcdcdcdcdcd";
        const auto root = sandbox.getChildFile ("v2");
        const auto source = sandbox.getChildFile ("b.wav");
        const auto a = makeBlindA();
        const auto frames = a->frameCount + 48'000;
        require (writeBlindB (source, *a, true, 48'000), "isolated B fixture must be prepared");
        const auto fileHash = juce::SHA256 (source).toHexString();
        const auto pcmHash = juce::String::repeatedString ("1", 64);
        const auto receipt = stageRuntimeV2Artifact (root, "sources", makeRuntimeV2WorkVersionSource (
            source, fileHash, pcmHash, recordingId, versionId, frames));
        const auto preset = bindRuntimeV2WorkVersionPresetToSource (
            presetId, revisionId, receipt, fileHash, pcmHash, recordingId, versionId, frames);
        const auto presetFile = root.getChildFile ("presets").getChildFile (workId).getChildFile (presetId + ".json");
        const auto manifestFile = root.getChildFile ("manifests").getChildFile (workId + ".json");
        require (writeJson (presetFile, preset) && writeJson (manifestFile,
            makeRuntimeV2Manifest (presetId, revisionId, presetFile, 1)), "isolated workspace must publish");
        const ref::RuntimeIdentity identity { runtimeId, workId, 42 };
        const auto bindingFile = ref::RuntimeABindingRepository (root).bindingFile (runtimeId);
        require (renewRuntimeABinding (bindingFile, identity, recordingId), "isolated A must be bound");
        ref::RuntimeV2Controller controller (root);
        controller.observeTransport (a->startSample, true, true);
        controller.configure (identity, 48'000, 2);
        for (int attempt = 0; attempt < 400 && ! controller.snapshot().aBindingAvailable; ++attempt)
            juce::Thread::sleep (10);
        for (std::int64_t offset = 0; offset < a->frameCount; offset += 8'192)
        {
            const auto count = static_cast<int> (std::min<std::int64_t> (8'192, a->frameCount - offset));
            juce::AudioBuffer<float> input (2, count);
            for (int channel = 0; channel < 2; ++channel)
                for (int frame = 0; frame < count; ++frame)
                    input.setSample (channel, frame, a->interleaved[static_cast<size_t> ((offset + frame) * 2 + channel)]);
            controller.observeTransport (a->startSample + offset, true, true);
            controller.observeAInput (input, a->startSample + offset, true, true, true);
            juce::Thread::sleep (12);
        }
        for (int attempt = 0; attempt < 1'200 && ! controller.snapshot().blindEligible; ++attempt)
        {
            if (attempt % 200 == 0)
                require (renewRuntimeABinding (bindingFile, identity, recordingId), "isolated lease must renew");
            juce::Thread::sleep (10);
        }
        controller.observeTransport (a->startSample + 1, true, true);
        require (controller.approveBlindLowerAAndStart (-14, -6), "isolated Blind must start explicitly");
        juce::AudioBuffer<float> output (2, 256);
        output.clear();
        require (controller.renderSelectedB (output, a->startSample + 1, true), "isolated Blind must be audible");
        verifyUnrelatedPublicationPreservesPlayback (
            controller, presetFile, manifestFile, preset, presetId, revisionId, a->startSample + 1, output);
        controller.endBlind();
        controller.renderSelectedB (output, a->startSample + 1, true);
    }
}
