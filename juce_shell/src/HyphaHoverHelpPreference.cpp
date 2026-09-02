#include "HyphaHoverHelpPreference.h"

#include <utility>

namespace hypha
{
namespace
{
    constexpr auto kHeader = "KIRIN_HYPHA_UI_PREFERENCES_V1";
    constexpr auto kEnabled = "show_hover_help=1";
    constexpr auto kDisabled = "show_hover_help=0";
    constexpr juce::uint32 kRefreshIntervalMs = 1000u;

    bool decodeEnabled (const juce::String& text, bool fallback)
    {
        if (! text.startsWith (kHeader))
            return fallback;
        if (text.contains (kDisabled))
            return false;
        if (text.contains (kEnabled))
            return true;
        return fallback;
    }
}

HoverHelpPreference::HoverHelpPreference (juce::File storageFile)
    : file (std::move (storageFile))
{
}

HoverHelpPreference& HoverHelpPreference::shared()
{
    static HoverHelpPreference preference (defaultStorageFile());
    return preference;
}

juce::File HoverHelpPreference::defaultStorageFile()
{
    auto root = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory);
   #if JUCE_MAC
    root = root.getChildFile ("Application Support");
   #endif
    return root.getChildFile ("Kirin")
               .getChildFile ("Kirin Hypha")
               .getChildFile ("ui-preferences.txt");
}

bool HoverHelpPreference::isEnabled()
{
    const juce::ScopedLock lock (stateLock);
    const auto now = juce::Time::getApproximateMillisecondCounter();
    if (! sessionOverride
        && (! haveRead || now - lastRefreshMs >= kRefreshIntervalMs))
    {
        refreshUnlocked();
        lastRefreshMs = now;
    }
    return cachedEnabled;
}

bool HoverHelpPreference::setEnabled (bool enabled)
{
    const juce::ScopedLock lock (stateLock);
    cachedEnabled = enabled;
    haveRead = true;
    lastRefreshMs = juce::Time::getApproximateMillisecondCounter();
    const bool persisted = writeUnlocked (enabled);
    sessionOverride = ! persisted;
    return persisted;
}

void HoverHelpPreference::refreshNowForTest()
{
    const juce::ScopedLock lock (stateLock);
    if (! sessionOverride)
        refreshUnlocked();
    lastRefreshMs = juce::Time::getApproximateMillisecondCounter();
}

void HoverHelpPreference::refreshUnlocked()
{
    if (! file.existsAsFile())
    {
        cachedEnabled = true;
        haveRead = true;
        return;
    }

    const auto text = file.loadFileAsString();
    cachedEnabled = decodeEnabled (text, cachedEnabled);
    haveRead = true;
}

bool HoverHelpPreference::writeUnlocked (bool enabled)
{
    if (file.getParentDirectory().createDirectory().failed())
        return false;

    juce::TemporaryFile temporary (file);
    const juce::String text = juce::String (kHeader) + "\n"
                            + (enabled ? kEnabled : kDisabled) + "\n";
    if (! temporary.getFile().replaceWithText (text, false, false, "\n"))
        return false;
    return temporary.overwriteTargetFileWithTemporary();
}
}
