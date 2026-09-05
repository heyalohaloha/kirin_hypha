#pragma once

#include <cstdint>

#include <juce_core/juce_core.h>

#include "ReferenceAuditionProtocol.h"
#include "ReferenceRuntimeV2Model.h"

namespace hypha::reference_audition
{
    constexpr std::int64_t maximumRuntimeEventBytes = 16 * 1024;

    struct RuntimeEventContext
    {
        RuntimeIdentity identity;
        std::int64_t manifestRevision = 0;
        RuntimeSourcePresetReceipt presetArtifact;
        juce::String presetName;
        juce::String checkId;
        juce::String checkLabel;
        juce::String candidateId;
        juce::String candidateName;
        juce::String cueId;
        juce::String cueLabel;
        juce::String comparisonMode;

        bool valid() const noexcept;
    };

    struct RuntimeEventWriteResult
    {
        bool written = false;
        juce::String eventId;
        juce::String canonicalJson;
    };

    class RuntimeEventTransport final
    {
    public:
        explicit RuntimeEventTransport (juce::File transportRootIn);

        RuntimeEventWriteResult writeAuditionStarted (
            const RuntimeEventContext&, const juce::String& eventId,
            const juce::String& runId, std::int64_t occurredAtMs) const;
        RuntimeEventWriteResult writeAuditionCompleted (
            const RuntimeEventContext&, const juce::String& eventId,
            const juce::String& runId, const juce::String& startedEventId,
            std::int64_t occurredAtMs, std::int64_t bConfirmedSwitches,
            std::int64_t aConfirmedSwitches) const;
        RuntimeEventWriteResult writeBlindStarted (
            const RuntimeEventContext&, const juce::String& eventId,
            const juce::String& runId, std::int64_t occurredAtMs,
            const juce::var& trialStart) const;
        RuntimeEventWriteResult writeBlindCompleted (
            const RuntimeEventContext&, const juce::String& eventId,
            const juce::String& runId, std::int64_t occurredAtMs,
            const juce::var& trialCompleted) const;

        juce::File eventFile (const juce::String& runtimeInstanceId,
                              const juce::String& eventId) const;
        static juce::String canonicalJson (const juce::var&);
        static juce::String uuidV4();

    private:
        RuntimeEventWriteResult write (
            const RuntimeEventContext&, const juce::String& eventId,
            const juce::String& runId, const juce::String& eventType,
            std::int64_t occurredAtMs, const juce::var& note,
            const juce::var& payload) const;

        const juce::File root;
    };
}
