#include "HyphaHoverHelpPreference.h"

#include "HyphaUiPreferences.h"

#include <utility>

namespace hypha
{
namespace
{
    constexpr auto kHoverHelpKey = "show_hover_help";
    constexpr juce::uint32 kRefreshIntervalMs = 1000u;
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
    return ui_preferences::defaultFile();
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

void HoverHelpPreference::overrideForTest (std::optional<bool> enabled)
{
    const juce::ScopedLock lock (stateLock);
    sessionOverride = enabled.has_value();
    haveRead = enabled.has_value();
    if (enabled) cachedEnabled = *enabled;
}

void HoverHelpPreference::refreshUnlocked()
{
    const auto stored = ui_preferences::read (file, kHoverHelpKey);
    if (stored == "0")
        cachedEnabled = false;
    else if (stored == "1" || ! file.existsAsFile())
        cachedEnabled = true;
    haveRead = true;
}

bool HoverHelpPreference::writeUnlocked (bool enabled)
{
    return ui_preferences::write (file, kHoverHelpKey, enabled ? "1" : "0");
}
}
