#pragma once
#include "reference_runtime_v2_analysis_test_support.h"
#include "../src/reference_audition/ReferenceRuntimePresetOptions.h"

namespace
{
    juce::var pendingPresetDescriptor (const juce::String& id, const juce::String& revision)
    {
        const auto preset = makeRuntimeV2Preset (id, revision);
        auto* descriptor = new juce::DynamicObject();
        descriptor->setProperty ("name", "Saved Work settings");
        for (const auto* field : { "source_template_artifact", "source_preset_artifact" })
            descriptor->setProperty (field, preset.getDynamicObject()->getProperty (field).clone());
        return juce::var (descriptor);
    }

    juce::File pendingPresetRequestFile (const juce::File& root)
    {
        juce::Array<juce::File> files;
        root.getChildFile ("preset_selection_requests").getChildFile (runtimeId)
            .findChildFiles (files, juce::File::findFiles, false, "*.json");
        require (files.size() == 1, "one explicit selection must own exactly one pending exchange");
        return files[0];
    }

    void acknowledgeLazyPreset (const juce::File& root, const juce::File& requestFile,
                                const juce::var& prepared = {})
    {
        const auto request = juce::JSON::parse (requestFile);
        auto* ack = new juce::DynamicObject();
        ack->setProperty ("format", "kirin_hypha_reference_preset_selection_acknowledgement");
        ack->setProperty ("version", "1.0");
        for (const auto* field : { "request_id", "runtime_instance_id", "host_process_id", "work_id" })
            ack->setProperty (field, request.getDynamicObject()->getProperty (field));
        ack->setProperty ("handled_at_ms", juce::Time::currentTimeMillis());
        ack->setProperty ("outcome", prepared.isVoid() ? "action_required" : "prepared");
        ack->setProperty ("prepared_preset", prepared);
        juce::var recovery;
        if (prepared.isVoid())
        {
            recovery = juce::var (new juce::DynamicObject());
            recovery.getDynamicObject()->setProperty ("reason", "source_unavailable");
            recovery.getDynamicObject()->setProperty ("action", "choose_source");
        }
        ack->setProperty ("recovery", recovery);
        require (writeJson (ref::PresetSelectionTransport (root).acknowledgementFile (
            runtimeId, requestFile.getFileNameWithoutExtension()), juce::var (ack)),
            "simulated OS must acknowledge the exact pending request");
    }

