#pragma once
#include "reference_runtime_test_support.h"

namespace
{
struct LiveBlindCaptureFacts
{
    bool authorityValid = false, receiptPresent = false, bindingMatches = false, recordingMatches = false;
    std::int64_t now = 0, issuedAt = 0, authorityExpires = 0, capturedAt = 0, captureExpires = 0;
    juce::String revision, pcmHash;
    bool current() const
    {
        return authorityValid && receiptPresent && issuedAt <= now && now <= authorityExpires
            && capturedAt > 0 && now >= capturedAt && now - capturedAt <= 10'000
            && captureExpires >= now && bindingMatches && recordingMatches;
    }
};

// Both file snapshots precede one clock sample. No age or expiry allowance is added.
LiveBlindCaptureFacts readLiveBlindCaptureFacts (
    const juce::File& root, const juce::File& captureFile, const ref::RuntimeIdentity& identity,
    ref::RuntimeABindingRepository::ReadClock clock = [] { return juce::Time::currentTimeMillis(); })
{
    LiveBlindCaptureFacts facts;
    const auto receipt = juce::JSON::parse (captureFile);
    facts.receiptPresent = receipt.isObject();
    facts.capturedAt = static_cast<juce::int64> (receipt["captured_at_ms"]);
    facts.captureExpires = static_cast<juce::int64> (receipt["lease_expires_at_ms"]);
    facts.revision = receipt["daw_revision_id"].toString();
    facts.pcmHash = receipt["cue_pcm_sha256"].toString();
    const auto authority = ref::RuntimeABindingRepository (root, [&] {
        facts.now = clock();
        return facts.now;
    }).loadCurrent (identity);
    facts.authorityValid = authority.has_value();
    if (authority)
    {
        facts.issuedAt = authority->issuedAtMs;
        facts.authorityExpires = authority->leaseExpiresAtMs;
        facts.bindingMatches = receipt["binding_id"].toString() == authority->bindingId;
        facts.recordingMatches = receipt["recording_id"].toString() == authority->recordingId;
    }
    return facts;
}

[[maybe_unused]] void printLiveBlindCaptureFacts (std::ostream& out, const LiveBlindCaptureFacts& facts)
{
    out << " authorityValid=" << facts.authorityValid << " receipt=" << facts.receiptPresent
        << " bindingMatches=" << facts.bindingMatches << " recordingMatches=" << facts.recordingMatches
        << " now=" << facts.now << " issued=" << facts.issuedAt << " authorityExpires=" << facts.authorityExpires
        << " capturedAt=" << facts.capturedAt << " captureExpires=" << facts.captureExpires
        << " captureRevision=" << facts.revision << " pcmHash=" << facts.pcmHash;
}
}
