#include "ReferenceRuntimeV2Controller.h"

namespace hypha::reference_audition
{
bool RuntimeV2Controller::requestLibraryRecovery()
{
    if (! libraryOnline.load (std::memory_order_acquire)) return false;
    if (pendingLibraryOpen != juce::File()) return true;
    const RuntimePreset* targetPreset = nullptr;
    if (workspace)
        for (const auto& preset : workspace->presets)
            if (preset.sourcePresetArtifact.presetId == currentSnapshot.presetId)
            { targetPreset = &preset; break; }
    const RuntimeCheck* targetCheck = nullptr;
    if (targetPreset != nullptr)
        for (const auto& check : targetPreset->checks)
            if (check.checkId == currentSnapshot.checkId)
            { targetCheck = &check; break; }
    if (! requestedConfiguration.identity.valid() || targetPreset == nullptr
        || targetCheck == nullptr) return false;
    bool opensBalanceSettings = false;
    for (const auto& binding : targetCheck->viewBindings)
        opensBalanceSettings = opensBalanceSettings || binding == "balance";
    const auto id = juce::Uuid().toDashedString().toLowerCase();
    const auto requestedAt = juce::Time::currentTimeMillis();
    auto* object = new juce::DynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_open");
    object->setProperty ("version", "2.0");
    object->setProperty ("namespace", opensBalanceSettings ? "tonal-v1" : "reference-v1");
    object->setProperty ("request_id", id);
    object->setProperty ("requested_at_ms", requestedAt);
    object->setProperty ("runtime_instance_id",
                         requestedConfiguration.identity.runtimeInstanceId);
    object->setProperty ("preset_id", currentSnapshot.presetId);
    object->setProperty ("preset_revision_id", targetPreset->sourcePresetArtifact.revisionId);
    object->setProperty ("check_id", currentSnapshot.checkId);
    const auto file = root.getChildFile ("library/open").getChildFile (id + ".json");
    if (! file.getParentDirectory().createDirectory()) return false;
    juce::TemporaryFile temporary (file);
    if (! temporary.getFile().replaceWithText (juce::JSON::toString (juce::var (object)))
        || ! temporary.overwriteTargetFileWithTemporary()) return false;
    pendingLibraryOpen = file;
    libraryOpenRequestedAt = requestedAt;
    currentSnapshot.recoveryStatus = "pending";
    notify();
    return true;
}

void RuntimeV2Controller::serviceLibraryRecovery()
{
    const juce::ScopedLock lock (stateLock);
    if (pendingLibraryOpen == juce::File()) return;
    const bool opened = ! pendingLibraryOpen.existsAsFile();
    if (! opened && juce::Time::currentTimeMillis() - libraryOpenRequestedAt < 15000) return;
    currentSnapshot.recoveryStatus = opened ? "opened" : "timed_out";
    pendingLibraryOpen = juce::File {};
    recoveryStatusExpiresAtMs = juce::Time::currentTimeMillis() + 3000;
}
}
