#pragma once

#include <cstdint>
#include <functional>
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
        using ReadClock = std::function<std::int64_t()>;
        explicit RuntimeABindingRepository (juce::File transportRootIn,
            ReadClock readClockIn = [] { return juce::Time::currentTimeMillis(); });

        // Production reads validate the parsed snapshot against the time after reading it.
        std::optional<RuntimeABinding> loadCurrent (const RuntimeIdentity&) const;
        // Explicit fixed time remains available for deterministic protocol boundary tests.
        std::optional<RuntimeABinding> load (const RuntimeIdentity&,
                                             std::int64_t nowMs) const;
        juce::File bindingFile (const juce::String& runtimeInstanceId) const;

    private:
        std::optional<RuntimeABinding> loadWithClock (const RuntimeIdentity&, const ReadClock&) const;
        const juce::File root;
        const ReadClock readClock;
    };
}
