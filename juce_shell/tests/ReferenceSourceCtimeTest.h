#pragma once
#include "ReferenceVersionFormatFixture.h"
#include "ReferenceManualService.h"
#include "ReferencePageWaitTestAccess.h"
#include "reference_rt_probe.h"
#include "../src/reference_audition/ReferencePinnedSource.h"
#include "ReferenceTransientSourceTest.h"
#include <cstdio>
#include <fcntl.h>
#if !JUCE_WINDOWS
#include <unistd.h>
#endif
#if JUCE_WINDOWS
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace hypha::tests::reference_control
{
struct SourcePageTag {};
std::int64_t& sourceRetryDeadline (Runtime&, SourcePageTag);
template <auto Deadline> struct SourceRetryAccess
{
    friend std::int64_t& sourceRetryDeadline (Runtime& r, SourcePageTag) { return r.*Deadline; }
};
template struct SourceRetryAccess<&Runtime::sourceRetryAtMs>;
reference_audition::VisualObservation& sourceVisual (Comparison&, SourcePageTag);
template <auto Visual> struct SourceVisualAccess
{
    friend reference_audition::VisualObservation& sourceVisual (Comparison& c, SourcePageTag)
    { return c.*Visual; }
};
template struct SourceVisualAccess<&Comparison::visual>;
bool cachedSourcePage (reference_audition::AudioPages&, std::int64_t, SourcePageTag);
void suspendSourcePage (reference_audition::AudioPages&, SourcePageTag);
juce::AudioFormatReader* sourcePageReader (reference_audition::AudioPages&, SourcePageTag);
template <auto Reader> struct SourceReaderAccess
{
    friend juce::AudioFormatReader* sourcePageReader (reference_audition::AudioPages& p, SourcePageTag)
    { return (p.*Reader).get(); }
};
template struct SourceReaderAccess<&reference_audition::AudioPages::reader>;
template <auto Verified> struct SuspendSourcePageAccess
{
    friend void suspendSourcePage (reference_audition::AudioPages& p, SourcePageTag)
    { (p.*Verified).store (false, std::memory_order_release); }
};
template struct SuspendSourcePageAccess<&reference_audition::AudioPages::sourceVerified>;
template <auto Contains, auto Generation> struct SourcePageAccess
{
    friend bool cachedSourcePage (reference_audition::AudioPages& p, std::int64_t start, SourcePageTag)
    { return (p.*Contains) (start, (p.*Generation).load (std::memory_order_acquire)); }
};
template struct SourcePageAccess<&reference_audition::AudioPages::containsReady,
                                 &reference_audition::AudioPages::activeGeneration>;
inline void suspendDuringPageCopy (reference_audition::AudioPages& p) { suspendSourcePage (p, {}); }
}

