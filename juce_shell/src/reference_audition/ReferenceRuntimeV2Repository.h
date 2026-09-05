#pragma once

#include "ReferenceRuntimeV2Model.h"

namespace hypha::reference_audition
{
    class RuntimeV2Repository final
    {
    public:
        explicit RuntimeV2Repository (juce::File transportRootIn = transportRoot());

        RuntimeWorkspaceLoadResult refresh (
            const juce::String& workId,
            std::shared_ptr<const RuntimeWorkspace> previous = {}) const;

        static juce::File transportRoot();

    private:
        const juce::File root;
    };
}