    void verifyLazyPresets (const juce::File& sandbox)
    {
        require (sandbox.createDirectory(), "lazy Preset sandbox must exist");
        const juce::String firstId = "88888888-8888-4888-8888-888888888888";
        const juce::String firstRevision = "99999999-9999-4999-8999-999999999999";
        const juce::String secondId = "12121212-1212-4212-8212-121212121212";
        const juce::String secondRevision = "13131313-1313-4313-8313-131313131313";
        const auto secondOption = "work:" + secondId;
        const auto root = sandbox.getChildFile ("transport");
        const auto presetFile = root.getChildFile ("presets").getChildFile (workId).getChildFile (firstId + ".json");
        const auto manifestFile = root.getChildFile ("manifests").getChildFile (workId + ".json");
        const auto sourceFile = sandbox.getChildFile ("source.wav");
        require (writeStereoWav (sourceFile), "lazy fixture must contain real stereo audio");
        const auto hash = juce::SHA256 (sourceFile).toHexString();
        const auto pcmHash = juce::String::repeatedString ("f", 64);
        const auto source = stageRuntimeV2Artifact (root, "sources", makeRuntimeV2Source (sourceFile, hash, pcmHash));
        require (writeJson (presetFile, bindRuntimeV2PresetToSource (
            firstId, firstRevision, source, hash, pcmHash)), "active Preset must be prepared");
        auto manifest = makeRuntimeV2Manifest (firstId, firstRevision, presetFile, 1);
        auto* object = manifest.getDynamicObject();
        object->setProperty ("version", "4.0");
        const auto descriptor = pendingPresetDescriptor (secondId, secondRevision);
        object->setProperty ("pending_presets", juce::var (juce::Array<juce::var> { descriptor }));
        require (writeJson (manifestFile, manifest), "v4 pending metadata must publish without pending audio files");
        ref::RuntimeV2Repository repository (root);
        const auto first = repository.refresh (workId);
        require (first.usable() && first.workspace->presets.size() == 1
                 && first.workspace->manifest.pendingPresets.size() == 1,
                 "pending metadata must not require or pretend to contain a prepared projection");

        ref::Snapshot options;
        ref::appendRuntimePresetOptions (options, *first.workspace);
        require (options.presets.size() == 7 && options.presets.back().id == secondOption
                 && options.presets.back().requiresPreparation,
                 "retired Work settings must remain selectable beside Global Presets");
        auto newerGlobal = *first.workspace;
        newerGlobal.globalPresetCatalog.presets.push_back ({ secondId,
            "14141414-1414-4414-8414-141414141414", "Updated Global settings", "user" });
        ref::Snapshot distinctOptions;
        ref::appendRuntimePresetOptions (distinctOptions, newerGlobal);
        require (distinctOptions.presets.size() == 8
                 && distinctOptions.presets[6].id == secondId
                 && distinctOptions.presets[7].id == secondOption,
                 "new Global and saved Work revisions must have distinct menu identities");

        for (int invalidCase = 0; invalidCase < 5; ++invalidCase)
        {
            auto invalid = manifest.clone();
            auto* bad = invalid.getDynamicObject();
            if (invalidCase == 0) bad->setProperty ("version", "3.0");
            if (invalidCase == 1) bad->getProperty ("pending_presets").getArray()->add (descriptor);
            if (invalidCase == 2) bad->getProperty ("pending_presets").getArray()->getReference (0)
                .getDynamicObject()->setProperty ("ready", true);
            if (invalidCase == 3) bad->getProperty ("pending_presets").getArray()->getReference (0)
                .getDynamicObject()->setProperty ("name", " too faint ");
            if (invalidCase == 4) bad->setProperty ("pending_presets", juce::var (juce::Array<juce::var> {
                pendingPresetDescriptor (firstId, firstRevision) }));
            require (writeJson (manifestFile, invalid), "malformed pending metadata must be staged");
            require (! repository.refresh (workId).usable(), "malformed v4 or expanded v3 must reject");
        }
        auto pendingOnly = manifest.clone();
        auto* waiting = pendingOnly.getDynamicObject();
        waiting->setProperty ("preset_artifacts", juce::var (juce::Array<juce::var> {}));
        auto* selected = new juce::DynamicObject();
        selected->setProperty ("preset_id", secondId);
        selected->setProperty ("revision_id", secondRevision);
        const juce::var secondIdentity (selected);
        waiting->setProperty ("active_preset", secondIdentity);
        require (writeJson (manifestFile, pendingOnly) && repository.refresh (workId).usable(),
                 "a pending active Preset must remain explicit rather than fake a ready source");
        require (writeJson (manifestFile, manifest), "active prepared state must be restored");

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
                 "active Preset must prepare while retired Preset stays listed");
        require (! controller.snapshot().bSelected && controller.selectPreset (secondOption),
                 "loading stays on A; explicit Work option must request OS preparation");
        auto requestFile = pendingPresetRequestFile (root);
        const auto assertExactTemplate = [&] {
            const auto request = juce::JSON::parse (requestFile);
            const auto* identity = request.getDynamicObject()->getProperty ("selected_preset").getDynamicObject();
            require (identity != nullptr && identity->getProperty ("preset_id") == secondId
                     && identity->getProperty ("revision_id") == runtimeTemplateRevisionId,
                     "menu prefix must never enter wire identity or silently select a newer Global revision");
        };
        assertExactTemplate();
        require (ref::runtimePresetDisplaySelection (controller.snapshot()) == secondOption
                 && ! controller.snapshot().bSelected, "pending menu selection must show the exact saved Work option on A");
        acknowledgeLazyPreset (root, requestFile);
        require (waitFor ([] (const auto& s) { return s.presetSelectionStatus == "source_unavailable"; }),
                 "an explicit failed preparation must surface a typed recovery instead of silent success");
        require (controller.retryPresetSelection(), "the same saved revision must remain retryable");
        requestFile = pendingPresetRequestFile (root);
        assertExactTemplate();

        const auto secondFile = presetFile.getSiblingFile (secondId + ".json");
        require (writeJson (secondFile, bindRuntimeV2PresetToSource (
            secondId, secondRevision, source, hash, pcmHash)), "selected Work snapshot must prepare independently");
        const auto secondManifest = makeRuntimeV2Manifest (secondId, secondRevision, secondFile, 2);
        object->getProperty ("preset_artifacts").getArray()->add (
            secondManifest.getDynamicObject()->getProperty ("preset_artifacts").getArray()->getReference (0));
        object->setProperty ("pending_presets", juce::var (juce::Array<juce::var> {}));
        object->setProperty ("revision", static_cast<juce::int64> (2));
        require (writeJson (manifestFile, manifest), "ready publication must retain the previous Preset");
        acknowledgeLazyPreset (root, requestFile, secondIdentity);
        require (waitFor ([&] (const auto& s) { return s.presetId == secondId && s.state == ref::RuntimeState::ready; }),
                 "exact acknowledgement must switch the prepared selection, not audio");
        const auto ready = controller.snapshot();
        require (! ready.bSelected && ready.presets.size() == 7
                 && ! ready.presets.back().requiresPreparation
                 && ref::runtimePresetDisplaySelection (ready) == secondOption,
                 "retired Work option must become ready without duplicate rows, wrong selection, or automatic B playback");
        std::cout << "Lazy Reference Presets: metadata, exact selection, failure/retry, preparation, A safety passed\n";
    }
}
