#pragma once

#include "HyphaVuCalibration.h"
#include "HyphaVuCalibrationFile.h"
#include "HyphaUiPreferences.h"
#include <juce_cryptography/juce_cryptography.h>
#include <utility>

namespace hypha::vu_calibration
{
// The exact chain owns this presentation preference. The filename never contains identity text.
// PRE uses its resolved project and instance; POST uses its selected PRE's exact locator. An
// unpaired POST uses its own identity, so its choice cannot reach a different chain on pairing.
inline juce::String scopeKey (const juce::String& project, const juce::String& instance)
{
    if (project.isEmpty() || instance.isEmpty()) return {};
    const auto identity = juce::String (static_cast<juce::int64> (project.getNumBytesAsUTF8())) + ":" + project
        + juce::String (static_cast<juce::int64> (instance.getNumBytesAsUTF8())) + ":" + instance;
    return juce::SHA256 (identity.toRawUTF8(), identity.getNumBytesAsUTF8()).toHexString();
}

class Preference final
{
public:
    explicit Preference (juce::File directory = ui_preferences::defaultFile()
                                                .getSiblingFile ("vu-calibration"))
        : root (std::move (directory)) {}

    // Caller is the message thread. Host save/restore and the audio path never perform this I/O.
    int read (const juce::String& scope, juce::uint32 now)
    {
        if (scope.isEmpty()) return cachedValue; // transient locator contention keeps adopted calibration
        if (scope == cachedScope && haveRead && now - readAt < refreshMs) return cachedValue;
        cachedScope = scope;
        cachedValue = defaultDbfs;
        readAt = now;
        haveRead = true;
        if (! validScope (scope)) return cachedValue;
        const auto file = fileFor (scope);
        const auto size = file.getSize();
        if (! file.existsAsFile() || size <= 0 || size > 256) return cachedValue;
        const auto text = file.loadFileAsString();
        for (const auto choice : choices)
            if (text == encoded (scope, choice)) cachedValue = choice;
        return cachedValue;
    }

    // Failure retains the previous setting: do not show a choice that the other side cannot read.
    bool set (const juce::String& scope, int value, juce::uint32 now)
    {
        return setUsingWriter (scope, value, now, [] (const juce::File& file, const juce::String& text)
            { return writeAtomicText (file, text); });
    }

    // Same publication and cache rule with an injectable writer; fixtures never exhaust a disk.
    template <typename Writer>
    bool setUsingWriter (const juce::String& scope, int value, juce::uint32 now, Writer write)
    {
        if (! validScope (scope) || ! valid (value)) return false;
        const auto file = fileFor (scope);
        if (root.createDirectory().failed()) return false;
        if (! write (file, encoded (scope, value))) return false;
        cachedScope = scope;
        cachedValue = value;
        readAt = now;
        haveRead = true;
        return true;
    }

    juce::File fileFor (const juce::String& scope) const
    { return root.getChildFile (scope + ".txt"); }

private:
    static constexpr juce::uint32 refreshMs = 250;
    static bool validScope (const juce::String& scope)
    { return scope.length() == 64 && scope.containsOnly ("0123456789abcdef"); }
    static juce::String encoded (const juce::String& scope, int value)
    { return "KIRIN_HYPHA_VU_CALIBRATION_V1\n" + scope + "\n" + juce::String (value) + "\n"; }

    juce::File root;
    juce::String cachedScope;
    int cachedValue = defaultDbfs;
    juce::uint32 readAt = 0;
    bool haveRead = false;
};
}
