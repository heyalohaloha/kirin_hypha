#pragma once

#include <cstdint>
#include <optional>

#include <juce_core/juce_core.h>

#include "ReferenceAuditionModel.h"
#include "ReferencePresetSelectionTransport.h"

namespace hypha::reference_audition
{
    constexpr std::int64_t maximumCandidatePreparationRequestBytes = 4 * 1024;
    constexpr std::int64_t maximumCandidatePreparationAcknowledgementBytes = 4 * 1024;

    struct CandidatePreparationTarget
    {
        juce::String presetId;
        juce::String presetRevisionId;
        juce::String checkId;
        juce::String candidateId;
    };

    struct CandidatePreparationRequest
    {
        juce::String requestId;
        RuntimeIdentity identity;
        std::int64_t manifestRevision = 0;
        std::int64_t requestedAtMs = 0;
        CandidatePreparationTarget target;
        juce::String canonicalJson;
    };

    struct CandidatePreparationAcknowledgement
    {
        juce::String requestId;
        RuntimeIdentity identity;
        std::int64_t handledAtMs = 0;
        PresetSelectionOutcome outcome = PresetSelectionOutcome::rejected;
        std::optional<CandidatePreparationTarget> preparedCandidate;
        std::optional<PresetSelectionRecovery> recovery;
    };

    class CandidatePreparationTransport final
    {
    public:
        explicit CandidatePreparationTransport (juce::File transportRootIn);

        std::optional<CandidatePreparationRequest> writeRequest (
            const RuntimeIdentity&,
            std::int64_t manifestRevision,
            const CandidatePreparationTarget&,
            std::int64_t requestedAtMs,
            juce::String requestId = {}) const;

        juce::File requestFile (const juce::String& runtimeInstanceId,
                                const juce::String& requestId) const;
        juce::File acknowledgementFile (const juce::String& runtimeInstanceId,
                                        const juce::String& requestId) const;
        std::optional<CandidatePreparationAcknowledgement> loadAcknowledgement (
            const CandidatePreparationRequest&) const;
        bool removeExchange (const CandidatePreparationRequest&) const;

    private:
        const juce::File root;
    };
}
