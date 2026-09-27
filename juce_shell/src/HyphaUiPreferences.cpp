#include "HyphaUiPreferences.h"

#include <utility>

namespace hypha::ui_preferences
{
namespace
{
constexpr auto kHeader = "KIRIN_HYPHA_UI_PREFERENCES_V1";

juce::StringArray settingLines (const juce::File& file)
{
    if (! file.existsAsFile())
        return {};
    juce::StringArray lines;
    lines.addLines (file.loadFileAsString());
    if (lines.isEmpty() || lines[0].trim() != kHeader)
        return {};
    lines.remove (0);
    lines.removeEmptyStrings();
    return lines;
}
}

juce::File defaultFile()
{
    auto root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    root = root.getChildFile ("Application Support");
   #endif
    return root.getChildFile ("Kirin")
               .getChildFile ("Kirin Hypha")
               .getChildFile ("ui-preferences.txt");
}

juce::String read (const juce::File& file, const juce::String& key)
{
    for (const auto& line : settingLines (file))
        if (line.upToFirstOccurrenceOf ("=", false, false).trim() == key)
            return line.fromFirstOccurrenceOf ("=", false, false).trim();
    return {};
}

bool write (const juce::File& file, const juce::String& key, const juce::String& value)
{
    if (file.getParentDirectory().createDirectory().failed())
        return false;

    auto lines = settingLines (file);
    bool replaced = false;
    for (auto& line : lines)
        if (line.upToFirstOccurrenceOf ("=", false, false).trim() == key)
        {
            line = key + "=" + value;
            replaced = true;
        }
    if (! replaced)
        lines.add (key + "=" + value);

    juce::TemporaryFile temporary (file);
    const auto text = juce::String (kHeader) + "\n" + lines.joinIntoString ("\n") + "\n";
    if (! temporary.getFile().replaceWithText (text, false, false, "\n"))
        return false;
    return temporary.overwriteTargetFileWithTemporary();
}
}

namespace hypha
{
namespace
{
constexpr auto kLanguageKey = "language";
constexpr juce::uint32 kRefreshIntervalMs = 1000u;

juce::String code (i18n::Language language)
{
    return language == i18n::Language::japanese ? "ja" : "en";
}
}

LanguagePreference::LanguagePreference (juce::File storageFile, juce::String systemDisplayLanguage)
    : file (std::move (storageFile)),
      systemLanguage (i18n::languageForSystem (systemDisplayLanguage)),
      cachedLanguage (systemLanguage)
{
}

LanguagePreference& LanguagePreference::shared()
{
    static LanguagePreference preference (ui_preferences::defaultFile(),
                                          juce::SystemStats::getDisplayLanguage());
    return preference;
}

i18n::Language LanguagePreference::language()
{
    const juce::ScopedLock lock (stateLock);
    const auto now = juce::Time::getApproximateMillisecondCounter();
    if (! sessionOverride && (! haveRead || now - lastRefreshMs >= kRefreshIntervalMs))
    {
        refreshUnlocked();
        lastRefreshMs = now;
    }
    return cachedLanguage;
}

bool LanguagePreference::setLanguage (i18n::Language language)
{
    const juce::ScopedLock lock (stateLock);
    cachedLanguage = language;
    haveRead = true;
    lastRefreshMs = juce::Time::getApproximateMillisecondCounter();
    const auto persisted = ui_preferences::write (file, kLanguageKey, code (language));
    sessionOverride = ! persisted;
    return persisted;
}

void LanguagePreference::refreshNowForTest()
{
    const juce::ScopedLock lock (stateLock);
    if (! sessionOverride)
        refreshUnlocked();
    lastRefreshMs = juce::Time::getApproximateMillisecondCounter();
}

void LanguagePreference::refreshUnlocked()
{
    const auto stored = ui_preferences::read (file, kLanguageKey);
    cachedLanguage = stored == "ja" ? i18n::Language::japanese
                   : stored == "en" ? i18n::Language::english
                                    : systemLanguage;
    haveRead = true;
}
}
