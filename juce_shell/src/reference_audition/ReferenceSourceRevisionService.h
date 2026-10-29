#pragma once
#include "ReferenceRuntimeV2Source.h"
#include <atomic>
#include <condition_variable>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <utility>
#include <functional>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_cryptography/juce_cryptography.h>
#if JUCE_WINDOWS
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace hypha::reference_audition::revision_detail
{
inline bool equal (const RuntimeFileRevision& a, const RuntimeFileRevision& b)
{
    return a.deviceId == b.deviceId && a.fileId == b.fileId && a.sizeBytes == b.sizeBytes
        && a.modifiedTimeNs == b.modifiedTimeNs && a.changedTimeNs == b.changedTimeNs;
}
inline bool sameObject (const RuntimeFileRevision& a, const RuntimeFileRevision& b)
{ return a.deviceId == b.deviceId && a.fileId == b.fileId && a.sizeBytes == b.sizeBytes; }
inline juce::String number (std::uint64_t n) { return juce::String (std::to_string (n)); }

#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
class OpenFile;
inline std::atomic<void (*)()> hashReadHook { nullptr }, pageReadHook { nullptr };
inline std::atomic<OpenFile*> watchedPageFile { nullptr };
#endif

// The hash stream and both revision observations use this same OS handle.
class OpenFile final : public juce::InputStream
{
public:
    explicit OpenFile (const juce::File& file)
    {
#if JUCE_WINDOWS
        handle = CreateFileW (file.getFullPathName().toWideCharPointer(), GENERIC_READ,
            FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
            FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
#else
        handle = ::open (file.getFullPathName().toRawUTF8(), O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
#endif
        RuntimeFileRevision r;
        if (revision (r)) length = r.sizeBytes.getLargeIntValue();
    }
    ~OpenFile() override
    {
#if JUCE_WINDOWS
        if (handle != INVALID_HANDLE_VALUE) CloseHandle (handle);
#else
        if (handle >= 0) ::close (handle);
#endif
    }
    bool revision (RuntimeFileRevision& r) const
    {
#if JUCE_WINDOWS
        BY_HANDLE_FILE_INFORMATION i {}; FILE_BASIC_INFO b {};
        if (handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle (handle, &i)
            || !GetFileInformationByHandleEx (handle, FileBasicInfo, &b, sizeof (b))
            || (i.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT)) != 0
            || b.LastWriteTime.QuadPart < 116444736000000000LL || b.ChangeTime.QuadPart < 116444736000000000LL)
            return false;
        const auto ticks = [] (LONGLONG t) -> juce::String {
            const auto n = static_cast<std::uint64_t> (t - 116444736000000000LL);
            return n <= std::numeric_limits<std::uint64_t>::max() / 100 ? number (n * 100) : juce::String {};
        };
        r.deviceId = number (i.dwVolumeSerialNumber);
        r.fileId = number ((std::uint64_t (i.nFileIndexHigh) << 32) | i.nFileIndexLow);
        r.sizeBytes = number ((std::uint64_t (i.nFileSizeHigh) << 32) | i.nFileSizeLow);
        r.modifiedTimeNs = ticks (b.LastWriteTime.QuadPart); r.changedTimeNs = ticks (b.ChangeTime.QuadPart);
        return r.modifiedTimeNs.isNotEmpty() && r.changedTimeNs.isNotEmpty();
#else
        struct stat i {};
        if (handle < 0 || ::fstat (handle, &i) != 0 || !S_ISREG (i.st_mode) || i.st_size < 0) return false;
        r.deviceId = number (std::uint64_t (i.st_dev)); r.fileId = number (std::uint64_t (i.st_ino));
        r.sizeBytes = number (std::uint64_t (i.st_size));
#if JUCE_MAC
        const auto mt = i.st_mtimespec, ct = i.st_ctimespec;
#else
        const auto mt = i.st_mtim, ct = i.st_ctim;
#endif
        r.modifiedTimeNs = number (std::uint64_t (mt.tv_sec) * 1000000000 + std::uint64_t (mt.tv_nsec));
        r.changedTimeNs = number (std::uint64_t (ct.tv_sec) * 1000000000 + std::uint64_t (ct.tv_nsec));
        return true;
#endif
    }
    juce::int64 getTotalLength() override { return length; }
    juce::int64 getPosition() override { return position; }
    bool isExhausted() override { return failed || position >= length; }
    bool setPosition (juce::int64 p) override
    {
#if JUCE_WINDOWS
        LARGE_INTEGER v {}; v.QuadPart = p;
        if (handle == INVALID_HANDLE_VALUE || !SetFilePointerEx (handle, v, nullptr, FILE_BEGIN)) return false;
#else
        if (handle < 0 || ::lseek (handle, p, SEEK_SET) < 0) return false;
#endif
        position = p; return true;
    }
    int read (void* data, int n) override
    {
#if JUCE_WINDOWS
        DWORD got = 0; if (handle == INVALID_HANDLE_VALUE || !ReadFile (handle, data, DWORD (n), &got, nullptr)) { failed = true; return 0; }
        const auto result = int (got);
#else
        const auto got = handle < 0 ? -1 : ::read (handle, data, size_t (n));
        if (got < 0) { failed = true; return 0; } const auto result = int (got);
#endif
        position += result;
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        if (result > 0)
        {
            if (hashingForTest) { if (auto hook = hashReadHook.exchange (nullptr)) hook(); }
            else if (watchedPageFile.load() == this)
                if (auto hook = pageReadHook.exchange (nullptr)) hook();
        }
#endif
        return result;
    }
    bool complete() const { return !failed && position == length; }
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    void enableHashHook() noexcept { hashingForTest = true; }
#endif
private:
#if JUCE_WINDOWS
    HANDLE handle = INVALID_HANDLE_VALUE;
#else
    int handle = -1;
#endif
    juce::int64 length = -1, position = 0;
    bool failed = false;
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    bool hashingForTest = false;
#endif
};

class BorrowedInput final : public juce::InputStream
{
public:
    explicit BorrowedInput (OpenFile& fileIn) : file (fileIn) {}
    juce::int64 getTotalLength() override { return file.getTotalLength(); }
    bool isExhausted() override { return file.isExhausted(); }
    int read (void* data, int size) override { return file.read (data, size); }
    juce::int64 getPosition() override { return file.getPosition(); }
    bool setPosition (juce::int64 position) override { return file.setPosition (position); }
private:
    OpenFile& file;
};

class SharedInput final : public juce::InputStream
{
public:
    explicit SharedInput (std::shared_ptr<OpenFile> fileIn) : file (std::move (fileIn)) {}
    juce::int64 getTotalLength() override { return file->getTotalLength(); }
    bool isExhausted() override { return file->isExhausted(); }
    int read (void* data, int size) override { return file->read (data, size); }
    juce::int64 getPosition() override { return file->getPosition(); }
    bool setPosition (juce::int64 position) override { return file->setPosition (position); }
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    OpenFile* fileForTest() const noexcept { return file.get(); }
#endif
private:
    std::shared_ptr<OpenFile> file;
};

// currentPathRevision must reject links and use the same five-field representation.
using PathRevision = bool (*) (const juce::File&, RuntimeFileRevision&);
inline juce::String verify (const RuntimeSource& source, PathRevision pathRevision, RuntimeFileRevision& verified)
{
    const juce::File path (source.absolutePath);
    OpenFile stream (path); RuntimeFileRevision before, after, pathBefore, pathAfter;
    if (!stream.revision (before) || !pathRevision (path, pathBefore)) return "reference_source_open_failed";
    if (!sameObject (before, source.revision) || !equal (before, pathBefore)) return "reference_source_changed";
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    stream.enableHashHook();
#endif
    juce::String hash;
    {
        juce::BufferedInputStream buffered (stream, 64 * 1024);
        hash = juce::SHA256 (buffered).toHexString();
        if (buffered.getPosition() != stream.getTotalLength()) return "reference_source_changed";
    }
    if (!stream.complete() || !stream.revision (after) || !pathRevision (path, pathAfter)
        || !equal (before, after) || !equal (before, pathAfter)) return "reference_source_changed";
    if (hash != source.sourceFileSha256) return "reference_source_changed";
    if (!stream.setPosition (0)) return "reference_source_open_failed";
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::make_unique<BorrowedInput> (stream)));
    if (reader == nullptr) return "reference_source_decode_failed";
    if (static_cast<std::int64_t> (std::llround (reader->sampleRate)) != source.audio.sampleRateHz
        || reader->numChannels != source.audio.channels || reader->lengthInSamples != source.audio.totalSampleFrames)
        return "reference_source_audio_mismatch";
    if (!stream.revision (after) || !pathRevision (path, pathAfter)
        || !equal (before, after) || !equal (before, pathAfter)) return "reference_source_changed";
    verified = before; return {};
}

