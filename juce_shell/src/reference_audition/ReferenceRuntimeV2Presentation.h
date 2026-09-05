#pragma once

#include <juce_core/juce_core.h>

namespace hypha::reference_audition
{
    enum class RuntimePresentationLayout
    {
        automatic,
        main,
        equal,
    };

    class RuntimeV2PresentationRepository final
    {
    public:
        explicit RuntimeV2PresentationRepository (juce::File transportRootIn);
        RuntimePresentationLayout load (const juce::String& workId) const noexcept;

        static juce::String text (RuntimePresentationLayout);

    private:
        const juce::File root;
    };
}