enum class SourceChange { attributes, contents, replacement, duringHash, startPending, readerSwap, duringRender, duringPageRead, modifiedTime, observedSwap };
inline std::mutex sourceHashPauseMutex;
inline std::condition_variable sourceHashPauseCondition;
inline bool sourceHashEntered = false, sourceHashRelease = false;
inline void pauseSourceHash()
{
    std::unique_lock<std::mutex> lock (sourceHashPauseMutex);
    sourceHashEntered = true; sourceHashPauseCondition.notify_all();
    sourceHashPauseCondition.wait_for (lock, std::chrono::seconds (30), [] { return sourceHashRelease; });
}
inline juce::File* sourcePageMutationTarget = nullptr;
inline void changeAttributesDuringPageRead()
{
#if JUCE_WINDOWS
    const auto path = sourcePageMutationTarget->getFullPathName().toWideCharPointer();
    const auto attributes = GetFileAttributesW (path);
    require (attributes != INVALID_FILE_ATTRIBUTES && SetFileAttributesW (path, attributes ^ FILE_ATTRIBUTE_HIDDEN)
             && SetFileAttributesW (path, attributes), "change attributes during page read");
#else
    const auto path = sourcePageMutationTarget->getFullPathName().toRawUTF8(); struct stat info {};
    require (::stat (path, &info) == 0 && ::chmod (path, info.st_mode ^ S_IXUSR) == 0
             && ::chmod (path, info.st_mode) == 0, "change attributes during page read");
#endif
}
inline juce::File* sourceHashMutationTarget = nullptr;
inline void mutateSourceDuringHash()
{
#if JUCE_WINDOWS
    const auto name = sourceHashMutationTarget->getFullPathName();
    const auto* path = name.toWideCharPointer();
    const auto lastWrite = [path]
    {
        WIN32_FILE_ATTRIBUTE_DATA data {};
        require (GetFileAttributesExW (path, GetFileExInfoStandard, &data) != 0, "read hash-race write time");
        return (ULONGLONG (data.ftLastWriteTime.dwHighDateTime) << 32) | data.ftLastWriteTime.dwLowDateTime;
    };
    const auto previous = lastWrite();
    auto* f = _wfopen (path, L"r+b");
#else
    auto* f = std::fopen (sourceHashMutationTarget->getFullPathName().toRawUTF8(), "r+b");
#endif
    // A byte the hash has already read (the hook runs after the first 64 KiB read). The file stays
    // a valid WAV, so only a revision check can refuse it, never a later decode failure.
    require (f != nullptr && std::fseek (f, 0, SEEK_END) == 0, "open hash-race fixture");
    const long size = std::ftell (f), offset = (size < 64L * 1024 ? size : 64L * 1024) - 1;
    unsigned char byte = 0;
    require (size > 64 && std::fseek (f, offset, SEEK_SET) == 0 && std::fread (&byte, 1, 1, f) == 1
             && std::fseek (f, offset, SEEK_SET) == 0, "read hash-race byte");
    byte ^= 1;
    require (std::fwrite (&byte, 1, 1, f) == 1 && std::fclose (f) == 0, "mutate during hash");
#if JUCE_WINDOWS
    // NTFS stamps writes from a coarse clock, so two quick writes can share one LastWriteTime and
    // look like one revision. Give every mutation its own, newer LastWriteTime instead of waiting.
    if (lastWrite() <= previous)
    {
        const ULONGLONG next = previous + 10000; // 1 ms in FILETIME ticks
        const FILETIME stamp { DWORD (next), DWORD (next >> 32) };
        const auto handle = CreateFileW (path, FILE_WRITE_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                         nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        require (handle != INVALID_HANDLE_VALUE, "open hash-race write time");
        const bool stamped = SetFileTime (handle, nullptr, nullptr, &stamp) != 0;
        require (CloseHandle (handle) != 0 && stamped, "separate hash-race write time");
    }
    require (lastWrite() > previous, "hash-race write gets its own write time");
#endif
}
inline void testReferenceSourceCtime (const juce::File& sandbox, SourceChange change = SourceChange::attributes)
{
    namespace control = hypha::tests::reference_control;
    const auto root = sandbox.getChildFile ("source-ctime-" + juce::String (static_cast<int> (change)));
    require (root.createDirectory().wasOk(), "isolated ctime fixture");
    const auto base = makeWholeSongFixture (root, root.getChildFile ("input.wav"),
        "22222222-2222-4222-8222-222222222222", "33333333-3333-4333-8333-333333333333");
    const auto manifest = hypha::tests::version_formats::publish (root, base.audio, 48000, false, 0, 1);
    require (writeJson (root.getChildFile ("library/manifest.json"), manifest), "publish fixture");
    const auto id = manifest["versions"][0]["entry_id"].toString();
    const auto selection = id + "/" + id + "/" + id;
    ref::ReferenceComparisonController c (root);
    control::stopWorkers (c); c.configure ({"ctime-post", {}, 42, true}, 48000, 2);
    control::service (c); require (c.selectVersion (selection), "select V");
    c.setPresented (true); control::service (c);
    juce::AudioBuffer<float> input (2, 256), original (2, 256); int cursor = 0;
    const auto feed = [&] (bool render, bool service = true)
    {
        for (int ch = 0; ch < 2; ++ch) input.copyFrom (ch, 0, base.audio, ch, cursor, 256);
        original.makeCopyOf (input, true);
        beginReferenceRtProbe();
        c.observeTransport (cursor, true, true); c.observeAInput (input, cursor, true, true, true);
        const bool sounded = render && c.renderSelectedB (input, cursor, true, true, true);
        require (endReferenceRtProbe() == 0, "ctime callback allocates nothing");
        if (!sounded) for (int ch = 0; ch < 2; ++ch)
            require (std::memcmp (input.getReadPointer (ch), original.getReadPointer (ch), 256 * sizeof (float)) == 0,
                     "source verification wait preserves A bit for bit");
        cursor += 256;
        if (service) { control::service (c); c.servicePendingAudition (-22, -6, true); }
        return sounded;
    };
    while (cursor < 192000) feed (false);
    require (c.snapshot().versionReady && c.requestAudition (1, -22, -6) && feed (true), "V is audible before metadata change");
    auto& currentPages = control::currentPagesFor (control::roleFor (c, { 1 }), {});
    for (int slot = 1; slot <= 3; ++slot)
        control::stopPageRefill (control::currentPagesFor (control::roleFor (c, { slot }), {}));
    c.setPresented (false);
    require (((juce::Thread&) control::sourceVisual (c, {})).stopThread (-1), "stop the independent visual worker for deterministic source races");
    auto file = root.getChildFile ("version-1.wav");
    if (change == SourceChange::attributes)
    {
        testTransientSourceFailures (root, control::roleFor (c, { 1 }).visualBinding().source);
        auto& runtime = control::roleFor (c, { 1 });
        ref::revision_detail::pathFailureForTest.store (true);
        const auto before = juce::Time::currentTimeMillis();
        control::serviceFor (runtime, {});
        const auto deadline = control::sourceRetryDeadline (runtime, {});
        require (deadline >= before + 5000, "unavailable source uses retry clock");
        require (!feed (true, false), "unavailable path does not play cached audio");
        control::sourceRetryDeadline (runtime, {}) = 0;
        for (int n = 0; n < 16 && !c.snapshot().versionReady; ++n) feed (false);
        require (c.snapshot().versionReady, "source becomes ready after retry deadline");
        require (c.requestAudition (1, -22, -6) && feed (true), "same source plays after retry");
    }
    if (change == SourceChange::duringRender)
    {
        auto& runtime = control::roleFor (c, { 1 });
        const auto generation = runtime.normalSelectionTicket();
        ref::AudioPages::setRenderCopyHookForTest (&control::suspendDuringPageCopy);
        require (!feed (true), "a verification change during page copy returns A");
        require (c.snapshot().selectedVersionId == selection && runtime.normalSelectionTicket() == generation,
                 "the selection survives a verification change during page copy");
        require (feed (true), "the same V resumes when the verified page is available");
        c.selectA(); return;
    }
    if (change == SourceChange::duringPageRead)
    {
        auto& runtime = control::roleFor (c, { 1 });
        const auto source = runtime.visualBinding().source;
        require (source != nullptr, "source is available for page read race");
        auto* reader = control::sourcePageReader (currentPages, {});
        auto* input = reader ? dynamic_cast<ref::revision_detail::SharedInput*> (reader->input) : nullptr;
        require (input != nullptr, "page reader owns the pinned source handle");
        std::int64_t absent = -1;
        for (std::int64_t at = 0; at < currentPages.lengthInSamples(); at += currentPages.cachedPageFrames())
            if (!control::cachedSourcePage (currentPages, at, {})) { absent = at; break; }
        require (absent >= 0, "page read race needs an unfilled page");
        const auto hash = juce::SHA256 (file).toHexString();
        sourcePageMutationTarget = &file;
        ref::revision_detail::watchedPageFile.store (input->fileForTest());
        ref::revision_detail::pageReadHook.store (&changeAttributesDuringPageRead);
        currentPages.request (absent); control::servicePages (currentPages, {});
        require (ref::revision_detail::pageReadHook.load() == nullptr, "metadata changed during decoder read");
        ref::revision_detail::watchedPageFile.store (nullptr); sourcePageMutationTarget = nullptr;
        require (!control::cachedSourcePage (currentPages, absent, {}), "page read across revisions is discarded");
        require (hash == juce::SHA256 (file).toHexString(), "page read mutation leaves content unchanged");
        ref::RuntimeV2SourceRepository repository (root);
        auto result = repository.checkSourceRevision (*source);
        for (int attempt = 0; attempt < 4 && result.state == ref::SourceRevisionState::revalidate; ++attempt)
        {
            require (ref::revision_detail::service().waitForCompletion (*source, std::chrono::seconds (30)), "page race hash completes");
            result = repository.checkSourceRevision (*source);
        }
        require (result.state == ref::SourceRevisionState::matched, "page source revalidates after metadata change");
        currentPages.request (absent); control::servicePages (currentPages, {});
        require (control::cachedSourcePage (currentPages, absent, {}), "page is read again after verification");
        currentPages.request (cursor); control::servicePages (currentPages, {});
        require (feed (true), "same V continues after the discarded page is rebuilt");
        c.selectA(); return;
    }
    if (change == SourceChange::observedSwap)
    {
        const auto saved = root.getChildFile ("saved-source.wav");
        require (file.moveFileTo (saved) && saved.copyFileTo (file), "replace path while the original reader remains open");
        const auto source = control::roleFor (c, { 1 }).visualBinding().source;
        ref::RuntimeV2SourceRepository otherObserver (root);
        require (source && otherObserver.checkSourceRevision (*source).state == ref::SourceRevisionState::refused,
                 "another observer detects the different file identity");
        require (!feed (true, false), "another observer's definite refusal blocks the next audio callback");
        require (file.deleteFile() && saved.moveFileTo (file), "restore source after the observed replacement");
        currentPages.request (cursor); control::servicePages (currentPages, {});
        require (currentPages.sourceInvalidated(), "decoder retains the other observer's definite refusal");
        control::service (c);
        require (c.snapshot().versionSelection->rejectionCode == "reference_source_changed" && !feed (true),
                 "an observed replacement remains refused after the original path returns");
        require (c.selectVersion (selection), "a new explicit selection can retry the restored source");
        auto retry = otherObserver.checkSourceRevision (*source);
        for (int attempt = 0; attempt < 4 && retry.state == ref::SourceRevisionState::revalidate; ++attempt)
        {
            require (ref::revision_detail::service().waitForCompletion (*source, std::chrono::seconds (30)), "restored source verification completes");
            retry = otherObserver.checkSourceRevision (*source);
        }
        require (retry.state == ref::SourceRevisionState::matched, "restored source matches the registered hash");
        for (int n = 0; n < 16 && !c.snapshot().versionReady; ++n) feed (false);
        require (c.snapshot().versionReady && c.requestAudition (1, -22, -6) && feed (true),
                 "the new explicit selection can play after the restored source is verified");
        c.selectA(); return;
    }
    if (change == SourceChange::readerSwap)
    {
        const auto source = control::roleFor (c, { 1 }).visualBinding().source;
        require (source != nullptr, "selected source is available for decoder swap");
        const auto saved = root.getChildFile ("saved-source.wav");
        require (file.moveFileTo (saved) && saved.copyFileTo (file), "install alternate file at source path");
        const auto pinned = std::make_shared<ref::revision_detail::PinnedSource> (source);
        const auto alternate = root.getChildFile ("alternate-open.wav");
        require (file.moveFileTo (alternate) && saved.moveFileTo (file), "restore original source path");
        require (!pinned->sameIdentity(), "decoder handle belongs to the substituted file");
        ref::RuntimeV2SourceRepository repository (root);
        ref::AudioPages pages (ref::AudioPages::ServiceMode::manual);
        require (pages.open (*source, 48000, 2, true,
                    [&] (ref::RuntimeFileRevision& revision) { return pinned->verified (repository, revision); }, pinned->input(),
                    [pinned] { return pinned->sameIdentity(); }, [pinned] { return pinned->wasRejected(); },
                    pinned->authorityOwner(), pinned->invalidation(), pinned->initialInvalidation()).isEmpty(), "open alternate decoder in fixture");
        pages.request (0); control::servicePages (pages, {});
        require (pages.sourceInvalidated() && !pages.sourceOpen() && !pages.readyAt (0, 256),
                 "substituted decoder is definitively refused and cannot publish a page");
        c.selectA(); return;
    }
    if (change == SourceChange::duringHash)
    {
        const auto source = control::roleFor (c, { 1 }).visualBinding().source;
        require (source != nullptr, "retain verified source for hash-race test");
        sourceHashMutationTarget = &file;
        ref::revision_detail::hashReadHook.store (&mutateSourceDuringHash);
        ref::RuntimeV2SourceRepository repository (root);
#if JUCE_WINDOWS
        const auto path = file.getFullPathName().toWideCharPointer();
        const auto attributes = GetFileAttributesW (path);
        require (attributes != INVALID_FILE_ATTRIBUTES && SetFileAttributesW (path, attributes ^ FILE_ATTRIBUTE_HIDDEN)
                 && SetFileAttributesW (path, attributes), "trigger hash-race revalidation");
#else
        struct stat info {}; require (::stat (file.getFullPathName().toRawUTF8(), &info) == 0
            && ::chmod (file.getFullPathName().toRawUTF8(), info.st_mode ^ S_IXUSR) == 0
            && ::chmod (file.getFullPathName().toRawUTF8(), info.st_mode) == 0, "trigger hash-race revalidation");
#endif
        const auto requested = repository.checkSourceRevision (*source);
        require (requested.state == ref::SourceRevisionState::revalidate, "hash-race job starts");
        require (ref::revision_detail::service().waitForCompletion (*source, std::chrono::seconds (30)), "hash-race worker completes");
        require (ref::revision_detail::hashReadHook.load() == nullptr, "mutation happened during hash");
        sourceHashMutationTarget = nullptr;
        const auto result = repository.checkSourceRevision (*source);
        require (result.state != ref::SourceRevisionState::matched, "a hash read across a source mutation is not accepted");
        c.selectA();
        // The check inside one hash on its own: put the byte back so the bytes match the receipt,
        // flip it again after the hash has read it, and the read must be refused as changed.
        require (ref::revision_detail::service().waitForCompletion (*source, std::chrono::seconds (30)), "hash-race follow-up completes");
        sourceHashMutationTarget = &file; mutateSourceDuringHash();
        ref::revision_detail::hashReadHook.store (&mutateSourceDuringHash);
        ref::RuntimeFileRevision verified;
        const auto direct = ref::revision_detail::verify (*source, [] (const juce::File& f, ref::RuntimeFileRevision& r)
            { ref::revision_detail::OpenFile opened (f); return opened.revision (r); }, verified);
        require (ref::revision_detail::hashReadHook.load() == nullptr && direct == "reference_source_changed",
                 "a byte changed after the hash read it is refused by the revision check");
        sourceHashMutationTarget = nullptr; return;
    }
    const auto startedBefore = ref::revision_detail::service().startedCount();
    if (change == SourceChange::attributes || change == SourceChange::startPending || change == SourceChange::modifiedTime || change == SourceChange::contents)
    {
        sourceHashEntered = false; sourceHashRelease = false;
        ref::revision_detail::hashReadHook.store (&pauseSourceHash);
    }
    if (change == SourceChange::replacement)
    {
#if JUCE_WINDOWS
        const auto identityOf = [] (const juce::File& f) { BY_HANDLE_FILE_INFORMATION i {};
            auto h = CreateFileW (f.getFullPathName().toWideCharPointer(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
            require (h != INVALID_HANDLE_VALUE && GetFileInformationByHandle (h, &i), "read file identity");
            CloseHandle (h); return std::pair<DWORD, DWORD> { i.nFileIndexHigh, i.nFileIndexLow }; };
        const auto oldIdentity = identityOf (file);
#else
        struct stat originalFile {}; require (::stat (file.getFullPathName().toRawUTF8(), &originalFile) == 0, "read file identity");
#endif
        const auto replacement = root.getChildFile ("replacement.wav");
        require (file.copyFileTo (replacement) && replacement.replaceFileIn (file), "replace source with a different file");
#if JUCE_WINDOWS
        require (identityOf (file) != oldIdentity, "replacement changes file identity");
#else
        struct stat replaced {}; require (::stat (file.getFullPathName().toRawUTF8(), &replaced) == 0
            && (originalFile.st_dev != replaced.st_dev || originalFile.st_ino != replaced.st_ino), "replacement changes file identity");
#endif
    }
#if JUCE_WINDOWS
    const auto path = file.getFullPathName().toWideCharPointer();
    const auto attributes = GetFileAttributesW (path);
    const auto revision = [&] (BY_HANDLE_FILE_INFORMATION& info, FILE_BASIC_INFO& basic)
    {
        const auto h = CreateFileW (path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
        if (h == INVALID_HANDLE_VALUE) return false;
        const bool ok = GetFileInformationByHandle (h, &info) && GetFileInformationByHandleEx (h, FileBasicInfo, &basic, sizeof (basic));
        CloseHandle (h); return ok;
    };
    BY_HANDLE_FILE_INFORMATION before {}, after {}; FILE_BASIC_INFO beforeBasic {}, afterBasic {};
    require (attributes != INVALID_FILE_ATTRIBUTES && revision (before, beforeBasic), "read initial revision");
    const auto hash = juce::SHA256 (file).toHexString();
    if (change == SourceChange::attributes || change == SourceChange::startPending) {
    require (SetFileAttributesW (path, attributes ^ FILE_ATTRIBUTE_HIDDEN) && SetFileAttributesW (path, attributes)
             && revision (after, afterBasic), "change only attributes");
    require (before.dwVolumeSerialNumber == after.dwVolumeSerialNumber && before.nFileIndexHigh == after.nFileIndexHigh
             && before.nFileIndexLow == after.nFileIndexLow && before.nFileSizeHigh == after.nFileSizeHigh
             && before.nFileSizeLow == after.nFileSizeLow && beforeBasic.LastWriteTime.QuadPart == afterBasic.LastWriteTime.QuadPart
             && beforeBasic.ChangeTime.QuadPart != afterBasic.ChangeTime.QuadPart
             && hash == juce::SHA256 (file).toHexString(), "only ctime changes and content remains identical");
    }
    else if (change == SourceChange::contents)
    {
        auto h = CreateFileW (path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
        require (h != INVALID_HANDLE_VALUE, "open fixture for content change");
        LARGE_INTEGER at {}; at.QuadPart = 128; DWORD count = 0; unsigned char byte = 0;
        require (SetFilePointerEx (h, at, nullptr, FILE_BEGIN) && ReadFile (h, &byte, 1, &count, nullptr) && count == 1, "read fixture byte");
        byte ^= 1; require (SetFilePointerEx (h, at, nullptr, FILE_BEGIN) && WriteFile (h, &byte, 1, &count, nullptr) && count == 1, "change fixture byte");
        require (SetFileTime (h, nullptr, nullptr, &before.ftLastWriteTime), "restore fixture mtime"); CloseHandle (h);
        require (revision (after, afterBasic) && before.nFileIndexHigh == after.nFileIndexHigh
            && before.nFileIndexLow == after.nFileIndexLow && before.nFileSizeHigh == after.nFileSizeHigh
            && before.nFileSizeLow == after.nFileSizeLow && beforeBasic.LastWriteTime.QuadPart == afterBasic.LastWriteTime.QuadPart
            && hash != juce::SHA256 (file).toHexString(), "content changed with same identity, size and mtime");
    }
#else
    struct stat before {}, after {};
    require (::stat (file.getFullPathName().toRawUTF8(), &before) == 0, "stat before chmod");
    const auto hash = juce::SHA256 (file).toHexString();
    if (change == SourceChange::attributes || change == SourceChange::startPending) {
    require (::chmod (file.getFullPathName().toRawUTF8(), before.st_mode ^ S_IXUSR) == 0
             && ::chmod (file.getFullPathName().toRawUTF8(), before.st_mode) == 0
             && ::stat (file.getFullPathName().toRawUTF8(), &after) == 0, "change only attributes");
#if JUCE_MAC
    const bool sameMtime = before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec && before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec;
    const bool changedCtime = before.st_ctimespec.tv_sec != after.st_ctimespec.tv_sec || before.st_ctimespec.tv_nsec != after.st_ctimespec.tv_nsec;
#else
    const bool sameMtime = before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec;
    const bool changedCtime = before.st_ctim.tv_sec != after.st_ctim.tv_sec || before.st_ctim.tv_nsec != after.st_ctim.tv_nsec;
#endif
    require (before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_size == after.st_size
             && sameMtime && changedCtime && hash == juce::SHA256 (file).toHexString(), "only ctime changes and content remains identical");
    }
    else if (change == SourceChange::contents)
    {
        const int fd = ::open (file.getFullPathName().toRawUTF8(), O_RDWR);
        require (fd >= 0, "open fixture for content change");
        unsigned char byte = 0; require (::pread (fd, &byte, 1, 128) == 1, "read fixture byte");
        byte ^= 1; require (::pwrite (fd, &byte, 1, 128) == 1, "change fixture byte");
#if JUCE_MAC
        const struct timespec times[] = { before.st_atimespec, before.st_mtimespec };
#else
        const struct timespec times[] = { before.st_atim, before.st_mtim };
#endif
        require (::futimens (fd, times) == 0 && ::close (fd) == 0, "restore fixture mtime");
        require (::stat (file.getFullPathName().toRawUTF8(), &after) == 0
            && before.st_dev == after.st_dev && before.st_ino == after.st_ino && before.st_size == after.st_size
#if JUCE_MAC
            && before.st_mtimespec.tv_sec == after.st_mtimespec.tv_sec && before.st_mtimespec.tv_nsec == after.st_mtimespec.tv_nsec
#else
            && before.st_mtim.tv_sec == after.st_mtim.tv_sec && before.st_mtim.tv_nsec == after.st_mtim.tv_nsec
#endif
            && hash != juce::SHA256 (file).toHexString(), "content changed with same identity, size and mtime");
    }
#endif
    if (change == SourceChange::modifiedTime)
    {
        const auto source = control::roleFor (c, { 1 }).visualBinding().source;
        ref::RuntimeV2SourceRepository repository (root);
        ref::RuntimeFileRevision beforeTime, afterTime;
        require (source && repository.readSourceRevision (*source, beforeTime), "read initial source timestamp");
        const auto hashBefore = juce::SHA256 (file).toHexString();
        require (file.setLastModificationTime (juce::Time (file.getLastModificationTime().toMilliseconds() + 2000))
                 && repository.readSourceRevision (*source, afterTime), "change only modification time");
        require (beforeTime.deviceId == afterTime.deviceId && beforeTime.fileId == afterTime.fileId
                 && beforeTime.sizeBytes == afterTime.sizeBytes && beforeTime.modifiedTimeNs != afterTime.modifiedTimeNs
                 && hashBefore == juce::SHA256 (file).toHexString(), "mtime changed with same file and content");
    }
    if (change == SourceChange::attributes || change == SourceChange::startPending || change == SourceChange::modifiedTime || change == SourceChange::contents)
    {
        auto& runtime = control::roleFor (c, { 1 });
        const auto source = runtime.visualBinding().source;
        require (source != nullptr, "selected source remains available");
        auto generation = runtime.normalSelectionTicket();
        ref::RuntimeV2SourceRepository repository (root);
        require (repository.checkSourceRevision (*source).state == ref::SourceRevisionState::revalidate,
                 "metadata change requests revalidation");
        {
            std::unique_lock<std::mutex> lock (sourceHashPauseMutex);
            require (sourceHashPauseCondition.wait_for (lock, std::chrono::seconds (30), [] { return sourceHashEntered; }),
                     "worker started hash without the audio callback");
        }
        for (int n = 0; n < 8; ++n)
            require (repository.checkSourceRevision (*source).state == ref::SourceRevisionState::revalidate,
                     "duplicate requests share the in-flight verification");
        auto& pages = control::currentPagesFor (runtime, {});
        control::stopPageRefill (pages);
        std::int64_t absent = -1;
        for (std::int64_t at = 0; at < pages.lengthInSamples(); at += pages.cachedPageFrames())
            if (!control::cachedSourcePage (pages, at, {})) { absent = at; break; }
        require (absent >= 0, "an unfilled source page exists");
        pages.request (absent); control::servicePages (pages, {});
        require (!control::cachedSourcePage (pages, absent, {}), "no new page is published during verification");
        require (!feed (true), "unverified source falls back to A");
        require (runtime.normalSelectionTicket() == generation && c.snapshot().selectedVersionId == selection
                 && c.snapshot().versionSelection->rejectionCode.isEmpty(), "waiting keeps selection and generation");
        if (change == SourceChange::startPending)
        {
            c.selectA(); generation = runtime.normalSelectionTicket();
            require (c.requestAudition (1, -22, -6), "a press during revalidation is queued");
            c.servicePendingAudition (-22, -6, true);
            require (c.snapshot().pendingAudition.waiting(), "pending start is retained without source changed");
        }
        { std::lock_guard<std::mutex> lock (sourceHashPauseMutex); sourceHashRelease = true; }
        sourceHashPauseCondition.notify_all();
        auto result = repository.checkSourceRevision (*source);
        for (int attempt = 0; attempt < 4 && result.state == ref::SourceRevisionState::revalidate; ++attempt)
        {
            require (ref::revision_detail::service().waitForCompletion (*source, std::chrono::seconds (30)), "hash worker completes");
            result = repository.checkSourceRevision (*source);
        }
        require (ref::revision_detail::service().startedCount() == startedBefore + 1,
                 "identical concurrent requests start only one hash");
        if (change != SourceChange::contents && result.state != ref::SourceRevisionState::matched)
            std::cerr << "source revalidation state=" << int (result.state) << " reason=" << result.reason << '\n';
        if (change == SourceChange::attributes || change == SourceChange::startPending || change == SourceChange::modifiedTime)
        {
            require (result.state == ref::SourceRevisionState::matched, "same hash verifies changed metadata");
            pages.request (cursor); control::servicePages (pages, {});
            if (change == SourceChange::startPending) c.servicePendingAudition (-22, -6, true);
            require (feed (true) && (change == SourceChange::startPending || runtime.normalSelectionTicket() == generation),
                     "same selected V resumes automatically after verification");
        }
        else
        {
            require (result.state == ref::SourceRevisionState::refused && result.reason == "reference_source_changed",
                     "different hash becomes a definite refusal");
            control::service (c);
        }
    }
    bool resumed = false;
    for (int n = 0; n < 16; ++n) resumed = feed (true) || resumed;
    const auto state = c.snapshot();
    if (change == SourceChange::attributes || change == SourceChange::startPending || change == SourceChange::modifiedTime)
        require (state.selectedVersionId == selection && state.versionSelection->rejectionCode.isEmpty()
                 && state.versionReady && resumed, "ctime-only change retains and resumes the same V");
    else
        require (!state.versionReady && state.versionSelection->rejectionCode == "reference_source_changed"
                 && !feed (true), "changed content or file identity is rejected without playing an old page");
    c.selectA();
}
