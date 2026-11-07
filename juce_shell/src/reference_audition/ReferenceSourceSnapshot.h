#pragma once
#include "ReferenceSourceFile.h"
#include "ReferenceSnapshotLease.h"
#include "ReferenceVerifiedInput.h"

namespace hypha::reference_audition::revision_detail
{
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
inline std::atomic<void (*)()> snapshotCopyHookForTest { nullptr };
inline std::atomic<std::uint64_t> snapshotCopiesForTest { 0 };
inline std::atomic<juce::int64> snapshotFreeBytesForTest { -1 }, snapshotWriteLimitForTest { -1 };
inline std::atomic<juce::int64> snapshotReadBytesForTest { 0 };
inline std::atomic<void (*)()> snapshotCacheDestroyedHookForTest { nullptr };
#endif
// All snapshots use the process's temporary volume. Reserve unwritten bytes across
// concurrent copies; completed bytes are already included in the filesystem's free space.
class SnapshotStorage final
{
public:
    SnapshotStorage() = default;
    SnapshotStorage (const SnapshotStorage&) = delete;
    SnapshotStorage& operator= (const SnapshotStorage&) = delete;
    static constexpr juce::int64 headroom = 256 * 1024 * 1024;
    bool reserve (const juce::File& file, juce::int64 bytes)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        const auto free = freeBytes (file);
        if (bytes < 0 || free < headroom || pending > free - headroom || bytes > free - headroom - pending) return false;
        remaining = bytes; pending += bytes; return true;
    }
    bool hasRoom (const juce::File& file) const
    {
        const std::lock_guard<std::mutex> lock (mutex);
        const auto free = freeBytes (file);
        return free >= headroom && pending <= free - headroom;
    }
    void wrote (juce::int64 bytes)
    {
        const std::lock_guard<std::mutex> lock (mutex);
        remaining -= bytes; pending -= bytes; written += bytes; committed += bytes;
    }
    void release()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        pending -= remaining; committed -= written; remaining = written = 0;
    }
    ~SnapshotStorage() { release(); }
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    static juce::int64 reservedForTest()
    { const std::lock_guard<std::mutex> lock (mutex); return pending + committed; }
#endif
private:
    static juce::int64 freeBytes (const juce::File& file)
    {
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        if (const auto free = snapshotFreeBytesForTest.load(); free >= 0) return free - committed;
#endif
        return file.getParentDirectory().getBytesFreeOnVolume();
    }
    inline static std::mutex mutex;
    inline static juce::int64 pending = 0, committed = 0;
    juce::int64 remaining = 0, written = 0;
};
inline bool sourceCopyFailure (const juce::String& code)
{ return code == "reference_source_cache_full" || code == "reference_source_copy_failed"; }

