#pragma once

#include <cstdint>
#include <optional>

#include <juce_core/juce_core.h>

#include "ReferenceAuditionModel.h"
#include "ReferenceRuntimeV2Model.h"

namespace hypha::reference_audition
{
    constexpr std::int64_t maximumPresetSelectionRequestBytes = 4 * 1024;
    constexpr std::int64_t maximumPresetSelectionAcknowledgementBytes = 4 * 1024;

    struct PresetSelectionRequest
    {
        juce::String requestId;
        RuntimeIdentity identity;
        std::int64_t manifestRevision = 0;
        std::int64_t requestedAtMs = 0;
        RuntimeGlobalPresetCatalogEntry selectedPreset;
        juce::String canonicalJson;
    };

    enum class PresetSelectionOutcome
    {
        prepared,
        actionRequired,
        rejected,
    };

    struct PresetSelectionRecovery
    {
        juce::String reason;
        juce::String action;
    };

    struct PresetSelectionAcknowledgement
    {
        juce::String requestId;
        RuntimeIdentity identity;
        std::int64_t handledAtMs = 0;
        PresetSelectionOutcome outcome = PresetSelectionOutcome::rejected;
        std::optional<RuntimeGlobalPresetCatalogEntry> preparedPreset;
        std::optional<PresetSelectionRecovery> recovery;
    };

    class PresetSelectionTransport final
    {
    public:
        explicit PresetSelectionTransport (juce::File transportRootIn);

        std::optional<PresetSelectionRequest> writeRequest (
            const RuntimeIdentity&,
            std::int64_t manifestRevision,
            const RuntimeGlobalPresetCatalogEntry&,
            std::int64_t requestedAtMs,
            juce::String requestId = {}) const;

        juce::File requestFile (const juce::String& runtimeInstanceId,
                                const juce::String& requestId) const;
        juce::File acknowledgementFile (const juce::String& runtimeInstanceId,
                                        const juce::String& requestId) const;
        std::optional<PresetSelectionAcknowledgement> loadAcknowledgement (
            const PresetSelectionRequest&) const;
        bool removeExchange (const PresetSelectionRequest&) const;

    private:
        const juce::File root;
    };
}
