#pragma once
#include "ReferenceSourceFile.h"
#include <array>
#include <vector>

namespace hypha::reference_audition::revision_detail
{
inline constexpr int verifiedChunkBytes = 64 * 1024;
using ChunkHashes = std::vector<juce::SHA256>;

// Used only by the non-RT decoder when temporary storage cannot hold a copy.
// Every complete chunk is authenticated against the initial full-file verification
// before any of its bytes reach the decoder, including writes with unchanged metadata.
class VerifiedInput final : public juce::InputStream
{
public:
    VerifiedInput (const juce::File& pathIn, RuntimeFileRevision revisionIn, std::shared_ptr<const ChunkHashes> hashesIn)
        : path (pathIn), expected (std::move (revisionIn)), hashes (std::move (hashesIn)), length (expected.sizeBytes.getLargeIntValue())
    {
        OpenFile file (path); RuntimeFileRevision current;
        opened = file.revision (current) && sameObject (current, expected);
        if (!opened) { failed.store (true); ++failures; }
    }
    bool available() const { return opened; }
    bool takeReadFailure() { return failed.exchange (false); }
    std::uint64_t failureCount() const noexcept { return failures.load(); }
    bool restart()
    {
        position = 0; cached = std::numeric_limits<size_t>::max(); failed.store (false); return opened;
    }
    juce::int64 getTotalLength() override { return length; }
    juce::int64 getPosition() override { return position; }
    bool isExhausted() override { return failed.load() || position >= length; }
    bool setPosition (juce::int64 at) override
    {
        if (at < 0 || at > length) return false;
        position = at; return true;
    }
    int read (void* destination, int requested) override
    {
        if (!opened || failed.load() || requested <= 0) return 0;
        int copied = 0;
        while (copied < requested && position < length)
        {
            const auto index = static_cast<size_t> (position / verifiedChunkBytes);
            if (cached != index)
            {
                const auto start = static_cast<juce::int64> (index) * verifiedChunkBytes;
                const auto size = static_cast<int> (std::min<juce::int64> (verifiedChunkBytes, length - start));
                if (index >= hashes->size() || !readChunk (size, start)
                    || juce::SHA256 (buffer.data(), static_cast<size_t> (size)) != (*hashes)[index])
                { cached = std::numeric_limits<size_t>::max(); failed.store (true); ++failures; return 0; }
                cached = index;
            }
            const auto offset = static_cast<int> (position % verifiedChunkBytes);
            const auto count = static_cast<int> (std::min<juce::int64> (
                std::min (requested - copied, verifiedChunkBytes - offset), length - position));
            std::memcpy (static_cast<char*> (destination) + copied, buffer.data() + offset, static_cast<size_t> (count));
            copied += count; position += count;
        }
        return copied;
    }
private:
    bool readChunk (int size, juce::int64 start)
    {
        // The handle lasts for one authenticated chunk, leaving idle decoders
        // compatible with writers that require exclusive access on Windows.
        OpenFile file (path); RuntimeFileRevision revision;
        if (!file.revision (revision) || !sameObject (revision, expected) || !file.setPosition (start)) return false;
        int filled = 0;
        while (filled < size)
        {
            const auto n = file.read (buffer.data() + filled, size - filled);
            if (n <= 0) return false;
            filled += n;
        }
        return true;
    }
    juce::File path;
    RuntimeFileRevision expected;
    std::shared_ptr<const ChunkHashes> hashes;
    std::array<char, verifiedChunkBytes> buffer;
    juce::int64 length = -1, position = 0;
    size_t cached = std::numeric_limits<size_t>::max();
    bool opened = false;
    std::atomic<bool> failed { false };
    std::atomic<std::uint64_t> failures { 0 };
};
}
