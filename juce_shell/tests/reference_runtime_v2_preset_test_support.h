#pragma once

#include "../src/reference_audition/ReferenceRuntimeV2PresetParsing.h"

namespace
{
// 2026-10-05：Kirin OS の工場出荷の Preset は 6 項目までに分けて 5 → 9 になった。作品ごとのカタログは「工場出荷が
// 先、そのあとが利用者」の並びだけを見て、工場出荷の数を決め打ちしない。
[[maybe_unused]] void verifyGlobalPresetCatalogOrder()
{
    const auto catalog = [] (std::initializer_list<const char*> origins)
    {
        auto root = new juce::DynamicObject();
        root->setProperty ("format", "kirin_hypha_reference_global_preset_catalog");
        root->setProperty ("version", "1.0");
        juce::Array<juce::var> presets;
        int index = 0;
        for (const auto* origin : origins)
        {
            auto entry = new juce::DynamicObject();
            entry->setProperty ("preset_id", "00000000-0000-4000-8000-" + juce::String (100 + index).paddedLeft ('0', 12));
            entry->setProperty ("revision_id", "00000000-0000-4000-8000-" + juce::String (200 + index).paddedLeft ('0', 12));
            entry->setProperty ("name_snapshot", "Preset " + juce::String (++index));
            entry->setProperty ("origin", juce::String (origin));
            presets.add (juce::var (entry));
        }
        root->setProperty ("presets", juce::var (presets));
        return juce::var (root);
    };
    const auto parses = [] (const juce::var& value)
    {
        ref::RuntimeGlobalPresetCatalog parsed;
        return ref::runtime_v2_parsing::parseGlobalPresetCatalog (value, parsed);
    };
    require (parses (catalog ({ "factory", "factory", "factory", "factory", "factory", "factory", "factory", "factory",
                                "factory", "user", "user" })),
             "nine Factory Presets followed by User Presets are a valid catalog");
    require (parses (catalog ({ "factory", "factory", "factory", "factory", "factory", "user" })),
             "a catalog does not hard-code how many Factory Presets Kirin OS ships");
    require (! parses (catalog ({ "user", "factory" })) && ! parses (catalog ({ "factory", "user", "factory" }))
                 && ! parses (catalog ({ "factory", "other" })),
             "Factory Presets come first, then User Presets, and nothing else");
}

[[maybe_unused]] void verifyRuntimeV2PresetSelection (
    ref::RuntimeV2Controller& controller,
    const juce::File& transportRoot,
    const ref::RuntimeIdentity& identity,
    std::int64_t manifestRevision,
    const ref::RuntimeSourcePresetReceipt& sourcePreset,
    const juce::String& expectedTemplateRevision,
    const juce::String& expectedPresetRevision,
    const ref::Snapshot& initialSnapshot)
{
    const auto adoptionFile = ref::PresetAdoptionTransport (transportRoot).adoptionFile (
        identity, manifestRevision, sourcePreset);
    const auto adoptionJson = juce::JSON::parse (adoptionFile);
    const auto* adoptionObject = adoptionJson.getDynamicObject();
    const auto* adoptedTemplate = adoptionObject == nullptr ? nullptr
        : adoptionObject->getProperty ("source_template_artifact").getDynamicObject();
    const auto* adoptedPreset = adoptionObject == nullptr ? nullptr
        : adoptionObject->getProperty ("source_preset_artifact").getDynamicObject();
    require (adoptionFile.existsAsFile()
             && adoptionFile.getSize() <= ref::maximumPresetAdoptionBytes
             && adoptionObject != nullptr
             && hasExactKeys (*adoptionObject, {
                 "format", "version", "runtime_instance_id", "host_process_id",
                 "work_id", "manifest_revision", "source_template_artifact",
                 "source_preset_artifact", "adopted_at_ms" })
             && static_cast<juce::int64> (
                 adoptionObject->getProperty ("manifest_revision")) == manifestRevision
             && adoptedTemplate != nullptr && adoptedPreset != nullptr
             && adoptedTemplate->getProperty ("revision_id") == expectedTemplateRevision
             && adoptedPreset->getProperty ("revision_id") == expectedPresetRevision,
             "Hypha must report actual adoption only after the exact Work snapshot becomes ready");
    require (initialSnapshot.presets.size() == 6
             && initialSnapshot.presets[0].requiresPreparation
             && initialSnapshot.presets[0].id
                    == "00000000-0000-4000-8000-000000000100"
             && initialSnapshot.presets[0].id != initialSnapshot.presetId
             && ! initialSnapshot.presets.back().requiresPreparation,
             "Preset identity must use exact IDs and revisions even when a Work Preset has the same name as a Factory Preset");
    const auto activeWorkPresetId = initialSnapshot.presetId;
    const auto requestedFactoryPresetId = initialSnapshot.presets[0].id;
    require (controller.selectPreset (requestedFactoryPresetId),
             "an absent Global Preset must be selectable without opening Kirin OS");
    juce::File selectionRequestFile;
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        const auto requestDirectory = transportRoot.getChildFile (
            "preset_selection_requests").getChildFile (identity.runtimeInstanceId);
        juce::Array<juce::File> files;
        requestDirectory.findChildFiles (files, juce::File::findFiles, false, "*.json");
        if (! files.isEmpty())
        {
            selectionRequestFile = files[0];
            break;
        }
        juce::Thread::sleep (10);
    }
    const auto pending = controller.snapshot();
    const auto selectionRequestJson = juce::JSON::parse (selectionRequestFile);
    const auto* selectionRequestObject = selectionRequestJson.getDynamicObject();
    const auto* requestedPresetObject = selectionRequestObject == nullptr ? nullptr
        : selectionRequestObject->getProperty ("selected_preset").getDynamicObject();
    require (selectionRequestFile.existsAsFile()
             && requestedPresetObject != nullptr
             && requestedPresetObject->getProperty ("preset_id") == requestedFactoryPresetId
             && pending.presetSelectionStatus == "pending"
             && pending.presetSelectionTargetId == requestedFactoryPresetId
             && ! pending.auditionBuffered && ! pending.bSelected,
             "Preset preparation must hold A and write the exact Global identity for Kirin OS");
    require (controller.selectPreset (activeWorkPresetId),
             "choosing an already prepared Work Preset must cancel the pending request");
    for (int attempt = 0; attempt < 100 && selectionRequestFile.existsAsFile(); ++attempt)
        juce::Thread::sleep (10);
    require (! selectionRequestFile.exists()
             && controller.snapshot().presetSelectionStatus.isEmpty(),
             "cancelling preparation must remove only Hypha's pending exchange");
}
}
