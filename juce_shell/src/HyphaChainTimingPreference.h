#pragma once

#include <juce_core/juce_core.h>

#include "HyphaUiPreferences.h"

#include <utility>

namespace hypha
{
// Whether POST keeps the chain timing (LiveCompareChainTiming.h) on its footer. A user-level
// presentation setting shared by every POST, like hover help: off until the user turns it on in
// the information menu. Read and written only on the message thread; the file is read at most
// once a second, so turning it on in one POST reaches the others without per-editor I/O.
class ChainTimingFooterPreference final
{
public:
    explicit ChainTimingFooterPreference (juce::File storageFile) : file (std::move (storageFile)) {}

    static ChainTimingFooterPreference& shared()
    {
        static ChainTimingFooterPreference preference (ui_preferences::defaultFile());
        return preference;
    }

    bool isEnabled()
    {
        const juce::ScopedLock lock (stateLock);
        const auto now = juce::Time::getApproximateMillisecondCounter();
        if (! sessionOverride && (! haveRead || now - lastRefreshMs >= refreshIntervalMs))
        {
            refreshUnlocked();
            lastRefreshMs = now;
        }
        return cachedEnabled;
    }

    // False when the file could not be written: the choice still holds for this session, and
    // the caller tells the user (R-28).
    bool setEnabled (bool enabled)
    {
        const juce::ScopedLock lock (stateLock);
        cachedEnabled = enabled;
        haveRead = true;
        lastRefreshMs = juce::Time::getApproximateMillisecondCounter();
        const bool persisted = ui_preferences::write (file, key, enabled ? "1" : "0");
        sessionOverride = ! persisted;
        return persisted;
    }

    void refreshNowForTest()
    {
        const juce::ScopedLock lock (stateLock);
        if (! sessionOverride)
            refreshUnlocked();
        lastRefreshMs = juce::Time::getApproximateMillisecondCounter();
    }

private:
    static constexpr auto key = "show_chain_timing_footer";
    static constexpr juce::uint32 refreshIntervalMs = 1000u;

    void refreshUnlocked()
    {
        cachedEnabled = ui_preferences::read (file, key) == "1";
        haveRead = true;
    }

    juce::File file;
    juce::CriticalSection stateLock;
    bool cachedEnabled = false;
    bool haveRead = false;
    bool sessionOverride = false;
    juce::uint32 lastRefreshMs = 0u;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChainTimingFooterPreference)
};
}