// One authority per registered file object: a receipt registered again for the same path and
// bytes (another file ID) must not be refused through the old receipt's mismatch.
inline juce::String keyFor (const RuntimeSource& s)
{
    return s.absolutePath + "\n" + s.sourceFileSha256 + "\n" + juce::String (s.audio.sampleRateHz)
        + ":" + juce::String (s.audio.channels) + ":" + juce::String (s.audio.totalSampleFrames)
        + "\n" + s.revision.deviceId + ":" + s.revision.fileId + ":" + s.revision.sizeBytes;
}

struct Authority { std::atomic<std::uint64_t> invalidation { 0 }; };
class Service final
{
    struct Entry
    {
        RuntimeFileRevision revision; juce::String failure;
        std::shared_ptr<Authority> authority = std::make_shared<Authority>();
        bool complete = false, queued = false, failureActive = false;
    };
    struct Job { juce::String key; RuntimeSource source; RuntimeFileRevision requested; PathRevision path; };
public:
    Service()
    {
        try { worker = std::thread ([this] { run(); }); }
        catch (...) {}
    }
    ~Service()
    {
        { std::lock_guard<std::mutex> l (mutex); stopping = true; }
        wake.notify_one();
        if (worker.joinable()) worker.join();
    }
    SourceRevisionResult check (const RuntimeSource& source, const RuntimeFileRevision& now, PathRevision path)
    {
        const auto key = keyFor (source);
        std::lock_guard<std::mutex> l (mutex);
        auto& e = entries[key];
        trim (key);
        if (!sameObject (source.revision, now))
        { markFailure (e); return { SourceRevisionState::refused, "reference_source_changed" }; }
        if (e.complete && equal (e.revision, now))
        {
            if (e.failure.isNotEmpty()) { markFailure (e); return { SourceRevisionState::refused, e.failure }; }
            e.failureActive = false; return { SourceRevisionState::matched, {} };
        }
        if (equal (source.revision, now))
        { e.failureActive = false; return { SourceRevisionState::matched, {} }; }
        if (!e.queued)
        {
            if (!worker.joinable())
            {
                try { worker = std::thread ([this] { run(); }); }
                catch (...) { return { SourceRevisionState::revalidate, {} }; }
            }
            try { jobs.push_back ({ key, source, now, path }); }
            catch (...) { return { SourceRevisionState::revalidate, {} }; }
            e.queued = true; wake.notify_one();
        }
        return { SourceRevisionState::revalidate, {} };
    }
    std::shared_ptr<Authority> watch (const RuntimeSource& source)
    {
        const auto key = keyFor (source); std::lock_guard<std::mutex> l (mutex);
        auto& e = entries[key]; trim (key); return e.authority;
    }
    void noteFailure (const RuntimeSource& source)
    {
        const auto key = keyFor (source); std::lock_guard<std::mutex> l (mutex);
        auto& e = entries[key]; trim (key); markFailure (e);
    }
    juce::String verifyInitial (const RuntimeSource& source, const RuntimeFileRevision& requested, PathRevision path)
    {
        if (!sameObject (source.revision, requested)) { noteFailure (source); return "reference_source_changed"; }
        const auto key = keyFor (source);
        {
            std::lock_guard<std::mutex> l (mutex);
            auto& e = entries[key];
            trim (key);
            if (e.complete && equal (e.revision, requested))
            {
                if (e.failure.isNotEmpty()) markFailure (e); else e.failureActive = false;
                return e.failure;
            }
            if (e.queued) return "reference_source_revalidating";
            e.queued = true;
        }
        RuntimeFileRevision verified, current;
        juce::String failure; bool stable = false;
        try
        {
            failure = verify (source, path, verified);
            stable = path (juce::File (source.absolutePath), current) && equal (current, requested);
        }
        catch (...) { failure = "reference_source_revalidating"; }
        {
            std::lock_guard<std::mutex> l (mutex);
            auto& e = entries[key]; e.queued = false;
            if (stable)
            {
                e.revision = current; e.failure = failure; e.complete = true;
                if (failure.isNotEmpty()) markFailure (e); else e.failureActive = false;
            }
            else if (failure == "reference_source_open_failed") markFailure (e);
        }
        finished.notify_all();
        return stable || failure == "reference_source_open_failed" ? failure : "reference_source_revalidating";
    }
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    std::uint64_t startedCount() const noexcept { return testStarted.load(); }
    bool waitForCompletion (const RuntimeSource& source, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> l (mutex);
        const auto key = keyFor (source);
        return finished.wait_for (l, timeout, [&] { auto i = entries.find (key); return i != entries.end() && !i->second.queued; });
    }
#endif
private:
    static void markFailure (Entry& entry)
    {
        if (!entry.failureActive) entry.authority->invalidation.fetch_add (1, std::memory_order_acq_rel);
        entry.failureActive = true;
    }
    void trim (const juce::String& preserve)
    {
        while (entries.size() > 128)
        {
            auto candidate = entries.begin();
            while (candidate != entries.end() && (candidate->first == preserve || candidate->second.queued || candidate->second.authority.use_count() > 1)) ++candidate;
            if (candidate == entries.end()) return;
            entries.erase (candidate);
        }
    }
    void run()
    {
        for (;;)
        {
            Job job;
            { std::unique_lock<std::mutex> l (mutex); wake.wait (l, [&] { return stopping || !jobs.empty(); });
              if (stopping) return; job = std::move (jobs.front()); jobs.pop_front(); }
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
            testStarted.fetch_add (1);
#endif
            RuntimeFileRevision verified, current;
            juce::String failure; bool stable = false;
            try
            {
                failure = verify (job.source, job.path, verified);
                stable = job.path (juce::File (job.source.absolutePath), current) && equal (current, job.requested);
            }
            catch (...) { failure = "reference_source_revalidating"; }
            { std::lock_guard<std::mutex> l (mutex); auto& e = entries[job.key]; e.queued = false;
              if (stable)
              {
                  e.revision = current; e.failure = failure; e.complete = true;
                  if (failure.isNotEmpty()) markFailure (e); else e.failureActive = false;
              }
              else if (failure == "reference_source_open_failed") markFailure (e);
              trim (job.key); }
            finished.notify_all();
        }
    }
    std::mutex mutex; std::condition_variable wake, finished; std::map<juce::String, Entry> entries;
    std::deque<Job> jobs; bool stopping = false;
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    std::atomic<std::uint64_t> testStarted { 0 };
#endif
    std::thread worker;
};
inline Service& service() { static Service instance; return instance; }
}
