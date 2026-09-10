#pragma once

#include "reference_runtime_v2_analysis_test_support.h"

namespace
{
    const juce::String pendingCandidateId = "14141414-1414-4414-8414-141414141414";

    juce::File pendingCandidateRequestFile (const juce::File& root)
    {
        juce::Array<juce::File> files;
        root.getChildFile ("candidate_preparation_requests").getChildFile (runtimeId)
            .findChildFiles (files, juce::File::findFiles, false, "*.json");
        require (files.size() == 1,
                 "one Candidate selection must own exactly one preparation exchange");
        return files[0];
    }

    juce::var makeProgressiveCandidatePreset (
        const juce::String& presetId, const juce::String& revisionId,
        const ref::RuntimeContentReceipt& preparedSource, const juce::String& fileHash,
        const juce::String& pcmHash)
    {
        auto preset = bindRuntimeV2PresetToSource (
            presetId, revisionId, preparedSource, fileHash, pcmHash);
        auto* root = preset.getDynamicObject();
        root->setProperty ("version", "3.0");
        auto* candidates = root->getProperty ("checks").getArray()->getReference (0)
                               .getDynamicObject()->getProperty ("candidates").getArray();
        auto* prepared = candidates->getReference (0).getDynamicObject();
        prepared->setProperty ("preparation_status", "prepared");
        auto pending = candidates->getReference (0).clone();
        auto* pendingObject = pending.getDynamicObject();
        pendingObject->setProperty ("candidate_id", pendingCandidateId);
        pendingObject->setProperty ("display_name", "Reference 2");
        auto* identity = pendingObject->getProperty ("source_identity").getDynamicObject();
        identity->setProperty ("catalog_reference_id", "catalog:source-2");
        pendingObject->setProperty ("source_artifact", juce::var());
        pendingObject->setProperty ("preparation_status", "pending");
        candidates->add (pending);
        return preset;
    }

    void prepareSecondCandidate (juce::var& preset,
                                 const ref::RuntimeContentReceipt& receipt)
    {
        auto* candidate = preset.getDynamicObject()->getProperty ("checks").getArray()
                              ->getReference (0).getDynamicObject()
                              ->getProperty ("candidates").getArray()
                              ->getReference (1).getDynamicObject();
        auto artifact = new juce::DynamicObject();
        artifact->setProperty ("relative_path", receipt.relativePath);
        artifact->setProperty ("sha256", receipt.sha256);
        artifact->setProperty ("bytes", receipt.bytes);
        candidate->setProperty ("source_artifact", juce::var (artifact));
        candidate->setProperty ("preparation_status", "prepared");
    }

    void acknowledgeCandidatePreparation (const juce::File& root,
                                          const juce::File& requestFile)
    {
        const auto request = juce::JSON::parse (requestFile);
        const auto* requestObject = request.getDynamicObject();
        const auto* sourcePreset = requestObject->getProperty ("source_preset").getDynamicObject();
        const auto* selected = requestObject->getProperty ("selected_candidate").getDynamicObject();
        auto target = new juce::DynamicObject();
        target->setProperty ("preset_id", sourcePreset->getProperty ("preset_id"));
        target->setProperty ("preset_revision_id", sourcePreset->getProperty ("revision_id"));
        target->setProperty ("check_id", selected->getProperty ("check_id"));
        target->setProperty ("candidate_id", selected->getProperty ("candidate_id"));
        auto ack = new juce::DynamicObject();
        ack->setProperty ("format",
                          "kirin_hypha_reference_candidate_preparation_acknowledgement");
        ack->setProperty ("version", "1.0");
        for (const auto* field : { "request_id", "runtime_instance_id",
                                  "host_process_id", "work_id" })
            ack->setProperty (field, requestObject->getProperty (field));
        ack->setProperty ("handled_at_ms", juce::Time::currentTimeMillis());
        ack->setProperty ("outcome", "prepared");
        ack->setProperty ("prepared_candidate", juce::var (target));
        ack->setProperty ("recovery", juce::var());
        const auto requestId = requestObject->getProperty ("request_id").toString();
        require (writeJson (ref::CandidatePreparationTransport (root).acknowledgementFile (
                                runtimeId, requestId), juce::var (ack)),
                 "simulated OS must acknowledge the exact Candidate request");
    }

