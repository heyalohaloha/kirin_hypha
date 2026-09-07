#pragma once

#include <juce_core/juce_core.h>

#include "ReferenceAuditionModel.h"

namespace hypha::reference_audition
{
    struct RuntimeFiles
    {
        juce::File capability;
        juce::File acknowledgement;

        bool valid() const noexcept
        {
            return capability != juce::File() && acknowledgement != juce::File();
        }
    };

    RuntimeFiles runtimeFiles (const juce::File& transportRoot,
                               const RuntimeIdentity& identity);
    bool writeCapability (const juce::File& transportRoot,
                          const RuntimeIdentity& identity,
                          std::int64_t nowMs);
    bool writeAcknowledgement (const juce::File& transportRoot,
                               const RuntimeIdentity& identity,
                               const Preparation& preparation,
                               const juce::String& rejectionCode,
                               std::int64_t nowMs);
    void removeRuntimeFiles (const RuntimeFiles&);
}
