#pragma once

#include <juce_core/juce_core.h>

#include "HyphaLanguage.h"

// The user-level presentation settings PRE and POST share, one `key=value` line each under a
// header, in one small file (ui-preferences.txt). A write replaces only its own key and goes
// through a temporary file, so hover help and the language never overwrite each other.
namespace hypha::ui_preferences
{
juce::File defaultFile();
// The value stored for `key`, or an empty string when the file, its header or the key is missing.
juce::String read (const juce::File&, const juce::String& key);
bool write (const juce::File&, const juce::String& key, const juce::String& value);
}

namespace hypha
{
// The language Hypha shows when it runs as a plug-in: the one chosen in MENU, or the system's
// display language until a choice is made. The file is read only on the message thread, at most
// once a second, so a choice made in POST reaches every open PRE without per-editor I/O.
class LanguagePreference final
{
public:
    LanguagePreference (juce::File storageFile, juce::String systemDisplayLanguage);

    static LanguagePreference& shared();

    i18n::Language language();
    // Stores an explicit choice. When the file cannot be written the choice still holds for this
    // session, and the caller tells the user (R-28).
    bool setLanguage (i18n::Language);
    void refreshNowForTest();

private:
    void refreshUnlocked();

    juce::File file;
    i18n::Language systemLanguage;
    juce::CriticalSection stateLock;
    i18n::Language cachedLanguage;
    bool haveRead = false;
    bool sessionOverride = false;
    juce::uint32 lastRefreshMs = 0u;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LanguagePreference)
};
}
