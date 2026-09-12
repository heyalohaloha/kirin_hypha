#pragma once

#include <cstddef>
#include <deque>
#include <memory>

#include "ReferenceRuntimeV2Source.h"

namespace hypha::reference_audition
{
    // Worker-thread-only cache. A content-addressed Source artifact is fully
    // verified once, then later B recalls require only the current file
    // revision check before the audio reader is reopened.
    class RuntimeV2SourceCache final
    {
    public:
        static constexpr size_t capacity = 3;

        std::shared_ptr<const RuntimeSource> find (const juce::String& artifactSha256);
        void remember (juce::String artifactSha256,
                       std::shared_ptr<const RuntimeSource> source);
        void forget (const juce::String& artifactSha256);
        void clear() noexcept;
        size_t size() const noexcept { return entries.size(); }

    private:
        struct Entry
        {
            juce::String artifactSha256;
            std::shared_ptr<const RuntimeSource> source;
        };

        std::deque<Entry> entries;
    };
}
