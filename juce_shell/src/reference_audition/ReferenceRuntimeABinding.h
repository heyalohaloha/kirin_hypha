#pragma once

#include <cstdint>
#include <optional>

#include <juce_core/juce_core.h>

#include "ReferenceAuditionModel.h"

namespace hypha::reference_audition
{
    struct RuntimeABinding
    {
        juce::String bindingId;
        juce::String runtimeInstanceId;
        std::uint32_t hostProcessId = 0;
        juce::String workId;
        juce::String recordingId;
        std::int64_t issuedAtMs = 0;
        std::int64_t leaseExpiresAtMs = 0;
    };

    class RuntimeABindingRepository final
    {
    public:
        explicit RuntimeABindingRepository (juce::File transportRootIn);

        std::optional<RuntimeABinding> load (const RuntimeIdentity&,
                                             std::int64_t nowMs) const;
        juce::File bindingFile (const juce::String& runtimeInstanceId) const;

    private:
        const juce::File root;
    };
}
