#include "reference_runtime_v2_analysis_test_support.h"

void testRuntimeV2Workspace (const juce::File& sandbox);

void testRuntimeV2Workspace (const juce::File& sandbox)
{
    const auto source = sandbox.getChildFile ("version-a.wav");
    const auto v2Root = sandbox.getChildFile ("plugin_data").getChildFile ("reference").getChildFile ("v2");
    const auto aBindingFile = ref::RuntimeABindingRepository (v2Root).bindingFile (runtimeId);
    const juce::String recordingId = "22222222-2222-4222-8222-222222222222";
        const juce::String presetId = "88888888-8888-4888-8888-888888888888";
        const juce::String revisionId = "99999999-9999-4999-8999-999999999999";
        const auto presetFile = v2Root.getChildFile ("presets").getChildFile (workId)
                                      .getChildFile (presetId + ".json");
        const auto manifestFile = v2Root.getChildFile ("manifests").getChildFile (workId + ".json");
        require (writeJson (presetFile, makeRuntimeV2Preset (presetId, revisionId)),
                 "v2 Preset projection fixture must be written");
        require (writeJson (manifestFile,
                            makeRuntimeV2Manifest (presetId, revisionId, presetFile, 1)),
                 "v2 Manifest fixture must be written");

        ref::RuntimeV2Repository v2Repository (v2Root);
        const auto first = v2Repository.refresh (workId);
        require (first.state == ref::RuntimeWorkspaceLoadState::updated && first.usable()
                 && first.workspace->manifest.revision == 1
                 && first.workspace->presets.size() == 1
                 && first.workspace->presets[0].checks.size() == 1
                 && first.workspace->presets[0].checks[0].candidates[0].displayName
                      == "Reference Mix",
                 "exact v2 Manifest and Preset bytes must load as one immutable workspace");

        require (presetFile.replaceWithText ("{\"partial\":true}"),
                 "partial Preset publication fixture must be written");
        const auto unchanged = v2Repository.refresh (workId, first.workspace);
        require (unchanged.state == ref::RuntimeWorkspaceLoadState::unchanged
                 && unchanged.workspace == first.workspace,
                 "an unchanged Manifest revision must keep the previously accepted workspace");
        require (writeJson (manifestFile,
                            makeRuntimeV2Manifest (presetId, revisionId, presetFile, 2)),
                 "new Manifest pointing at invalid Preset must be written");
        const auto retained = v2Repository.refresh (workId, first.workspace);
        require (retained.state == ref::RuntimeWorkspaceLoadState::retainedPrevious
                 && retained.workspace == first.workspace
                 && retained.rejectionCode == "reference_preset_contract_rejected",
                 "a partial new workspace must retain the last accepted revision");

        require (writeJson (presetFile, makeRuntimeV2Preset (presetId, revisionId))
                 && writeJson (manifestFile,
                               makeRuntimeV2Manifest (presetId, revisionId, presetFile, 2)),
                 "complete replacement workspace must be restored");
        const auto second = v2Repository.refresh (workId, first.workspace);
        require (second.state == ref::RuntimeWorkspaceLoadState::updated
                 && second.workspace->manifest.revision == 2,
                 "a fully verified later Manifest revision must replace the runtime workspace");
        require (writeJson (manifestFile,
                            makeRuntimeV2Manifest (presetId, revisionId, presetFile, 1)),
                 "rollback Manifest fixture must be written");
        const auto rollback = v2Repository.refresh (workId, second.workspace);
        require (rollback.state == ref::RuntimeWorkspaceLoadState::retainedPrevious
                 && rollback.workspace == second.workspace
                 && rollback.rejectionCode == "reference_manifest_rollback",
                 "Manifest rollback must fail closed without taking away audible prior state");

        auto unknownField = makeRuntimeV2Manifest (presetId, revisionId, presetFile, 3);
        unknownField.getDynamicObject()->setProperty ("generated_at", "2026-09-05T00:00:00.000Z");
        require (writeJson (manifestFile, unknownField),
                 "unknown-field Manifest fixture must be written");
        const auto malformedV2 = v2Repository.refresh (workId, second.workspace);
        require (malformedV2.state == ref::RuntimeWorkspaceLoadState::retainedPrevious
                 && malformedV2.rejectionCode == "reference_manifest_rejected",
                 "closed v2 contracts must reject unknown fields and retain prior state");

        const auto exactFileHash = juce::SHA256 (source).toHexString();
        const auto pcmHash = juce::String::repeatedString ("f", 64);
        const auto pendingSource = v2Root.getChildFile ("sources").getChildFile ("pending.json");
        require (writeJson (pendingSource, makeRuntimeV2Source (source, exactFileHash, pcmHash)),
                 "v2 Source artifact fixture must be written");
        const auto sourceArtifactHash = juce::SHA256 (pendingSource).toHexString();
        const auto exactSourceArtifact = pendingSource.getSiblingFile (sourceArtifactHash + ".json");
        require (pendingSource.moveFileTo (exactSourceArtifact),
                 "v2 Source artifact must use its content hash");
        ref::RuntimeCandidate sourceCandidate;
        sourceCandidate.candidateId = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa";
        sourceCandidate.displayName = "Exact Source";
        sourceCandidate.sourceKind = "catalog_track";
        sourceCandidate.sourceIdentityKey = "catalog:source-test:" + exactFileHash + ":" + pcmHash;
        sourceCandidate.sourceArtifact = {
            "plugin_data/reference/v2/sources/" + sourceArtifactHash + ".json",
            sourceArtifactHash,
            exactSourceArtifact.getSize(),
        };
        sourceCandidate.cues.push_back ({
            "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", "Full track", 48'000, 0, 96'000, false,
        });
        sourceCandidate.defaultCueId = sourceCandidate.cues[0].cueId;
        ref::RuntimeV2SourceRepository sourceRepository (v2Root);
        const auto exactSource = sourceRepository.load (sourceCandidate);
        require (exactSource.accepted()
                 && sourceRepository.verifySourceFile (*exactSource.source).isEmpty(),
                 "receipt, identity, revision, full hash, and audio facts must verify together");

        auto sourceWithData = *exactSource.source;
        sourceWithData.measurementArtifact = stageRuntimeV2Artifact (
            v2Root, "measurements", makeRuntimeV2Measurement (exactFileHash, pcmHash));
        ref::RuntimeV2MeasurementRepository measurementRepository (v2Root);
        const auto measurement = measurementRepository.load (sourceWithData);
        require (measurement.accepted()
                 && measurement.measurement->waveform.has_value()
                 && measurement.measurement->waveform->samplePeakMillidbfs[0][0] == -300'000
                 && measurement.measurement->loudness->series.at ("lufs_m_millilu")[0]
                      == std::nullopt,
                 "detailed facts must preserve the source sample grid and leading silence");
        auto wrongMeasurement = sourceWithData;
        wrongMeasurement.sourcePcmSha256 = juce::String::repeatedString ("0", 64);
        require (measurementRepository.load (wrongMeasurement).rejectionCode
                     == "reference_measurement_contract_rejected",
                 "detailed measurement content must remain bound to the exact source PCM");

        sourceWithData.alignmentArtifact = stageRuntimeV2Artifact (
            v2Root, "alignments", makeRuntimeV2Alignment (exactFileHash, pcmHash));
        ref::RuntimeV2AlignmentRepository alignmentRepository (v2Root);
        const auto alignment = alignmentRepository.load (sourceWithData);
        require (alignment.accepted()
                 && alignment.alignment->grid.pointCount == 4
                 && alignment.alignment->features.onsetStrengthQ15[0] == 0
                 && ! alignment.alignment->features.loudnessMillilu[0]
                 && alignment.alignment->features.onsetStrengthQ15[2] == 1000,
                 "alignment must preserve empty opening bars before later content evidence");
        auto insufficientAlignment = makeRuntimeV2Alignment (exactFileHash, pcmHash);
        auto* insufficientFeatures = insufficientAlignment.getDynamicObject()
                                         ->getProperty ("features").getDynamicObject();
        insufficientFeatures->setProperty ("onset_strength_q15",
            juce::var (integerSeries ({ 0, 0, 0, 800 })));
        for (const auto* name : { "sub_energy_millidbfs", "bass_energy_millidbfs",
                                  "mid_energy_millidbfs", "high_energy_millidbfs" })
            insufficientFeatures->setProperty (name, juce::var (integerSeries ({
                std::nullopt, std::nullopt, std::nullopt, -23'000 })));
        juce::Array<juce::var> insufficientChroma;
        for (int pitchClass = 0; pitchClass < 12; ++pitchClass)
            insufficientChroma.add (juce::var (integerSeries ({
                std::nullopt, std::nullopt, std::nullopt, 200 + pitchClass })));
        insufficientFeatures->setProperty ("chroma_q15", juce::var (insufficientChroma));
        insufficientFeatures->setProperty ("loudness_millilu", juce::var (integerSeries ({
            std::nullopt, std::nullopt, std::nullopt, -13'500 })));
        auto insufficientSource = sourceWithData;
        insufficientSource.alignmentArtifact = stageRuntimeV2Artifact (
            v2Root, "alignments", insufficientAlignment);
        require (alignmentRepository.load (insufficientSource).rejectionCode
                     == "reference_alignment_contract_rejected",
                 "leading silence may not be mistaken for alignment evidence");

        const juce::String profileId = "cccccccc-cccc-4ccc-8ccc-cccccccccccc";
        const juce::String profileRevisionId = "dddddddd-dddd-4ddd-8ddd-dddddddddddd";
        const auto pendingProfile = v2Root.getChildFile ("profiles").getChildFile ("pending.json");
        require (writeJson (pendingProfile, makeRuntimeV2Profile (profileId, profileRevisionId)),
                 "v2 Profile projection fixture must be written");
        const auto profileHash = juce::SHA256 (pendingProfile).toHexString();
        const auto exactProfile = pendingProfile.getSiblingFile (profileHash + ".json");
        require (pendingProfile.moveFileTo (exactProfile),
                 "v2 Profile projection must use its content hash");
        const ref::RuntimeContentReceipt profileReceipt {
            "plugin_data/reference/v2/profiles/" + profileHash + ".json",
            profileHash,
            exactProfile.getSize(),
        };
        ref::RuntimeV2ProfileRepository profileRepository (v2Root);
        const auto loadedProfile = profileRepository.load (profileReceipt);
        require (loadedProfile.accepted()
                 && loadedProfile.profile->sourceCount == 3
                 && loadedProfile.profile->views.size() == 1
                 && loadedProfile.profile->views.find ("spectrum")
                      != loadedProfile.profile->views.end(),
                 "exact neutral Profile distributions must load without source names or judgments");

        auto malformedProfile = makeRuntimeV2Profile (profileId, profileRevisionId);
        auto* malformedViews = malformedProfile.getDynamicObject()->getProperty ("views")
                                   .getDynamicObject();
        auto* malformedSpectrum = malformedViews->getProperty ("spectrum").getDynamicObject();
        auto* malformedDistribution = malformedSpectrum->getProperty ("level_millidbfs")
                                          .getDynamicObject();
        auto* malformedCounts = malformedDistribution->getProperty ("contributor_count").getArray();
        malformedCounts->set (0, static_cast<juce::int64> (2));
        require (writeJson (pendingProfile, malformedProfile),
                 "malformed v2 Profile fixture must be written");
        const auto malformedProfileHash = juce::SHA256 (pendingProfile).toHexString();
        const auto exactMalformedProfile = pendingProfile.getSiblingFile (
            malformedProfileHash + ".json");
        require (pendingProfile.moveFileTo (exactMalformedProfile),
                 "malformed v2 Profile fixture must be content-addressed");
        const auto rejectedProfile = profileRepository.load ({
            "plugin_data/reference/v2/profiles/" + malformedProfileHash + ".json",
            malformedProfileHash,
            exactMalformedProfile.getSize(),
        });
        require (! rejectedProfile.accepted()
                 && rejectedProfile.rejectionCode == "reference_profile_contract_rejected",
                 "a Profile must not show quantiles when fewer than three sources contributed");

        const auto controllerPreset = bindRuntimeV2PresetToSource (
            presetId, revisionId, sourceCandidate.sourceArtifact, exactFileHash, pcmHash);
        const auto presentationFile = v2Root.getChildFile ("presentations")
                                            .getChildFile (workId + ".json");
        auto presentation = new juce::DynamicObject();
        presentation->setProperty ("format", "kirin_reference_presentation");
        presentation->setProperty ("version", "1.0");
        presentation->setProperty ("work_id", workId);
        presentation->setProperty ("layout_mode", "main");
        presentation->setProperty ("updated_at", "2026-09-05T00:00:00.000Z");
        juce::var presentationValue (presentation);
        require (writeJson (presentationFile, presentationValue),
                 "shared Reference presentation preference must be written");
        ref::RuntimeV2PresentationRepository presentationRepository (v2Root);
        require (presentationRepository.load (workId) == ref::RuntimePresentationLayout::main,
                 "Hypha must read the Work presentation preference used by the OS preview");
        presentation->setProperty ("unknown", true);
        require (writeJson (presentationFile, presentationValue)
                 && presentationRepository.load (workId)
                      == ref::RuntimePresentationLayout::automatic,
                 "invalid presentation preference must fall back silently to auto");
        presentation->removeProperty ("unknown");
        require (writeJson (presentationFile, presentationValue),
                 "valid presentation preference must be restored");
        require (writeJson (presetFile, controllerPreset)
                 && writeJson (manifestFile,
                               makeRuntimeV2Manifest (presetId, revisionId, presetFile, 4)),
                 "audition-ready v2 workspace must publish Preset before Manifest");
        {
            std::atomic<bool> comparisonSuspended { false };
            std::atomic<bool> gateReleasedOnCallingThread { false };
            const auto callingThread = std::this_thread::get_id();
            const auto controllerNow = juce::Time::currentTimeMillis();
            require (writeJson (aBindingFile, makeRuntimeABinding (
                         { runtimeId, workId, 42 }, recordingId,
                         controllerNow, controllerNow + 9'000)),
                     "live controller A binding must be staged");
            ref::RuntimeV2Controller controller (v2Root,
                [&comparisonSuspended, &gateReleasedOnCallingThread, callingThread] (bool bSelected)
                {
                    if (! bSelected && std::this_thread::get_id() == callingThread)
                        gateReleasedOnCallingThread.store (true);
                    comparisonSuspended.store (bSelected);
                    return true;
                });
            const ref::RuntimeIdentity v2Identity { runtimeId, workId, 42 };
            controller.observeTransport (128, true, true);
            controller.configure (v2Identity, 48'000.0, 2);
            for (int attempt = 0; attempt < 400; ++attempt)
            {
                const auto state = controller.snapshot();
                if (state.state == ref::RuntimeState::ready && state.auditionBuffered)
                    break;
                juce::Thread::sleep (10);
            }
            auto runtime = controller.snapshot();
            require (runtime.state == ref::RuntimeState::ready
                     && runtime.auditionBuffered
                     && runtime.aBindingAvailable
                     && runtime.aRecordingId == recordingId
                     && runtime.presetName == "Mix Reference"
                     && runtime.checkLabel == juce::String::fromUTF8 ("低音")
                     && runtime.candidateName == "Reference Mix"
                     && runtime.cueLabel == "Chorus"
                     && runtime.presentationLayout == "main",
                     "active Preset, first Check, first comparison track, and default Cue must become ready without an open action");
            const auto v2RuntimeFiles = ref::runtimeFiles (v2Root, v2Identity);
            const auto v2Capability = juce::JSON::parse (v2RuntimeFiles.capability);
            require (v2Capability.getDynamicObject() != nullptr
                     && v2Capability.getDynamicObject()->getProperty ("work_id") == workId,
                     "Reference v2 must keep its own exact capability lease alive");
            require (controller.selectB (std::numeric_limits<double>::quiet_NaN(),
                                         std::numeric_limits<double>::quiet_NaN()),
                     "missing comparison measurements must keep original-gain B available");
            runtime = controller.snapshot();
            require (runtime.comparisonFallbackOriginal
                     && ! runtime.gainLimited
                     && std::abs (runtime.appliedGainDb) < 1.0e-9
                     && comparisonSuspended.load(),
                     "measurement fallback must be explicit internally while leaving A unchanged and B at original gain");
            juce::AudioBuffer<float> output (2, 256);
            output.clear();
            require (controller.renderSelectedB (output, 128, true)
                     && std::abs (output.getSample (0, 1)) > 0.001f,
                     "always-ready v2 B must render the verified source");
            controller.observeTransport (12'345, true, true);
            for (int attempt = 0; attempt < 100
                 && ! controller.snapshot().auditionBuffered; ++attempt)
                juce::Thread::sleep (10);
            output.clear();
            require (controller.renderSelectedB (output, 12'345, true),
                     "moving host transport must remain inside the prepared Reference page");
            const auto beforeRefresh = output.getSample (0, 0);
            juce::Thread::sleep (650);
            output.clear();
            require (controller.renderSelectedB (output, 12'345, true)
                     && std::abs (output.getSample (0, 0) - beforeRefresh) < 1.0e-7f,
                     "background workspace polling must not move the established B anchor");
            controller.selectA();

            controller.configure (v2Identity, 44'100.0, 2);
            for (int attempt = 0; attempt < 400
                 && ! controller.snapshot().sampleRateApprovalRequired; ++attempt)
                juce::Thread::sleep (10);
            runtime = controller.snapshot();
            require (runtime.sampleRateApprovalRequired
                     && runtime.sourceSampleRateHz == 48'000
                     && runtime.hostSampleRateHz == 44'100
                     && ! runtime.bSelected,
                     "sample-rate mismatch must hold live A and expose one bounded approval");
            require (controller.approveSampleRateConversion(),
                     "one approval must authorize the exact source-rate and host-rate pair");
            for (int attempt = 0; attempt < 400; ++attempt)
            {
                const auto state = controller.snapshot();
                if (state.state == ref::RuntimeState::ready && state.auditionBuffered)
                    break;
                juce::Thread::sleep (10);
            }
            runtime = controller.snapshot();
            require (runtime.state == ref::RuntimeState::ready
                     && runtime.auditionBuffered
                     && ! runtime.sampleRateApprovalRequired,
                     "approved sample-rate conversion must prepare B without changing the source file");

            auto measuredSourceValue = makeRuntimeV2Source (source, exactFileHash, pcmHash);
            addRuntimeV2MeasurementSummary (measuredSourceValue, -14.0, -3.0);
            const auto measuredSourceReceipt = stageRuntimeV2Artifact (
                v2Root, "sources", measuredSourceValue);
            const auto measuredPreset = bindRuntimeV2PresetToSource (
                presetId, revisionId, measuredSourceReceipt, exactFileHash, pcmHash);
            require (writeJson (presetFile, measuredPreset)
                     && writeJson (manifestFile,
                                   makeRuntimeV2Manifest (presetId, revisionId, presetFile, 5)),
                     "measured v2 Source replacement must publish child artifacts before Manifest");
            controller.configure (v2Identity, 48'000.0, 2);
            for (int attempt = 0; attempt < 400; ++attempt)
            {
                const auto state = controller.snapshot();
                if (state.state == ref::RuntimeState::ready && state.auditionBuffered
                    && std::isfinite (state.sourceIntegratedLoudness))
                    break;
                juce::Thread::sleep (10);
            }
            require (controller.selectB (-11.0, -2.0),
                     "v2 loudness match must remain audible when positive B gain is headroom-limited");
            runtime = controller.snapshot();
            require (runtime.gainLimited
                     && ! runtime.comparisonFallbackOriginal
                     && std::abs (runtime.appliedGainDb - 2.0) < 1.0e-9
                     && std::abs (runtime.adjustedBMaximumTruePeakDbtp + 1.0) < 1.0e-9
                     && std::abs (runtime.loudnessDeltaBMinusA + 1.0) < 1.0e-9,
                     "v2 normal A/B must cap B at -1 dBTP and expose the remaining mismatch instead of rejecting it");
            controller.selectA();
            require (controller.selectB (-11.0, 0.2),
                     "a pre-existing A peak above -1 dBTP must not make a safe exact match unavailable");
            runtime = controller.snapshot();
            require (! runtime.gainLimited
                     && std::abs (runtime.appliedGainDb - 3.0) < 1.0e-9
                     && std::abs (runtime.adjustedBMaximumTruePeakDbtp) < 1.0e-9
                     && std::abs (runtime.loudnessDeltaBMinusA) < 1.0e-9,
                     "normal A/B must preserve the louder existing source ceiling instead of imposing -1 dBTP");
            controller.selectA();
            gateReleasedOnCallingThread.store (false);
            require (controller.selectB (-11.0, -2.0),
                     "audio-thread fail-close fixture must enter B first");
            output.clear();
            output.setSample (0, 0, 0.625f);
            require (! controller.renderSelectedB (output, 128, true, false)
                     && output.getSample (0, 0) == 0.625f
                     && ! controller.snapshot().bSelected,
                     "an unavailable realtime route must return the buffer and selection to A immediately");
            require (! gateReleasedOnCallingThread.load(),
                     "the realtime callback must not execute the lock-taking output gate release");
            for (int attempt = 0; attempt < 100 && comparisonSuspended.load(); ++attempt)
                juce::Thread::sleep (10);
            require (! comparisonSuspended.load(),
                     "the controller worker must release the output gate after realtime fail-close");

            const auto alignedA = makeBlindA();
            const auto alignedBFile = sandbox.getChildFile ("controller-b-trimmed.wav");
            constexpr std::int64_t alignedBTailFrames = 48'000;
            const auto alignedBFrames = alignedA->frameCount + alignedBTailFrames;
            require (writeBlindB (alignedBFile, *alignedA, false,
                                  static_cast<int> (alignedBTailFrames)),
                     "controller content-alignment B fixture must be written");
            const auto alignedBFileHash = juce::SHA256 (alignedBFile).toHexString();
            const auto alignedBPcmHash = juce::String::repeatedString ("1", 64);
            const juce::String alignedVersionId =
                "abababab-abab-4bab-8bab-abababababab";
            const auto alignedSourceReceipt = stageRuntimeV2Artifact (
                v2Root, "sources", makeRuntimeV2WorkVersionSource (
                    alignedBFile, alignedBFileHash, alignedBPcmHash, recordingId,
                    alignedVersionId, alignedBFrames));
            const auto alignedPreset = bindRuntimeV2WorkVersionPresetToSource (
                presetId, revisionId, alignedSourceReceipt, alignedBFileHash,
                alignedBPcmHash, recordingId, alignedVersionId, alignedBFrames);
            require (writeJson (presetFile, alignedPreset)
                     && writeJson (manifestFile,
                                   makeRuntimeV2Manifest (
                                       presetId, revisionId, presetFile, 6)),
                     "same-recording Work Version must publish before alignment testing");
            const auto alignmentNow = juce::Time::currentTimeMillis();
            require (writeJson (aBindingFile, makeRuntimeABinding (
                         { runtimeId, workId, 42 }, recordingId,
                         alignmentNow, alignmentNow + 9'000)),
                     "content alignment requires a renewed live A binding");
            controller.disconnect();
            for (int attempt = 0; attempt < 200
                 && controller.snapshot().state != ref::RuntimeState::disconnected; ++attempt)
                juce::Thread::sleep (10);
            require (controller.snapshot().state == ref::RuntimeState::disconnected,
                     "controller reconfiguration must first finish its fail-closed reset");
            controller.observeTransport (alignedA->startSample, true, true);
            controller.configure (v2Identity, 48'000.0, 2);
            for (int attempt = 0; attempt < 400
                 && (controller.snapshot().state != ref::RuntimeState::ready
                     || controller.snapshot().sourceKind != "work_version"); ++attempt)
                juce::Thread::sleep (10);
            require (controller.snapshot().state == ref::RuntimeState::ready
                     && controller.snapshot().sourceKind == "work_version",
                     "same-recording Work Version must become ready before A capture");
            constexpr int captureBlockFrames = 8'192;
            for (std::int64_t offset = 0; offset < alignedA->frameCount;
                 offset += captureBlockFrames)
            {
                const auto frames = static_cast<int> (std::min<std::int64_t> (
                    captureBlockFrames, alignedA->frameCount - offset));
                juce::AudioBuffer<float> input (2, frames);
                for (int frame = 0; frame < frames; ++frame)
                    for (int channel = 0; channel < 2; ++channel)
                        input.setSample (channel, frame, alignedA->interleaved[static_cast<size_t> (
                            (offset + frame) * 2 + channel)]);
                const auto hostPosition = alignedA->startSample + offset;
                controller.observeTransport (hostPosition, true, true);
                controller.observeAInput (input, hostPosition, true, true, true);
                juce::Thread::sleep (12);
            }
            for (int attempt = 0; attempt < 400
                 && ! controller.snapshot().blindEligible; ++attempt)
                juce::Thread::sleep (10);
            require (controller.snapshot().blindEligible,
                     "DAW content at bar five must establish the shared normal/Blind alignment");
            controller.observeTransport (alignedA->startSample + 1, true, true);
            for (int attempt = 0; attempt < 200
                 && ! controller.snapshot().auditionBuffered; ++attempt)
                juce::Thread::sleep (10);
            require (controller.selectB (std::numeric_limits<double>::quiet_NaN(),
                                         std::numeric_limits<double>::quiet_NaN()),
                     "content-aligned normal B must remain one-action available");
            output.clear();
            require (controller.renderSelectedB (
                         output, alignedA->startSample + 1, true)
                     && std::abs (output.getSample (0, 0)
                                  - alignedA->interleaved[2] * 0.5f) < 1.0e-5f,
                     "normal B must map bar-five DAW content to the matched trimmed-file sample");
            controller.selectA();
        }

        require (source.replaceWithText ("changed after receipt")
                 && sourceRepository.verifySourceRevision (*exactSource.source)
                      == "reference_source_changed"
                 && sourceRepository.verifySourceFile (*exactSource.source)
                      == "reference_source_changed",
                 "a changed source revision must stop recurring audition before the expensive full check");
}
