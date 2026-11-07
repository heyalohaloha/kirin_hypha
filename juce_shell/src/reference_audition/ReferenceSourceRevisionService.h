#pragma once
#include "ReferenceRuntimeV2Source.h"
#include <atomic>
#include <array>
#include <algorithm>
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

#include "ReferenceSourceFile.h"
#include "ReferenceSourceSnapshot.h"

namespace hypha::reference_audition::revision_detail
{
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER) || defined(KIRIN_HYPHA_TEST_WALL_CLOCK)
inline std::atomic<void (*) (const RuntimeSource&)> sourceInitialFormatOpenHookForTest { nullptr };
#endif
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
inline std::atomic<std::int64_t> monitorClockOffsetMsForTest { 0 };
inline std::atomic<void (*)()> sourceServiceStoppingHookForTest { nullptr };
#endif
// currentPathRevision must reject links and use the same five-field representation.
using PathRevision = bool (*) (const juce::File&, RuntimeFileRevision&);
inline juce::String verify (const RuntimeSource& source, PathRevision pathRevision, RuntimeFileRevision& verified,
                           bool prepareAudio = false, SourceSnapshotCache& snapshots = snapshotCache())
{
    const juce::File path (source.absolutePath);
    if (prepareAudio)
    {
        bool fresh = false;
        const auto snapshot = snapshots.get (source, &fresh);
        if (!snapshot->available()) return snapshot->reason();
        RuntimeFileRevision current;
        if (!pathRevision (path, current)) return "reference_source_read_failed";
        if (fresh && equal (current, snapshot->revision()))
        {
            juce::AudioFormatManager formats; formats.registerBasicFormats();
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER) || defined(KIRIN_HYPHA_TEST_WALL_CLOCK)
            if (auto hook = sourceInitialFormatOpenHookForTest.load()) hook (source);
#endif
            auto input = snapshot->open();
            if (!input) return "reference_source_read_failed";
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (
                std::make_unique<SharedInput> (input, snapshot)));
            if (auto* direct = dynamic_cast<VerifiedInput*> (input.get()); direct && direct->takeReadFailure())
                return "reference_source_revalidating";
            if (!reader) return "reference_source_decode_failed";
            if (static_cast<std::int64_t> (std::llround (reader->sampleRate)) != source.audio.sampleRateHz
                || reader->numChannels != source.audio.channels || reader->lengthInSamples != source.audio.totalSampleFrames)
                return "reference_source_audio_mismatch";
            RuntimeFileRevision after;
            if (!pathRevision (path, after)) return "reference_source_read_failed";
            if (!equal (current, after)) return "reference_source_revalidating";
            verified = current; return {};
        }
        // A shared immutable copy does not attest to later changes of the published file.
    }
    OpenFile stream (path); RuntimeFileRevision before, after, pathBefore, pathAfter;
    if (!stream.revision (before) || !pathRevision (path, pathBefore)) return "reference_source_read_failed";
    if (!equal (before, pathBefore)) return "reference_source_revalidating";
    if (!sameObject (before, source.revision)) return "reference_source_changed";
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    stream.enableHashHook();
#endif
    juce::String hash;
    {
        juce::BufferedInputStream buffered (stream, 64 * 1024);
        hash = juce::SHA256 (buffered).toHexString();
        if (buffered.getPosition() != stream.getTotalLength()) return "reference_source_read_failed";
    }
    if (!stream.complete() || !stream.revision (after) || !pathRevision (path, pathAfter)) return "reference_source_read_failed";
    if (!equal (before, after) || !equal (before, pathAfter)) return "reference_source_revalidating";
    if (hash != source.sourceFileSha256) return "reference_source_changed";
    if (!stream.setPosition (0)) return "reference_source_read_failed";
    juce::AudioFormatManager formats; formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (std::make_unique<BorrowedInput> (stream)));
    if (stream.readFailed()) return "reference_source_read_failed";
    if (reader == nullptr) return "reference_source_decode_failed";
    if (static_cast<std::int64_t> (std::llround (reader->sampleRate)) != source.audio.sampleRateHz
        || reader->numChannels != source.audio.channels || reader->lengthInSamples != source.audio.totalSampleFrames)
        return "reference_source_audio_mismatch";
    if (!stream.revision (after) || !pathRevision (path, pathAfter)) return "reference_source_read_failed";
    if (!equal (before, after) || !equal (before, pathAfter)) return "reference_source_revalidating";
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

