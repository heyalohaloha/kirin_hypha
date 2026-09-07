#pragma once

namespace
{
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
