#pragma once

#include <juce_core/juce_core.h>

#include "ReferenceAuditionModel.h"

namespace hypha::reference_audition
{
    class Repository final
    {
    public:
        explicit Repository (juce::File transportRootIn = transportRoot());

        LoadResult load (const juce::String& workId) const;
        juce::String verifySourceFile (const SourceReceipt&) const;

        static juce::File transportRoot();

    private:
        const juce::File root;
    };
}