struct Authority
{
    std::atomic<std::uint64_t> invalidation { 0 }, unavailable { 0 };
};
class Service final
{
    struct Entry
    {
        RuntimeFileRevision revision; juce::String failure, transientFailure;
        std::shared_ptr<Authority> authority = std::make_shared<Authority>();
        bool complete = false, queued = false, failureActive = false;
        std::int64_t nextMonitorMs = 0, auditAtMs = 0, missingSinceMs = -1;
    };
    struct Job { juce::String key; RuntimeSource source; RuntimeFileRevision requested; PathRevision path; bool monitor = false; };
public:
    Service()
    {
        startWorkers();
    }
    ~Service()
    {
        { std::lock_guard<std::mutex> l (mutex); stopping = true; }
        wake.notify_all();
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
        if (auto hook = sourceServiceStoppingHookForTest.load()) hook();
#endif
        for (auto& worker : workers) if (worker.joinable()) worker.join();
    }
    SourceSnapshotCache& snapshotCache() noexcept { return snapshots; }
    SourceRevisionResult check (const RuntimeSource& source, const RuntimeFileRevision& now, PathRevision path)
    {
        const auto key = keyFor (source);
        std::lock_guard<std::mutex> l (mutex);
        auto& e = entries[key];
        trim (key);
        if (!sameObject (source.revision, now))
        { return { SourceRevisionState::refused, "reference_source_changed" }; }
        if (e.transientFailure.isNotEmpty()) return { SourceRevisionState::revalidate, e.transientFailure };
        if (e.complete && equal (e.revision, now))
        {
            if (e.failure.isNotEmpty()) return { SourceRevisionState::refused, e.failure };
            e.failureActive = false; return { SourceRevisionState::matched, {} };
        }
        if (equal (source.revision, now))
        { e.failureActive = false; return { SourceRevisionState::matched, {} }; }
        if (!e.queued)
        {
            if (!startWorkers()) return { SourceRevisionState::revalidate, {} };
            try { jobs.push_back ({ key, source, now, path }); }
            catch (...) { return { SourceRevisionState::revalidate, {} }; }
            e.queued = true; wake.notify_one();
        }
        return { SourceRevisionState::revalidate, {} };
    }
    // Each receipt has one probe in flight. A bounded worker pool lets other sources
    // recover while one source's stat or hash is waiting for storage.
    std::uint64_t monitor (const RuntimeSource& source, PathRevision path)
    {
        const auto key = keyFor (source);
        const auto now = monotonicMs();
        std::lock_guard<std::mutex> l (mutex);
        auto& e = entries[key]; trim (key);
        if (!e.queued && now >= e.nextMonitorMs && startWorkers())
        {
            jobs.push_back ({ key, source, {}, path, true });
            e.queued = true; e.nextMonitorMs = now + 1000; wake.notify_one();
        }
        return e.authority->unavailable.load (std::memory_order_acquire);
    }
    std::shared_ptr<Authority> watch (const RuntimeSource& source)
    {
        const auto key = keyFor (source); std::lock_guard<std::mutex> l (mutex);
        auto& e = entries[key]; trim (key); return e.authority;
    }
    void requestAudit (const RuntimeSource& source) noexcept
    {
        try
        {
            const std::lock_guard<std::mutex> lock (mutex);
            auto i = entries.find (keyFor (source));
            if (i != entries.end() && !i->second.failureActive && i->second.missingSinceMs < 0)
            { i->second.auditAtMs = 0; i->second.nextMonitorMs = 0; }
        }
        catch (...) {} // The unverified fill still fails safely if its audit cannot be requested.
    }
    void retry (const RuntimeSource& source)
    {
        std::lock_guard<std::mutex> l (mutex);
        auto i = entries.find (keyFor (source));
        if (i != entries.end())
        {
            auto& entry = i->second;
            // Another role may select the same freshly verified source. Reuse that
            // success; only failed or unavailable attempts need their cache retired.
            if (entry.failure.isNotEmpty() || entry.transientFailure.isNotEmpty()
                || entry.authority->unavailable.load (std::memory_order_acquire) != 0)
                entry.complete = false;
            entry.transientFailure.clear();
        }
    }
    juce::String verifyInitial (const RuntimeSource& source, const RuntimeFileRevision& requested, PathRevision path)
    {
        if (!sameObject (source.revision, requested)) return "reference_source_changed";
        const auto key = keyFor (source);
        {
            std::lock_guard<std::mutex> l (mutex);
            auto& e = entries[key];
            trim (key);
            if (e.complete && equal (e.revision, requested))
            {
                if (e.failure.isEmpty()) markAvailable (e);
                return e.failure;
            }
            if (e.queued) return "reference_source_revalidating";
            e.queued = true;
        }
        RuntimeFileRevision verified, current;
        juce::String failure; bool stable = false;
        try
        {
            failure = verify (source, path, verified, true, snapshots);
            stable = path (juce::File (source.absolutePath), current) && equal (current, requested);
        }
        catch (...) { failure = "reference_source_revalidating"; }
        {
            std::lock_guard<std::mutex> l (mutex);
            auto& e = entries[key]; e.queued = false;
            if (stable && failure != "reference_source_read_failed" && failure != "reference_source_revalidating")
            {
                e.revision = current; e.failure = failure; e.complete = true; e.transientFailure.clear();
                e.auditAtMs = monotonicMs() + auditIntervalMs; e.nextMonitorMs = monotonicMs() + 1000;
                if (failure.isEmpty()) markAvailable (e);
            }
        }
        finished.notify_all();
        return stable || failure == "reference_source_read_failed" ? failure : "reference_source_revalidating";
    }
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    void pollMonitorForTest (const RuntimeSource& source)
    { std::lock_guard<std::mutex> l (mutex); entries[keyFor (source)].nextMonitorMs = 0; }
    void advanceMonitorForTest (const RuntimeSource& source, std::int64_t missingMs = 0, bool rehash = true)
    {
        std::lock_guard<std::mutex> l (mutex); auto& e = entries[keyFor (source)];
        e.nextMonitorMs = 0; e.auditAtMs = rehash ? 0 : monotonicMs() + 60000;
        if (missingMs) e.missingSinceMs = monotonicMs() - missingMs;
    }
    std::uint64_t startedCount() const noexcept { return testStarted.load(); }
    bool waitForIdle (std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> l (mutex);
        return finished.wait_for (l, timeout, [&] {
            for (const auto& item : entries) if (item.second.queued) return false;
            return true;
        });
    }
    bool waitForCompletion (const RuntimeSource& source, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> l (mutex);
        const auto key = keyFor (source);
        return finished.wait_for (l, timeout, [&] { auto i = entries.find (key); return i != entries.end() && !i->second.queued; });
    }
#endif
private:
    static constexpr std::int64_t auditIntervalMs = 5 * 60 * 1000;
    bool startWorkers()
    {
        for (auto& worker : workers)
            if (!worker.joinable())
            { try { worker = std::thread ([this] { run(); }); } catch (...) {} }
        return std::any_of (workers.begin(), workers.end(), [] (const auto& worker) { return worker.joinable(); });
    }
    static std::int64_t monotonicMs()
    {
        return std::chrono::duration_cast<std::chrono::milliseconds> (
            std::chrono::steady_clock::now().time_since_epoch()).count()
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
            + monitorClockOffsetMsForTest.load()
#endif
            ;
    }
    void monitorOnce (const Job& job)
    {
        RuntimeFileRevision current, verified;
        juce::String failure;
        const auto now = monotonicMs();
        bool cached = false;
        if (!job.path (juce::File (job.source.absolutePath), current)) failure = "reference_source_read_failed";
        else
        {
            { std::lock_guard<std::mutex> l (mutex); const auto& e = entries[job.key];
              cached = e.complete && e.missingSinceMs < 0 && equal (e.revision, current) && now < e.auditAtMs;
              if (cached) failure = e.failure; }
            if (!cached) failure = verify (job.source, job.path, verified, false, snapshots);
        }
        const auto completedAt = monotonicMs();
        std::lock_guard<std::mutex> l (mutex);
        auto& e = entries[job.key]; e.queued = false;
        e.nextMonitorMs = completedAt + 1000;
        if (failure.isEmpty())
        {
            e.revision = cached ? current : verified; e.complete = true;
            e.failure.clear(); e.transientFailure.clear();
            if (!cached) e.auditAtMs = completedAt + auditIntervalMs;
            markAvailable (e);
        }
        else
        {
            // A writer may truncate and restore this path, or change attributes during a
            // hash. Keep verified private PCM through the same short retry window as I/O.
            if (e.missingSinceMs < 0) e.missingSinceMs = now;
            if ((e.failureActive && failure == e.failure && equal (current, e.revision))
                || now - e.missingSinceMs >= 3000)
            {
                if (failure == "reference_source_read_failed" || failure == "reference_source_revalidating")
                    e.authority->unavailable.store (1, std::memory_order_release);
                else
                {
                    e.revision = current; e.complete = true; e.failure = failure; e.missingSinceMs = -1;
                    if (!cached) e.auditAtMs = completedAt + auditIntervalMs;
                    markFailure (e);
                }
            }
        }
    }
    static void markAvailable (Entry& entry)
    {
        entry.failureActive = false;
        entry.missingSinceMs = -1;
        entry.authority->unavailable.store (0, std::memory_order_release);
    }
    static void markFailure (Entry& entry)
    {
        if (!entry.failureActive) entry.authority->invalidation.fetch_add (1, std::memory_order_acq_rel);
        entry.failureActive = true;
        entry.authority->unavailable.store (sourceFailureValue (entry.failure), std::memory_order_release);
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
            if (job.monitor)
            {
                try { monitorOnce (job); }
                catch (...) { std::lock_guard<std::mutex> l (mutex); entries[job.key].queued = false; }
                finished.notify_all(); continue;
            }
            RuntimeFileRevision verified, current;
            juce::String failure; bool stable = false;
            try
            {
                failure = verify (job.source, job.path, verified, true, snapshots);
                stable = job.path (juce::File (job.source.absolutePath), current) && equal (current, job.requested);
            }
            catch (...) { failure = "reference_source_revalidating"; }
            { std::lock_guard<std::mutex> l (mutex); auto& e = entries[job.key]; e.queued = false;
              if (stable && failure != "reference_source_read_failed" && failure != "reference_source_revalidating")
              {
                  e.revision = current; e.failure = failure; e.complete = true; e.transientFailure.clear();
                e.auditAtMs = monotonicMs() + auditIntervalMs; e.nextMonitorMs = monotonicMs() + 1000;
                  // Refusing a new reader does not revoke an existing verified copy.
                  // Only monitorOnce may confirm a failure after its retry window.
                  if (failure.isEmpty()) markAvailable (e);
              }
              // The worker keeps a read failure until a fresh attempt (Service::retry) drops it, so
              // polls that only wait for that attempt do not queue another hash.
              else if (failure == "reference_source_read_failed") e.transientFailure = failure;
              trim (job.key); }
            finished.notify_all();
        }
    }
    SourceSnapshotCache snapshots;
    std::mutex mutex; std::condition_variable wake, finished; std::map<juce::String, Entry> entries;
    std::deque<Job> jobs; bool stopping = false;
#if defined(KIRIN_REFERENCE_TEST_COMMIT_OBSERVER)
    std::atomic<std::uint64_t> testStarted { 0 };
#endif
    std::array<std::thread, 4> workers;
};
inline Service& service() { static Service instance; return instance; }
inline SourceSnapshotCache& snapshotCache() { return service().snapshotCache(); }
}
