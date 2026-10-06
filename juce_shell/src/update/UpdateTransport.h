#pragma once
#include <juce_core/juce_core.h>
#include <atomic>
#include <optional>
namespace hypha::update
{
// Fixed official endpoint. Never takes an arbitrary server/URL from a manifest or UI.
inline constexpr const char* manifestEndpoint = "https://kirinmastering.com/updates/hypha-stable.v1.json";
std::optional<juce::String> fetchOfficialManifest (const std::atomic<bool>& cancelled);
#if JUCE_WINDOWS
// Dedicated no-resend transport; JUCE's WinINet retry path is not used.
std::optional<juce::String> fetchOfficialManifestWindows (const std::atomic<bool>& cancelled);
#endif
#if JUCE_MAC
// Dedicated bounded Network.framework connection; callbacks drain before returning.
std::optional<juce::String> fetchOfficialManifestMac (const std::atomic<bool>& cancelled);
#if defined (HYPHA_UPDATE_TRANSPORT_TESTING)
struct MacTransportProbe
{
    std::size_t maximumRetainedBytes = 0, maximumHeaderBytes = 0, dataCallbacks = 0, sends = 0;
    bool connectionCancelled = false, callbacksDrained = false, handlesReleased = false;
};
// Test binaries only. Always 127.0.0.1 and a fixed fixture route; no arbitrary URL.
std::optional<juce::String> fetchManifestMacForTest (const std::atomic<bool>& cancelled,
                                                 unsigned short loopbackPort,
                                                 MacTransportProbe&);
#endif
#endif
}
