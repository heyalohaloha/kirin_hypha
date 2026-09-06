#pragma once

#include <cstdint>

#include <juce_core/juce_core.h>

#include "ReferenceAuditionModel.h"
#include "ReferenceRuntimeV2Model.h"

namespace hypha::reference_audition
{
    constexpr std::int64_t maximumPresetAdoptionBytes = 4 * 1024;

    class PresetAdoptionTransport final
    {
    public:
        explicit PresetAdoptionTransport (juce::File transportRootIn);

        bool write (const RuntimeIdentity&, std::int64_t manifestRevision,
                    const RuntimeSourcePresetReceipt& sourceTemplateArtifact,
                    const RuntimeSourcePresetReceipt& sourcePresetArtifact,
                    std::int64_t adoptedAtMs) const;

        juce::File adoptionFile (const RuntimeIdentity&, std::int64_t manifestRevision,
                                 const RuntimeSourcePresetReceipt& sourcePresetArtifact) const;

    private:
        const juce::File root;
    };
}
