#pragma once
#include "ReferenceMetricPublication.h"
#include <mutex>

namespace hypha::reference_audition::metrics
{
// Shared B/C control-worker LRU; short metadata operations are mutex-protected. Only immutable full-source observations, never PCM or
// disk storage. Published snapshots may outlive eviction; the cache owns at most
// eight series and 32 MiB of accounted series/key storage.
class SeriesCache final
{
public:
    static constexpr std::size_t maximumSongs = 8, maximumBytes = 32u * 1024u * 1024u;
    explicit SeriesCache (std::size_t bytes = maximumBytes)
        : byteLimit (std::min (bytes, maximumBytes)) {}
    std::shared_ptr<const SeriesData> find (const juce::String& key)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        const auto found = std::find_if (entries.begin(), entries.end(), [&] (const auto& e) { return e.key == key; });
        if (found == entries.end()) return {};
        auto entry = std::move (*found);
        entries.erase (found);
        auto result = entry.series;
        entries.push_back (std::move (entry));
        return result;
    }
    bool insert (const juce::String& key, std::shared_ptr<const SeriesData> series)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        if (key.isEmpty() || !series || !series->fullScanComplete) return false;
        const auto overhead = sizeof (SeriesData) + sizeof (Entry) + key.getNumBytesAsUTF8() + 1u;
        if (overhead > byteLimit || series->frames.capacity() > (byteLimit - overhead) / sizeof (KirinReferenceMetricFrame))
            return false; // Never truncate an oversized series or evict valid entries for it.
        const auto bytes = overhead + series->frames.capacity() * sizeof (KirinReferenceMetricFrame);
        for (auto it = entries.begin(); it != entries.end(); ++it)
            if (it->key == key) { used -= it->bytes; entries.erase (it); break; }
        while (!entries.empty() && (entries.size() >= maximumSongs || bytes > byteLimit - used))
        { used -= entries.front().bytes; entries.erase (entries.begin()); }
        entries.push_back ({key, std::move (series), bytes}); used += bytes;
        return true;
    }
    std::size_t size() const { const std::lock_guard<std::mutex> lock (mutex); return entries.size(); }
    std::size_t bytes() const { const std::lock_guard<std::mutex> lock (mutex); return used; }
private:
    struct Entry { juce::String key; std::shared_ptr<const SeriesData> series; std::size_t bytes; };
    mutable std::mutex mutex;
    const std::size_t byteLimit;
    std::size_t used = 0;
    std::vector<Entry> entries;
};
}
