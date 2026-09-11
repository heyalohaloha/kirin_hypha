#pragma once

#include <functional>
#include <optional>

#include "AppearanceContract.h"

namespace hypha::appearance
{
enum class StoredState { missing, valid, invalid, futureSchema };

struct StoredActivation final
{
    StoredState state = StoredState::missing;
    std::optional<Activation> value;
    bool fromBackup = false;
};

struct StoredPreference final
{
    StoredState state = StoredState::missing;
    std::optional<Preference> value;
    bool fromBackup = false;
};

enum class TransactionState { persisted, unchanged, lockBusy, unsupported, ioFailure };

struct TransactionResult final
{
    TransactionState state = TransactionState::ioFailure;
    Preference preference;
    bool backupUpdated = false;
};

class Storage final
{
public:
    explicit Storage (juce::File sharedPluginDataRoot);

    static juce::File defaultSharedPluginDataRoot();

    StoredActivation readActivation();
    StoredPreference readPreference();
    TransactionResult updatePreference (
        const std::optional<Activation>& activation,
        const std::optional<Choice>& choice,
        bool acknowledgeNotice);

    const juce::File& root() const noexcept { return sharedRoot; }
    juce::File activationFile() const;
    juce::File preferenceFile() const;
    juce::File preferenceBackupFile() const;
    juce::File preferenceLockDirectory() const;

private:
    struct RawDocument final
    {
        StoredState state = StoredState::missing;
        juce::String text;
    };

    RawDocument readRaw (const juce::File&) const;
    bool writeAtomic (const juce::File&, const juce::String&) const;
    void invalidatePreferenceCache();

    juce::File sharedRoot;
    juce::String activationPrimaryText;
    juce::String activationBackupText;
    juce::String preferencePrimaryText;
    juce::String preferenceBackupText;
    StoredActivation cachedActivation;
    StoredPreference cachedPreference;
    bool haveActivationCache = false;
    bool havePreferenceCache = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Storage)
};
}
