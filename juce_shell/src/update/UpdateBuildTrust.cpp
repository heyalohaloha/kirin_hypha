#include "UpdateBuildTrust.h"
#include "UpdateManifest.h"
#include <juce_cryptography/juce_cryptography.h>
#ifndef HYPHA_UPDATE_PUBLIC_KEY
 #define HYPHA_UPDATE_PUBLIC_KEY ""
#endif
#ifndef HYPHA_UPDATE_KEY_SHA256
 #define HYPHA_UPDATE_KEY_SHA256 ""
#endif
#ifndef HYPHA_UPDATE_KEY_MARKER
 #define HYPHA_UPDATE_KEY_MARKER "disabled"
#endif

namespace hypha::update
{
juce::String productionPublicKey()
{
    constexpr char key[] = HYPHA_UPDATE_PUBLIC_KEY;
    constexpr char digest[] = HYPHA_UPDATE_KEY_SHA256;
    // Kept in executable bytes because the runtime consumes it; a sidecar alone
    // cannot attest which key a fresh or reused build actually compiled. Static storage keeps
    // the bytes contiguous: MSVC may build a local array from immediate stores instead.
    static constexpr char marker[] = "KirinHyphaUpdateKeySha256=" HYPHA_UPDATE_KEY_MARKER ";";
    const auto actualMarker = juce::String (marker);
    const auto expected = juce::String (digest).isEmpty() ? juce::String ("disabled") : juce::String (digest);
    if (actualMarker != "KirinHyphaUpdateKeySha256=" + expected + ";"
        || ! validPublicKey (key) || juce::String (digest).length() != 64
        || juce::SHA256 (key, sizeof (key) - 1).toHexString() != digest) return {};
    return key;
}
}
