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
inline std::atomic<int> verifyFailureForTest { 0 };
inline std::atomic<bool> persistentReadFailureForTest { false };
inline std::atomic<bool> pathFailureForTest { false }, persistentPathFailureForTest { false };
inline std::atomic<std::uint64_t> pathChecksForTest { 0 }, sourceHashBytesForTest { 0 }, originalReadBytesForTest { 0 };
inline std::mutex originalReadMutexForTest;
inline juce::String originalReadPathForTest;
inline void observeOriginalReadsForTest (const juce::String& path)
{
    const std::lock_guard<std::mutex> lock (originalReadMutexForTest);
    originalReadPathForTest = path; originalReadBytesForTest.store (0);
}
inline std::atomic<const RuntimeSource*> unchangedMetadataForTest { nullptr };
inline bool unchangedRevisionForTest (const juce::String& path, RuntimeFileRevision& revision)
{
    const auto* source = unchangedMetadataForTest.load();
    if (!source || path != source->absolutePath) return false;
    revision = source->revision; return true;
}
#endif

// The hash stream and both revision observations use this same OS handle.
class OpenFile final : public juce::InputStream
{
public:
    explicit OpenFile (const juce::File& file)
    {
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        pathForTest = file.getFullPathName();
        int expected = 1;
        if (verifyFailureForTest.compare_exchange_strong (expected, 0)) return;
#endif
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
        if (r.modifiedTimeNs.isEmpty() || r.changedTimeNs.isEmpty()) return false;
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
#endif
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        unchangedRevisionForTest (pathForTest, r);
#endif
        return true;
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
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        int expected = 2;
        if (hashingForTest && hashReadStarted && (persistentReadFailureForTest.load()
            || verifyFailureForTest.compare_exchange_strong (expected, 0)))
        { failed = true; return 0; }
#endif
#if JUCE_WINDOWS
        DWORD got = 0; if (handle == INVALID_HANDLE_VALUE || !ReadFile (handle, data, DWORD (n), &got, nullptr)) { failed = true; return 0; }
        const auto result = int (got);
#else
        const auto got = handle < 0 ? -1 : ::read (handle, data, size_t (n));
        if (got < 0) { failed = true; return 0; } const auto result = int (got);
#endif
        if (n > 0 && result == 0 && position < length) failed = true;
        position += result;
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        if (result > 0)
        {
            { const std::lock_guard<std::mutex> lock (originalReadMutexForTest);
              if (pathForTest == originalReadPathForTest) originalReadBytesForTest.fetch_add (static_cast<std::uint64_t> (result)); }
            if (hashingForTest) { sourceHashBytesForTest.fetch_add (static_cast<std::uint64_t> (result)); hashReadStarted = true; if (auto hook = hashReadHook.exchange (nullptr)) hook(); }
            else if (watchedPageFile.load() == this)
                if (auto hook = pageReadHook.exchange (nullptr)) hook();
        }
#endif
        return result;
    }
    bool complete() const { return !failed && position == length; }
    bool readFailed() const { return failed; }
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
    juce::String pathForTest;
    bool hashingForTest = false, hashReadStarted = false;
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
    explicit SharedInput (std::shared_ptr<juce::InputStream> fileIn, std::shared_ptr<void> ownerIn = {})
        : owner (std::move (ownerIn)), file (std::move (fileIn)) {}
    juce::int64 getTotalLength() override { return file->getTotalLength(); }
    bool isExhausted() override { return file->isExhausted(); }
    int read (void* data, int size) override { return file->read (data, size); }
    std::shared_ptr<juce::InputStream> sharedStream() const { return file; }
    std::shared_ptr<void> lifetime() const { return owner; }
    juce::int64 getPosition() override { return file->getPosition(); }
    bool setPosition (juce::int64 position) override { return file->setPosition (position); }
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    OpenFile* fileForTest() const noexcept { return dynamic_cast<OpenFile*> (file.get()); }
#endif
private:
    std::shared_ptr<void> owner;
    std::shared_ptr<juce::InputStream> file;
};

}