    void verifyLazyCandidates (const juce::File& sandbox)
    {
        require (sandbox.createDirectory(), "lazy Candidate sandbox must exist");
        const juce::String presetId = "88888888-8888-4888-8888-888888888888";
        const juce::String revisionId = "99999999-9999-4999-8999-999999999999";
        const auto root = sandbox.getChildFile ("transport");
        const auto presetFile = root.getChildFile ("presets").getChildFile (workId)
                                    .getChildFile (presetId + ".json");
        const auto manifestFile = root.getChildFile ("manifests").getChildFile (workId + ".json");
        const auto sourceFile = sandbox.getChildFile ("source.wav");
        require (writeStereoWav (sourceFile), "Candidate fixture must contain real stereo audio");
        const auto fileHash = juce::SHA256 (sourceFile).toHexString();
        const auto pcmHash = juce::String::repeatedString ("f", 64);
        const auto firstSource = stageRuntimeV2Artifact (
            root, "sources", makeRuntimeV2Source (sourceFile, fileHash, pcmHash));
        auto preset = makeProgressiveCandidatePreset (
            presetId, revisionId, firstSource, fileHash, pcmHash);
        require (writeJson (presetFile, preset)
                     && writeJson (manifestFile,
                                   makeRuntimeV2Manifest (presetId, revisionId, presetFile, 1)),
                 "progressive Candidate projection must publish atomically");

        ref::RuntimeV2Controller controller (root, [] (bool) { return true; });
        controller.observeTransport (128, true, true);
        controller.configure ({ runtimeId, workId, 42 }, 48'000, 2);
        const auto waitFor = [&] (auto predicate) {
            for (int attempt = 0; attempt < 300; ++attempt)
            {
                controller.observeTransport (128, true, true);
                if (predicate (controller.snapshot())) return true;
                juce::Thread::sleep (10);
            }
            return false;
        };
        require (waitFor ([] (const auto& s) { return s.state == ref::RuntimeState::ready; }),
                 "first prepared Candidate must make the Check immediately auditionable");
        const auto initial = controller.snapshot();
        require (initial.candidates.size() == 2
                     && ! initial.candidates.front().requiresPreparation
                     && initial.candidates.back().requiresPreparation,
                 "prepared and pending Candidates must remain visible and distinguishable");
        require (controller.selectCandidate (pendingCandidateId),
                 "selecting a pending Candidate must request its exact preparation");
        const auto requestFile = pendingCandidateRequestFile (root);
        const auto request = juce::JSON::parse (requestFile);
        const auto* requestObject = request.getDynamicObject();
        const auto* selected = requestObject->getProperty ("selected_candidate").getDynamicObject();
        require (selected != nullptr
                     && selected->getProperty ("candidate_id") == pendingCandidateId
                     && selected->getProperty ("check_id") == initial.checkId
                     && ! controller.snapshot().bSelected
                     && controller.snapshot().candidatePreparationStatus == "pending",
                 "pending selection must identify one Candidate and keep live audio on A");

        auto secondSourceValue = makeRuntimeV2Source (sourceFile, fileHash, pcmHash);
        secondSourceValue.getDynamicObject()->getProperty ("source_identity").getDynamicObject()
            ->setProperty ("catalog_reference_id", "catalog:source-2");
        const auto secondSource = stageRuntimeV2Artifact (root, "sources", secondSourceValue);
        prepareSecondCandidate (preset, secondSource);
        require (writeJson (presetFile, preset)
                     && writeJson (manifestFile,
                                   makeRuntimeV2Manifest (presetId, revisionId, presetFile, 2)),
                 "OS preparation must republish the same Preset with one more ready Candidate");
        acknowledgeCandidatePreparation (root, requestFile);
        require (waitFor ([] (const auto& s) {
                     return s.state == ref::RuntimeState::ready
                         && s.candidateId == pendingCandidateId;
                 }), "exact acknowledgement must restore the requested Candidate selection");
        const auto prepared = controller.snapshot();
        require (! prepared.bSelected && prepared.candidates.size() == 2
                     && ! prepared.candidates.back().requiresPreparation,
                 "preparation must not start B and must not duplicate the Candidate row");
        std::cout << "Lazy Reference Candidates: visible pending state, exact preparation, A safety passed\n";
    }
}
