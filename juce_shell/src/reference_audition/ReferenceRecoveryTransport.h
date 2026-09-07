#pragma once

#include <cstdint>
#include <optional>

#include <juce_core/juce_core.h>

namespace hypha::reference_audition
{
    constexpr std::int64_t maximumRecoveryRequestBytes = 16 * 1024;
    constexpr std::int64_t maximumRecoveryAcknowledgementBytes = 8 * 1024;

    enum class RecoveryDestination
    {
        reference,
        workBinding,
        candidateSource,
        candidateMeasurement,
        diagnostics,
    };

    enum class RecoveryOutcome
    {
        exactOpened,
        safeFallbackOpened,
        rejected,
    };

    struct RecoveryContext
    {
        juce::String presetId;
        juce::String checkId;
        juce::String candidateId;
    };

    struct RecoveryAuthority
    {
        juce::String runtimeInstanceId;
        std::uint32_t hostProcessId = 0;
        juce::String workId;
    };

    struct RecoveryRequest
    {
        juce::String requestId;
        RecoveryAuthority authority;
        RecoveryDestination destination = RecoveryDestination::reference;
        RecoveryContext context;
        std::int64_t requestedAtMs = 0;
    };

    struct RecoveryAcknowledgement
    {
        juce::String requestId;
        juce::String runtimeInstanceId;
        std::uint32_t hostProcessId = 0;
        RecoveryOutcome outcome = RecoveryOutcome::rejected;
        std::int64_t handledAtMs = 0;
    };

    class RecoveryTransport final
    {
    public:
        explicit RecoveryTransport (juce::File transportRootIn = transportRoot());

        std::optional<RecoveryRequest> writeRequest (
            const RecoveryAuthority&,
            RecoveryDestination,
            const RecoveryContext&,
            std::int64_t requestedAtMs,
            juce::String requestId = {}) const;

        std::optional<RecoveryAcknowledgement> loadAcknowledgement (
            const RecoveryRequest&) const;

        juce::File requestFile (const juce::String& runtimeInstanceId) const;
        juce::File acknowledgementFile (const juce::String& runtimeInstanceId) const;

        static juce::File transportRoot();

    private:
        const juce::File root;
    };
}
