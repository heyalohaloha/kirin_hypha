#include "UpdateTransport.h"

namespace hypha::update
{
std::optional<juce::String> fetchOfficialManifest (const std::atomic<bool>& cancelled)
{
#if JUCE_WINDOWS
    return fetchOfficialManifestWindows (cancelled);
#elif JUCE_MAC
    return fetchOfficialManifestMac (cancelled);
#else
    // No supported native transport on other platforms; never fall back to an
    // unbounded shared networking implementation.
    juce::ignoreUnused (cancelled);
    return {};
#endif
}
}
