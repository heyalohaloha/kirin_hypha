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

        RuntimeWorkspaceLoadResult refreshLibrary (std::shared_ptr<const RuntimeWorkspace> previous = {}) const;
        bool libraryOnline (std::int64_t nowMs) const;
        // Kirin OS の準備の状態。期限の過ぎたもの（Kirin OS が閉じている）・形の違うものは無し。読めない曲は飛ばす。
        std::shared_ptr<const RuntimeLibraryPreparation> libraryPreparation (std::int64_t nowMs) const;

        static juce::File transportRoot();

    private:
        const juce::File root;
    };
}