// Prefer a private disk copy. Storage failure keeps the verified chunk receipts and
// permits authenticated direct reads without holding an exclusive source handle.
class SourceSnapshot final
{
public:
    explicit SourceSnapshot (const RuntimeSource& source) : original (source.absolutePath)
    {
        valid = prepare (source);
        if (!valid || !copied) { if (temporary.available()) temporary.payload().deleteFile(); storage.release(); }
    }
    std::shared_ptr<juce::InputStream> open() const
    {
        if (!valid) return {};
        if (copied) return std::make_shared<OpenFile> (temporary.payload());
        // Retain an open failure at the decoder boundary as retryable I/O.
        return std::make_shared<VerifiedInput> (original, verifiedRevision, hashes);
    }
    bool available() const noexcept { return valid; }
    bool hasPrivateCopy() const noexcept { return valid && copied; }
    juce::String storageNotice() const { return notice; }
    RuntimeFileRevision revision() const { return verifiedRevision; }
    juce::String reason() const { return valid ? juce::String {} : failure; }
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    juce::int64 copiedBytesForTest = 0;
    juce::File fileForTest() const { return temporary.payload(); }
#endif
private:
    // SHA256 consumes this stream once. The same chunks feed the optional copy
    // and the direct-read receipts, so the source is never read twice to prepare it.
    class Capture final : public juce::InputStream
    {
    public:
        Capture (SourceSnapshot& ownerIn, OpenFile& sourceIn)
            : owner (ownerIn), source (sourceIn) {}
        juce::int64 getTotalLength() override { return source.getTotalLength(); }
        juce::int64 getPosition() override { return position; }
        bool setPosition (juce::int64 at) override { return at == position; }
        bool isExhausted() override { return position >= getTotalLength() || failed; }
        int read (void* destination, int requested) override
        {
            int count = 0;
            while (count < requested && position < getTotalLength())
            {
                if (offset == filled)
                {
                    const auto wanted = static_cast<int> (std::min<juce::int64> (verifiedChunkBytes, getTotalLength() - position));
                    filled = 0;
                    while (filled < wanted)
                    {
                        const auto n = source.read (buffer.data() + filled, wanted - filled);
                        if (n <= 0) { failed = true; return count; }
                        filled += n;
                    }
                    offset = 0;
                    if (filled <= 0) { failed = true; return count; }
                    owner.hashes->emplace_back (buffer.data(), static_cast<size_t> (filled));
                    owner.write (buffer.data(), filled);
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
                    owner.copiedBytesForTest += filled;
                    snapshotReadBytesForTest.fetch_add (filled);
#endif
                }
                const auto n = std::min (requested - count, filled - offset);
                std::memcpy (static_cast<char*> (destination) + count, buffer.data() + offset, static_cast<size_t> (n));
                offset += n; count += n; position += n;
            }
            return count;
        }
    private:
        SourceSnapshot& owner; OpenFile& source;
        std::array<char, verifiedChunkBytes> buffer;
        int offset = 0, filled = 0;
        juce::int64 position = 0;
        bool failed = false;
    };
    bool prepare (const RuntimeSource& source)
    {
        OpenFile input (original);
        RuntimeFileRevision before, after;
        if (!input.revision (before)) return false;
        if (!sameObject (before, source.revision)) { failure = "reference_source_changed"; return false; }
        if (!temporary.available()) notice = "reference_source_copy_failed";
        else if (!storage.reserve (temporary.payload(), input.getTotalLength()))
            notice = "reference_source_cache_full";
        else
        {
            output = std::make_unique<juce::FileOutputStream> (temporary.payload());
            if (!output->openedOk()) abandonCopy ("reference_source_copy_failed");
        }
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        ++snapshotCopiesForTest;
        input.enableHashHook();
        if (auto hook = snapshotCopyHookForTest.exchange (nullptr)) hook();
#endif
        Capture capture (*this, input);
        const auto hash = juce::SHA256 (capture).toHexString();
        if (output)
        {
            output->flush();
            if (output->getStatus().failed()) abandonCopy ("reference_source_copy_failed");
        }
        copied = output != nullptr; output.reset();
        if (!input.complete() || !input.revision (after)) return false;
        if (!equal (before, after)) { failure = "reference_source_revalidating"; return false; }
        if (hash != source.sourceFileSha256) { failure = "reference_source_changed"; return false; }
        verifiedRevision = before; return true;
    }
    void abandonCopy (const char* code)
    { notice = code; output.reset(); temporary.payload().deleteFile(); storage.release(); }
    void write (const char* bytes, int count)
    {
        if (!output) return;
        if (!storage.hasRoom (temporary.payload())) { abandonCopy ("reference_source_cache_full"); return; }
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        if (const auto limit = snapshotWriteLimitForTest.load(); limit >= 0 && output->getPosition() + count > limit)
        { abandonCopy ("reference_source_copy_failed"); return; }
#endif
        if (!output->write (bytes, static_cast<size_t> (count)))
        { abandonCopy ("reference_source_copy_failed"); return; }
        storage.wrote (count);
    }
    juce::String failure { "reference_source_read_failed" }, notice;
    bool valid = false, copied = false;
    juce::File original;
    RuntimeFileRevision verifiedRevision;
    std::shared_ptr<ChunkHashes> hashes = std::make_shared<ChunkHashes>();
    SnapshotStorage storage;
    SnapshotLease temporary;
    std::unique_ptr<juce::FileOutputStream> output;
};
class SourceSnapshotCache final
{
    struct Request
    {
        std::once_flag once;
        std::shared_ptr<SourceSnapshot> copy;
    };
    struct Entry { std::weak_ptr<SourceSnapshot> copy; std::weak_ptr<Request> request; };
    std::mutex mutex;
    std::map<juce::String, Entry> copies;
public:
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    ~SourceSnapshotCache() { if (auto hook = snapshotCacheDestroyedHookForTest.load()) hook(); }
#endif
    std::shared_ptr<SourceSnapshot> get (const RuntimeSource& source, bool* freshlyVerified = nullptr)
    {
        if (freshlyVerified) *freshlyVerified = false;
        const auto key = source.absolutePath + "\n" + source.sourceFileSha256 + "\n"
            + source.revision.deviceId + ":" + source.revision.fileId + ":" + source.revision.sizeBytes;
        std::shared_ptr<Request> request;
        {
            const std::lock_guard<std::mutex> lock (mutex);
            const auto& prepared = *source.preparedAudio;
            if (prepared.snapshot && prepared.key == key) return prepared.snapshot;
            for (auto i = copies.begin(); i != copies.end();)
                if (i->second.copy.expired() && i->second.request.expired()) i = copies.erase (i); else ++i;
            auto& entry = copies[key];
            if (auto copy = entry.copy.lock()) { *source.preparedAudio = {key, copy}; return copy; }
            request = entry.request.lock();
            if (!request) entry.request = request = std::make_shared<Request>();
        }
        // Only callers for this source wait for its copy. Never hold the cache lock over I/O.
        std::call_once (request->once, [&] { request->copy = std::make_shared<SourceSnapshot> (source); });
        auto copy = request->copy;
        if (freshlyVerified) *freshlyVerified = true;
        if (copy->available())
        {
            const std::lock_guard<std::mutex> lock (mutex);
            copies[key].copy = copy;
            *source.preparedAudio = {key, copy};
        }
        return copy;
    }
};
// The revision service owns this cache and joins all workers before destroying it.
SourceSnapshotCache& snapshotCache();
inline std::shared_ptr<SourceSnapshot> snapshotFor (const RuntimeSource& source, bool* freshlyVerified = nullptr)
{ return snapshotCache().get (source, freshlyVerified); }
inline std::unique_ptr<juce::InputStream> snapshotInput (const RuntimeSource& source)
{
    const auto copy = snapshotFor (source);
    if (auto file = copy->open()) return std::make_unique<SharedInput> (std::move (file), copy);
    return std::make_unique<juce::MemoryInputStream> (nullptr, 0, false);
}
}
