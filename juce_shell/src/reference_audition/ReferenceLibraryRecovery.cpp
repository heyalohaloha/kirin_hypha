#include "ReferenceRuntimeV2Controller.h"

namespace hypha::reference_audition
{
bool RuntimeV2Controller::requestLibraryRecovery()
{
    if (! libraryOnline.load (std::memory_order_acquire)) return false;
    if (pendingLibraryOpen != juce::File()) return true;
    const auto id = juce::Uuid().toDashedString();
    const auto requestedAt = juce::Time::currentTimeMillis();
    auto* object = new juce::DynamicObject();
    object->setProperty ("format", "kirin_hypha_reference_library_open");
    object->setProperty ("version", "1.0");
    object->setProperty ("request_id", id);
    object->setProperty ("requested_at_ms", requestedAt);
    object->setProperty ("preset_id", currentSnapshot.presetId);
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
