#pragma once
#include <juce_core/juce_core.h>

namespace hypha::tests
{
inline juce::File temporaryParent()
{
    // JUCE's macOS tempDirectory is a real Library/Caches directory. Native fixtures use the
    // build's explicit test root instead; direct invocation uses the process temp environment.
    const auto configured = juce::SystemStats::getEnvironmentVariable ("KIRIN_HYPHA_TEST_STORAGE_ROOT", {});
    if (configured.isNotEmpty()) return juce::File (configured);
    const auto temporary = juce::SystemStats::getEnvironmentVariable (
       #if JUCE_WINDOWS
        "TEMP",
       #else
        "TMPDIR",
       #endif
        {});
    return temporary.isNotEmpty() ? juce::File (temporary) : juce::File::getCurrentWorkingDirectory();
}
}
