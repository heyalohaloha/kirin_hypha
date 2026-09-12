#include "AppearanceStorage.h"

#include "AppearanceFileLock.h"

#include <utility>

namespace hypha::appearance
{
namespace
{
constexpr std::int64_t maxSafeJsonInteger = 9'007'199'254'740'991LL;

StoredState storedState (DecodeState state)
{
    switch (state)
    {
        case DecodeState::valid: return StoredState::valid;
        case DecodeState::invalid: return StoredState::invalid;
        case DecodeState::futureSchema: return StoredState::futureSchema;
    }
    return StoredState::invalid;
}

juce::File backupOf (const juce::File& file)
{
    return file.getSiblingFile (file.getFileName() + ".bak");
}

bool preferenceChanged (const Preference& left, const Preference& right)
{
    return left.activationSeen != right.activationSeen
        || left.firstActivationId != right.firstActivationId
        || left.choice != right.choice
        || left.noticeAcknowledged != right.noticeAcknowledged;
}

juce::String fingerprint (StoredState state, const juce::String& text)
{
    return juce::String (static_cast<int> (state)) + ":" + text;
}
}

Storage::Storage (juce::File sharedPluginDataRoot)
    : sharedRoot (std::move (sharedPluginDataRoot))
{
}

juce::File Storage::defaultSharedPluginDataRoot()
{
   #if JUCE_WINDOWS
    auto local = juce::SystemStats::getEnvironmentVariable ("LOCALAPPDATA", {});
    if (local.isEmpty())
        local = juce::File::getSpecialLocation (
            juce::File::userApplicationDataDirectory).getFullPathName();
    return juce::File (local).getChildFile ("Kirin OS").getChildFile ("plugin_data");
   #elif JUCE_MAC
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Application Support").getChildFile ("Kirin OS")
        .getChildFile ("plugin_data");
   #else
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("Kirin OS").getChildFile ("plugin_data");
   #endif
}

juce::File Storage::activationFile() const
{
    return sharedRoot.getChildFile ("appearance").getChildFile ("v1")
        .getChildFile ("os-jungle-activation.json");
}

juce::File Storage::preferenceFile() const
{
    return sharedRoot.getChildFile ("appearance").getChildFile ("v1")
        .getChildFile ("hypha-jungle-preference.json");
}

juce::File Storage::preferenceBackupFile() const
{
    return backupOf (preferenceFile());
}

juce::File Storage::preferenceLockDirectory() const
{
    return preferenceFile().getSiblingFile ("hypha-jungle-preference.lock");
}

Storage::RawDocument Storage::readRaw (const juce::File& file) const
{
    if (! file.exists())
        return {};
    if (! file.existsAsFile() || file.getSize() <= 0 || file.getSize() > maxDocumentBytes)
        return { StoredState::invalid, {} };
    const auto text = file.loadFileAsString();
    if (text.isEmpty())
        return { StoredState::invalid, {} };
    return { StoredState::valid, text };
}

StoredActivation Storage::readActivation()
{
    const auto primary = readRaw (activationFile());
    const auto backup = readRaw (backupOf (activationFile()));
    const auto primaryFingerprint = fingerprint (primary.state, primary.text);
    const auto backupFingerprint = fingerprint (backup.state, backup.text);
    if (haveActivationCache && primaryFingerprint == activationPrimaryText
        && backupFingerprint == activationBackupText)
        return cachedActivation;

    activationPrimaryText = primaryFingerprint;
    activationBackupText = backupFingerprint;
    haveActivationCache = true;
    if (primary.state == StoredState::valid)
    {
        const auto decoded = decodeActivation (primary.text);
        if (decoded.state != DecodeState::invalid)
        {
            cachedActivation = { storedState (decoded.state), decoded.value, false };
            return cachedActivation;
        }
    }
    cachedActivation = {
        primary.state == StoredState::missing ? StoredState::missing : StoredState::invalid,
        std::nullopt, false
    };
    if (backup.state == StoredState::valid)
    {
        const auto decoded = decodeActivation (backup.text);
        if (decoded.state == DecodeState::valid)
            cachedActivation = { StoredState::valid, decoded.value, true };
        else if (decoded.state == DecodeState::futureSchema)
            cachedActivation = { StoredState::futureSchema, std::nullopt, true };
    }
    else if (primary.state == StoredState::missing && backup.state == StoredState::invalid)
        cachedActivation = { StoredState::invalid, std::nullopt, true };
    return cachedActivation;
}

StoredPreference Storage::readPreference()
{
    const auto primary = readRaw (preferenceFile());
    const auto backup = readRaw (preferenceBackupFile());
    const auto primaryFingerprint = fingerprint (primary.state, primary.text);
    const auto backupFingerprint = fingerprint (backup.state, backup.text);
    if (havePreferenceCache && primaryFingerprint == preferencePrimaryText
        && backupFingerprint == preferenceBackupText)
        return cachedPreference;

    preferencePrimaryText = primaryFingerprint;
    preferenceBackupText = backupFingerprint;
    havePreferenceCache = true;
    if (primary.state == StoredState::valid)
    {
        const auto decoded = decodePreference (primary.text);
        if (decoded.state != DecodeState::invalid)
        {
            cachedPreference = { storedState (decoded.state), decoded.value, false };
            return cachedPreference;
        }
    }
    cachedPreference = {
        primary.state == StoredState::missing ? StoredState::missing : StoredState::invalid,
        std::nullopt, false
    };
    if (backup.state == StoredState::valid)
    {
        const auto decoded = decodePreference (backup.text);
        if (decoded.state == DecodeState::valid)
            cachedPreference = { StoredState::valid, decoded.value, true };
        else if (decoded.state == DecodeState::futureSchema)
            cachedPreference = { StoredState::futureSchema, std::nullopt, true };
    }
    else if (primary.state == StoredState::missing && backup.state == StoredState::invalid)
        cachedPreference = { StoredState::invalid, std::nullopt, true };
    return cachedPreference;
}

bool Storage::writeAtomic (const juce::File& target, const juce::String& text) const
{
    if (target.getParentDirectory().createDirectory().failed())
        return false;
    juce::TemporaryFile temporary (target);
    if (! temporary.getFile().replaceWithText (text, false, false, "\n"))
        return false;
    return temporary.overwriteTargetFileWithTemporary();
}

void Storage::invalidatePreferenceCache()
{
    havePreferenceCache = false;
    preferencePrimaryText.clear();
    preferenceBackupText.clear();
}

TransactionResult Storage::updatePreference (
    const std::optional<Activation>& activation,
    const std::optional<Choice>& choice,
    bool acknowledgeNotice)
{
    if (preferenceFile().getParentDirectory().createDirectory().failed())
        return {};

    FileLock lock (preferenceLockDirectory());
    for (int attempt = 0; attempt < 50 && ! lock.tryAcquire(); ++attempt)
        juce::Thread::sleep (5);
    if (! lock.isHeld())
        return { TransactionState::lockBusy, {}, false };

    invalidatePreferenceCache();
    const auto stored = readPreference();
    if (stored.state == StoredState::futureSchema)
        return { TransactionState::unsupported, {}, false };

    Preference before = stored.value.value_or (defaultPreference());
    Preference after = before;
    if (activation.has_value() && ! after.activationSeen)
    {
        after.activationSeen = true;
        after.firstActivationId = activation->id;
    }
    if (choice.has_value() && after.activationSeen)
        after.choice = *choice;
    if (acknowledgeNotice && after.activationSeen)
        after.noticeAcknowledged = true;

    const bool needsRecovery = stored.fromBackup;
    if (! preferenceChanged (before, after) && ! needsRecovery)
        return { TransactionState::unchanged, before, true };
    if (preferenceChanged (before, after) && before.revision >= maxSafeJsonInteger)
        return { TransactionState::unsupported, before, false };
    if (preferenceChanged (before, after))
        ++after.revision;

    const auto text = encodePreference (after);
    if (decodePreference (text).state != DecodeState::valid
        || ! writeAtomic (preferenceFile(), text))
        return { TransactionState::ioFailure, before, false };
    const bool backupUpdated = writeAtomic (preferenceBackupFile(), text);
    invalidatePreferenceCache();
    return { TransactionState::persisted, after, backupUpdated };
}
}
