#include "ReferenceRuntimeV2SourceCache.h"

#include <algorithm>
#include <utility>

namespace hypha::reference_audition
{
    std::shared_ptr<const RuntimeSource> RuntimeV2SourceCache::find (
        const juce::String& artifactSha256)
    {
        const auto match = std::find_if (entries.begin(), entries.end(), [&] (const auto& entry)
        {
            return entry.artifactSha256 == artifactSha256;
        });
        if (match == entries.end())
            return {};

        auto source = match->source;
        if (match != entries.begin())
        {
            Entry recent { match->artifactSha256, source };
            entries.erase (match);
            entries.push_front (std::move (recent));
        }
        return source;
    }

    void RuntimeV2SourceCache::remember (
        juce::String artifactSha256,
        std::shared_ptr<const RuntimeSource> source)
    {
        if (artifactSha256.isEmpty() || source == nullptr)
            return;
        forget (artifactSha256);
        entries.push_front ({ std::move (artifactSha256), std::move (source) });
        while (entries.size() > capacity)
            entries.pop_back();
    }

    void RuntimeV2SourceCache::forget (const juce::String& artifactSha256)
    {
        entries.erase (std::remove_if (entries.begin(), entries.end(), [&] (const auto& entry)
        {
            return entry.artifactSha256 == artifactSha256;
        }), entries.end());
    }

    void RuntimeV2SourceCache::clear() noexcept
    {
        entries.clear();
    }
}
