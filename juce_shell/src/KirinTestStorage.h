#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace hypha::storage
{
// Non-RT path resolution only. Match kirin_measure::PlatformPaths::under_test_root so
// Rust workers and every JUCE transport share one fixture, including loaded plug-in modules.
// An absent/empty variable leaves each caller's shipping default unchanged. A configured
// but unwritable root remains selected: a failed fixture must never fall back to user data.
inline std::optional<juce::File> testPluginDataRoot()
{
    const auto configured = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_TEST_STORAGE_ROOT", {});
    if (configured.isEmpty()) return std::nullopt;
    const auto root = juce::File::isAbsolutePath (configured)
        ? juce::File (configured) : juce::File::getCurrentWorkingDirectory().getChildFile (configured);
   #if JUCE_WINDOWS
    return root.getChildFile ("home/AppData/Local/Kirin OS/plugin_data");
   #else
    return root.getChildFile ("home/Library/Application Support/Kirin OS/plugin_data");
   #endif
}
}
