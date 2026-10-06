#pragma once

#include <juce_core/juce_core.h>
#include <optional>

namespace hypha::update
{
inline constexpr int maximumManifestBytes = 16 * 1024;
inline constexpr juce::int64 maximumManifestLifetime = 30 * 24 * 60 * 60;

struct Manifest
{
    juce::String version, sourceCommit;
    juce::int64 publishedAt = 0, expiresAt = 0, sequence = 0;
    bool withdrawn = false;
    juce::StringArray platforms, formats;
};

// Stable, numeric major.minor.patch only. Invalid input compares equal; validate first.
bool validSemVer (const juce::String& version);
int compareVersions (const juce::String& left, const juce::String& right);
bool validPublicKey (const juce::String& publicKey);

// Offline/non-RT only. No network, filesystem, license or Reference state access.
// Key format: JUCE RSAKey "10001,<512 lowercase hex digits>" (2048-bit RSA).
// Empty/invalid keys, invalid signatures/schema/time or rollback fail closed.
std::optional<Manifest> verifyManifest (const juce::String& wire,
                                      const juce::String& publicKey,
                                      juce::int64 now,
                                      juce::int64 previousSequence);
}
