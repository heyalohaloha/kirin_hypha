#include "TestPathBudget.h"
#include "ReferenceMetricCueCoverageTest.h"
#include "TestTemporaryDirectory.h"
#include "AtomicFile.h"
#include "reference_audition/ReferenceMetricSourceScan.h"
#include "reference_audition/ReferenceMetricRuntime.h"
#include "reference_audition/ReferenceTestCommitObserver.h"
#include <iostream>
#include <stdexcept>
#include <condition_variable>
#include <mutex>
#include <chrono>
#include <ctime>

// This isolated target uses copy/failure hooks, not the runtime worker-stage recorder.
namespace hypha::reference_audition
{
void stageReferenceTestWorker (const void*, const char*) noexcept {}
void notifyReferenceTestCommit() noexcept {}
}
namespace ref = hypha::reference_audition;
namespace metric = ref::metrics;
namespace revision = ref::revision_detail;
namespace
{
void require (bool condition, const char* message)
{ if (!condition) throw std::runtime_error (message); }
struct Fixture
{
    juce::File root = hypha::tests::temporaryParent()
        .getNonexistentChildFile ("hmrs", "", false);
    ref::RuntimeV2SourceRepository repository {root};
    std::shared_ptr<ref::RuntimeSource> source = std::make_shared<ref::RuntimeSource>();
    Fixture (int rate = 48000, int channels = 2, int length = 24000)
    {
        require (root.createDirectory().wasOk(), "create isolated fixture");
        const auto file = root.getChildFile ("fixture.wav");
        juce::WavAudioFormat format;
        auto stream = file.createOutputStream();
        require (stream != nullptr, "create output stream");
        std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
            stream.get(), rate, static_cast<unsigned int> (channels), 24, {}, 0));
        require (writer != nullptr, "create WAV writer");
        stream.release();
        juce::AudioBuffer<float> data (channels, length);
        for (int c = 0; c < channels; ++c)
            for (int f = 0; f < length; ++f)
                data.setSample (c, f, static_cast<float> (0.1 * std::sin ((f + c * 31) * 0.03)));
        require (writer->writeFromAudioSampleBuffer (data, 0, length), "write fixture WAV");
        writer.reset();
        source->absolutePath = file.getFullPathName();
        source->sourceFileSha256 = juce::SHA256 (file).toHexString();
        source->audio = {rate, channels, length};
        require (repository.readSourceRevision (*source, source->revision), "read fixture revision");
    }
    ~Fixture() { hypha::tests::removeFixtureDirectory (root); }
};
void verifyPreparedScan()
{
    Fixture f;
    std::atomic<bool> cancel {false};
    auto& cache = revision::snapshotCache();
    const auto before = revision::snapshotCopiesForTest.load();
    require (!cache.prepared (*f.source), "peek miss has no snapshot");
    const auto missing = metric::scanPreparedSource (f.source, f.root, 48000, 2, false, 100, cancel);
    require (missing.scan.state == metric::ScanState::waiting, "unprepared source waits");
    require (revision::snapshotCopiesForTest.load() == before, "peek/scan miss performs zero copies");
    const auto copy = cache.get (*f.source);
    require (copy->hasPrivateCopy() && cache.prepared (*f.source) == copy, "prepared owner retained");
    const auto preparedCount = revision::snapshotCopiesForTest.load();
    auto result = metric::scanPreparedSource (f.source, f.root, 48000, 2, false, 100, cancel);
    require (result.scan.state == metric::ScanState::complete && result.scan.frames.size() == 5,
             "real private WAV reader produces complete five-aperture native series");
    auto again = metric::scanPreparedSource (f.source, f.root, 48000, 2, false, 100, cancel);
    require (again.scan.state == metric::ScanState::complete, "repeated prepared reader completes");
    require (revision::snapshotCopiesForTest.load() == preparedCount, "reader does not copy source again");
    require (again.scan.frames.size() == result.scan.frames.size(), "repeat frame count");
    for (std::size_t i = 0; i < result.scan.frames.size(); ++i)
    {
        const auto& a = result.scan.frames[i]; const auto& b = again.scan.frames[i];
        const auto same = [] (double x, double y) { return x == y || (std::isnan (x) && std::isnan (y)); };
        require (a.end_sample == b.end_sample && same (a.crest_db, b.crest_db)
            && same (a.psr_db, b.psr_db) && same (a.sharpness_acum, b.sharpness_acum),
            "repeat native observations identical");
    }
    const metric::ScanWindow partial { 4800, 24000, { 9601, 24000 } };
    const auto cueOnly = metric::scanPreparedSource (f.source, f.root, 48000, 2, false, 100, cancel, {}, partial);
    require (cueOnly.scan.state == metric::ScanState::complete && !cueOnly.scan.wholeSource
        && cueOnly.scan.scannedFrom == 4800 && cueOnly.scan.frames.size() == 3
        && cueOnly.scan.frames.front().end_sample == 14400, "private reader seek retains absolute Cue endpoints only");
    const auto requested = metric::scanPreparedSource (f.source, f.root, 48000, 2, false, 100, cancel,
        {}, {}, metric::CueScanRequest { { 9601, 24000 }, 5, 7 });
    require (requested.scan.state == metric::ScanState::complete && !requested.scan.wholeSource
        && requested.scan.sourceFrames == 24000 && requested.scan.frames.size() == 3
        && requested.scan.frames.front().end_sample == 14400, "Cue request resolves from actual host reader length");
    require (metric::scanPreparedSource (f.source, f.root, 48000, 2, false, 100, cancel,
        {}, partial, metric::CueScanRequest { {9601, 24000}, 5, 7 }).scan.state == metric::ScanState::invalidFormat,
        "explicit window and Cue request are mutually exclusive");
    require (metric::scanPreparedSource (f.source, f.root, 48000, 2, false, 100, cancel,
        {}, {}, metric::CueScanRequest { {0, 24001}, 5, 7 }).scan.state == metric::ScanState::invalidFormat,
        "Cue request cannot extend past actual host EOF");
    cancel.store (true);
    require (metric::scanPreparedSource (f.source, f.root, 48000, 2, false, 100, cancel).scan.state
             == metric::ScanState::cancelled, "cancelled job never opens reader");
    cancel.store (false);
    auto authority = revision::service().watch (*f.source);
    authority->unavailable.store (2);
    require (metric::scanPreparedSource (f.source, f.root, 48000, 2, false, 100, cancel).scan.state
             != metric::ScanState::complete, "invalid authority cannot publish series");
}
void verifyRuntimeCache()
{
    Fixture f;
    const auto copy = revision::snapshotCache().get (*f.source);
    require (copy->hasPrivateCopy(), "runtime fixture prepared");
    metric::Runtime runtime (f.root);
    runtime.service (f.source, 48000, 2, 10, {0, 24000});
    require (runtime.snapshot() && runtime.snapshot()->state == metric::Availability::measuring,
             "runtime reports measuring while dedicated job runs");
    runtime.waitForTest();
    runtime.service (f.source, 48000, 2, 10, {0, 24000});
    const auto full = runtime.snapshot();
    require (full && full->state == metric::Availability::ready && full->series
        && full->series->frames.size() == 5, "runtime publishes immutable complete series");
    runtime.service (f.source, 48000, 2, 11, {0, 9600});
    const auto cue = runtime.snapshot();
    require (cue && cue->state == metric::Availability::ready && cue->selectionGeneration == 11
        && cue->series == full->series, "cue and selection generation reuse same native series");
    Fixture other (48000, 2, 28800);
    const auto otherCopy = revision::snapshotCache().get (*other.source);
    runtime.service (other.source, 48000, 2, 20, {0, 24000}); runtime.waitForTest();
    runtime.service (other.source, 48000, 2, 20, {0, 24000});
    require (runtime.snapshot()->state == metric::Availability::ready && runtime.startsForTest() == 2,
             "A then B measured once each");
    runtime.service (f.source, 48000, 2, 21, {0, 9600});
    require (runtime.snapshot()->series == full->series && runtime.snapshot()->selectionGeneration == 21
        && runtime.startsForTest() == 2, "A-B-A reuses exact cached native series without a third scan");
    runtime.clear();
    require (!runtime.snapshot(), "clear withdraws published series immediately");
    runtime.service (f.source, 48000, 2, 11, {0, 9600});
    const auto reopened = runtime.snapshot();
    require (reopened && reopened->state == metric::Availability::ready
        && reopened->series == full->series, "same generation/Cue reopen republishes cached ready data");
    runtime.clear();
    runtime.service (f.source, 48000, 2, 12, {0, 24000});
    require (runtime.snapshot() && runtime.snapshot()->state == metric::Availability::ready
        && runtime.snapshot()->selectionGeneration == 12 && runtime.snapshot()->series == full->series,
        "new generation reopen is ready from verified cache");
}
void verifySeriesLru()
{
    const auto series = [] {
        auto result = std::make_shared<metric::SeriesData>();
        result->frames.resize (4); result->fullScanComplete = true;
        return result;
    };
    metric::SeriesCache cache;
    for (int i = 0; i < 8; ++i) require (cache.insert (juce::String (i), series()), "eight full series admitted");
    require (cache.size() == 8 && cache.bytes() <= metric::SeriesCache::maximumBytes, "song and byte bounds");
    require (cache.find ("0") != nullptr, "touch oldest makes it most recently used");
    require (cache.insert ("8", series()) && !cache.find ("1") && cache.find ("0") && cache.find ("8"),
             "ninth song evicts least recently used, not oldest inserted");
    metric::SeriesCache probe;
    require (probe.insert ("a", series()), "measure accounted series storage");
    const auto one = probe.bytes();
    metric::SeriesCache small (one * 2);
    require (small.insert ("a", series()) && small.insert ("b", series()) && small.size() == 2, "exact byte limit fits");
    require (small.find ("a") && small.insert ("c", series()) && !small.find ("b") && small.find ("a")
        && small.size() == 2 && small.bytes() == one * 2, "byte bound evicts LRU before eight songs");
    auto oversized = series(); oversized->frames.resize (100);
    require (!small.insert ("large", oversized) && small.size() == 2 && small.bytes() == one * 2,
             "oversized series rejected without evicting or truncating cached facts");
    auto prefix = series(); prefix->fullScanComplete = false;
    require (!small.insert ("prefix", prefix), "partial Cue is never a full-series cache entry");
    require (small.insert ("a", series()) && small.size() == 2 && small.bytes() == one * 2,
             "replacement does not double-count storage");
}
void verifyDirectFallbackAndShortFile()
{
    std::atomic<bool> cancel {false};
    {
        Fixture f;
        revision::snapshotFreeBytesForTest.store (0);
        const auto copy = revision::snapshotCache().get (*f.source);
        revision::snapshotFreeBytesForTest.store (-1);
        require (copy->available() && !copy->hasPrivateCopy(), "force existing verified direct fallback");
        const auto result = metric::scanPreparedSource (f.source, f.root, 48000, 2, false, 100, cancel);
        require (result.scan.state == metric::ScanState::readFailed
            && result.reason == "reference_metric_private_copy_required", "metrics refuses direct/NAS fallback");
    }
    {
        Fixture f (48000, 1, 1000);
        const auto copy = revision::snapshotCache().get (*f.source);
        require (copy->hasPrivateCopy(), "short private fixture");
        const auto result = metric::scanPreparedSource (f.source, f.root, 48000, 1, false, 100, cancel);
        require (result.scan.state == metric::ScanState::complete && result.scan.frames.empty(),
                 "sub-100ms source has no observations, not padded zero");
    }
}
void verifyFailureReasons()
{
    Fixture f;
    const auto copy = revision::snapshotCache().get (*f.source);
    metric::Runtime runtime (f.root);
    runtime.service (f.source, 0, 2, 1, {0, 24000});
    require (runtime.snapshot()->reason == "unsupported_format" && runtime.snapshot()->state == metric::Availability::unavailable,
             "format rejection before Job retains a fixed public reason");
    runtime.service ({}, 48000, 2, 2, {0, 24000});
    require (runtime.snapshot()->reason == "read_failed", "missing source retains read failure reason");
    runtime.service (f.source, 48000, 2, 3, {0, 24000}); runtime.waitForTest();
    runtime.service (f.source, 48000, 2, 3, {0, 24000});
    require (runtime.snapshot()->state == metric::Availability::ready && runtime.snapshot()->reason.isEmpty(),
             "successful generation clears previous error reason");
    runtime.service (f.source, 48000, 2, 4, {1, 10});
    const auto tinyCue = runtime.snapshot();
    require (tinyCue->state == metric::Availability::ready && tinyCue->reason.isEmpty(),
             "a scanned source is distinct from metric-specific missing Cue observations");
    for (const auto& reason : tinyCue->cueReasons) require (reason == "cue_too_short", "no endpoint in Cue has a precise short-Cue reason");
    runtime.service (f.source, 48000, 2, 5, {0, 4800});
    const auto first = runtime.snapshot();
    for (std::size_t i = 0; i < 3; ++i)
        require (first->cueReasons[i] == (first->cue[i].observations == 0 ? "no_valid_value" : ""),
                 "existing window with no finite metric is not mislabeled short audio");
    Fixture shortAudio (8000, 2, 400);
    const auto shortCopy = revision::snapshotCache().get (*shortAudio.source);
    metric::Runtime shortRuntime (shortAudio.root);
    shortRuntime.service (shortAudio.source, 8000, 2, 6, {0, 400}); shortRuntime.waitForTest();
    shortRuntime.service (shortAudio.source, 8000, 2, 6, {0, 400});
    require (shortRuntime.snapshot()->reason == "audio_too_short", "under-100ms source receives short-audio reason");
    Fixture direct;
    revision::snapshotFreeBytesForTest.store (0);
    const auto directCopy = revision::snapshotCache().get (*direct.source);
    revision::snapshotFreeBytesForTest.store (-1);
    require (!directCopy->hasPrivateCopy(), "reason fixture uses direct fallback");
    metric::Runtime directRuntime (direct.root);
    directRuntime.service (direct.source, 48000, 2, 7, {0, 24000}); directRuntime.waitForTest();
    directRuntime.service (direct.source, 48000, 2, 7, {0, 24000});
    require (directRuntime.snapshot()->reason == "private_copy_required", "worker SourceScan reason survives Job transport");
    const auto failedStarts = directRuntime.startsForTest();
    require (failedStarts == 1, "terminal failure fixture ran exactly one worker");
    const auto firstFailure = directRuntime.snapshot();
    directRuntime.service (direct.source, 48000, 2, 7, {4800, 9600});
    require (directRuntime.snapshot() != firstFailure && directRuntime.snapshot()->reason == "private_copy_required",
             "same-generation changed Cue republishes terminal reason");
    directRuntime.service (direct.source, 48000, 2, 8, {4800, 14400});
    require (directRuntime.snapshot()->selectionGeneration == 8
        && directRuntime.snapshot()->state == metric::Availability::unavailable
        && directRuntime.snapshot()->reason == "private_copy_required" && !directRuntime.snapshot()->series,
        "new Cue and generation retain the terminal source reason without stale data");
    directRuntime.service (direct.source, 48000, 2, 9, {4800, 14400});
    require (directRuntime.snapshot()->selectionGeneration == 9 && directRuntime.startsForTest() == failedStarts,
             "generation-only change republishes reason without admitting another worker");
    directRuntime.service (f.source, 48000, 2, 10, {0, 24000}); directRuntime.waitForTest();
    directRuntime.service (f.source, 48000, 2, 10, {0, 24000});
    require (directRuntime.snapshot()->state == metric::Availability::ready && directRuntime.snapshot()->reason.isEmpty(),
             "different source does not inherit terminal failure");
    directRuntime.clear();
    directRuntime.service (direct.source, 48000, 2, 11, {0, 24000});
    require (directRuntime.snapshot()->state == metric::Availability::measuring && directRuntime.snapshot()->reason.isEmpty(),
             "explicit clear permits a fresh attempt without reusing old failure");
    directRuntime.waitForTest(); directRuntime.service (direct.source, 48000, 2, 11, {0, 24000});
    require (directRuntime.startsForTest() == failedStarts + 2 && directRuntime.snapshot()->reason == "private_copy_required",
             "clear starts exactly one fresh worker and accepts only its new failure");
    require (metric::Runtime::failureReasonForTest (metric::ScanState::capacityExceeded, {}) == "capacity_exceeded"
        && metric::Runtime::failureReasonForTest (metric::ScanState::waiting, "reference_source_revalidating") == "preparing"
        && metric::Runtime::failureReasonForTest (metric::ScanState::readFailed, "reference_source_changed") == "source_changed"
        && metric::Runtime::failureReasonForTest (metric::ScanState::analysisFailed, "/private/raw/error") == "analysis_failed",
        "fixed whitelist maps capacity/preparation/source errors without leaking raw text");
}
void verifyEarlyCuePublication()
{
    Fixture f (8000, 2, 16000);
    const auto copy = revision::snapshotCache().get (*f.source);
    require (copy->hasPrivateCopy(), "early Cue private snapshot");
    metric::Runtime runtime (f.root);
    struct Gate
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false, released = false, once = false;
        void arrive()
        {
            std::unique_lock<std::mutex> lock (mutex);
            if (once) return;
            once = true; entered = true; changed.notify_all();
            changed.wait (lock, [this] { return released; });
        }
        bool awaitEntry()
        {
            std::unique_lock<std::mutex> lock (mutex);
            return changed.wait_for (lock, std::chrono::seconds (5), [this] { return entered; });
        }
        void open()
        {
            { const std::lock_guard<std::mutex> lock (mutex); released = true; }
            changed.notify_all();
        }
        void reset()
        {
            const std::lock_guard<std::mutex> lock (mutex);
            entered = false; released = false; once = false;
        }
    };
    const auto gate = std::make_shared<Gate>();
    struct Unblock { std::shared_ptr<Gate> gate; ~Unblock() { gate->open(); } } unblock {gate};
    runtime.afterEarlyForTest = [gate] { gate->arrive(); };
    const metric::Cue cue {0, 4000};
    runtime.service (f.source, 8000, 2, 1, cue);
    require (gate->awaitEntry(), "native output reaches requested Cue before full scan");
    runtime.service (f.source, 8000, 2, 1, cue);
    const auto early = runtime.snapshot();
    require (early && early->state == metric::Availability::ready && early->series && !early->series->fullScanComplete
        && early->series->frames.back().end_sample >= cue.end && early->series->frames.back().end_sample < 16000,
        "immutable prefix is ready only for its fully covered Cue, not the full source");
    runtime.service (f.source, 8000, 2, 2, {0, 16000});
    require (runtime.snapshot()->state == metric::Availability::measuring && !runtime.snapshot()->series,
             "Cue outside current prefix immediately withdraws ready facts");
    runtime.service (f.source, 8000, 2, 3, {800, 3200});
    require (runtime.snapshot()->state == metric::Availability::ready && runtime.snapshot()->selectionGeneration == 3,
             "Cue inside the same source prefix reaggregates under current selection generation");
    gate->open(); runtime.waitForTest(); runtime.service (f.source, 8000, 2, 3, {800, 3200});
    const auto full = runtime.snapshot();
    require (full->series && full->series->fullScanComplete && full->series->frames.size() == 20,
             "only real EOF completion enters full cache");
    const auto same = [] (double a, double b) { return a == b || (std::isnan (a) && std::isnan (b)); };
    for (std::size_t i = 0; i < early->series->frames.size(); ++i) {
        const auto& a = early->series->frames[i]; const auto& b = full->series->frames[i];
        require (a.end_sample == b.end_sample && same (a.psr_db, b.psr_db) && same (a.crest_db, b.crest_db)
            && same (a.sharpness_acum, b.sharpness_acum), "early facts equal exact uninterrupted native history");
    }
    metric::Runtime cancelled (f.root);
    gate->reset();
    cancelled.afterEarlyForTest = runtime.afterEarlyForTest;
    auto lease = std::make_shared<int> (1);
    const std::weak_ptr<void> weakLease = lease;
    cancelled.setPublisherLease (lease); lease.reset();
    cancelled.service (f.source, 8000, 2, 9, cue);
    require (gate->awaitEntry(), "second independent job reaches early publication");
    cancelled.setPublisherLease ({});
    require (!weakLease.expired(), "cancelled detached selected scan retains publisher lease");
    require (!cancelled.snapshot(), "clear immediately removes old prefix and reasons");
    cancelled.service (f.source, 0, 2, 10, cue);
    gate->open(); cancelled.waitForTest();
    require (weakLease.expired(), "selected scan publisher lease releases after Work teardown");
    require (cancelled.snapshot()->selectionGeneration == 10 && cancelled.snapshot()->reason == "unsupported_format"
        && !cancelled.snapshot()->series, "cancelled old job cannot replace the new generation or its reason");
}
int benchmarkLateCue()
{
    constexpr std::uint32_t rate = 48000;
    constexpr std::uint64_t total = rate * 180;
    constexpr double tolerance = 1e-4;
    const metric::Cue cue { rate * 120, rate * 140 };
    Fixture f (rate, 2, static_cast<int> (total));
    const auto copy = revision::snapshotCache().get (*f.source);
    require (copy->hasPrivateCopy(), "late Cue benchmark requires private snapshot");
    std::atomic<bool> cancelled {false};
    const auto measured = [&] (unsigned warmup, const std::optional<metric::CueScanRequest>& request) {
        const auto wallStart = std::chrono::steady_clock::now();
        const auto cpuStart = std::clock();
        const auto result = metric::scanPreparedSource (f.source, f.root, rate, 2, false, 36000,
                                                       cancelled, {}, {}, request);
        const auto cpuEnd = std::clock();
        const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - wallStart).count();
        require (cpuStart != static_cast<std::clock_t> (-1) && cpuEnd != static_cast<std::clock_t> (-1)
                 && cpuEnd >= cpuStart && wall > 0, "process CPU clock unavailable");
        const double cpu = static_cast<double> (cpuEnd - cpuStart) / CLOCKS_PER_SEC;
        std::cout << "LATE_CUE_COST warmup_seconds=" << warmup << " wall_seconds=" << wall
                  << " process_cpu_seconds=" << cpu << " one_core_percent=" << (100 * cpu / wall)
                  << " source_frames=" << result.scan.sourceFrames << " scanned_from=" << result.scan.scannedFrom
                  << " scanned_through=" << result.scan.scannedThrough << " retained_frames=" << result.scan.frames.size()
                  << " whole_source=" << result.scan.wholeSource << std::endl;
        require (result.scan.state == metric::ScanState::complete && result.scan.sourceFrames == total,
                 "late Cue benchmark scan incomplete");
        return result.scan;
    };
    // Exactly one uninterrupted full scan; both candidates compare against these raw observations.
    const auto full = measured (0, {});
    require (full.wholeSource && full.frames.size() == 1800, "full baseline covers real EOF");
    std::vector<KirinReferenceMetricFrame> expected;
    for (const auto& frame : full.frames)
        if (frame.end_sample > cue.start && frame.end_sample <= cue.end) expected.push_back (frame);
    require (expected.size() == 200, "baseline Cue has every absolute 100ms endpoint");
    bool accepted[2][3] {};
    const char* names[] { "PSR", "CREST", "SHARP" };
    const auto value = [] (const KirinReferenceMetricFrame& f, int metricIndex) {
        return metricIndex == 0 ? f.psr_db : metricIndex == 1 ? f.crest_db : f.sharpness_acum;
    };
    bool allMatch = true;
    for (int candidate = 0; candidate < 2; ++candidate) {
        const unsigned warmup = candidate == 0 ? 5 : 10;
        // Test-only bypass: expose raw values for strict comparison, not production approval.
        const auto window = metric::cueScanWindow (rate, total, cue, warmup, 7);
        require (window.has_value(), "valid late Cue window");
        const auto actual = measured (warmup, metric::CueScanRequest { cue, warmup, 7 });
        require (!actual.wholeSource && actual.completeMetricMask == 7
            && actual.completeCue.start == cue.start && actual.completeCue.end == cue.end
            && actual.scannedFrom == window->start && actual.scannedThrough == window->stop,
            "late Cue scan preserves precise partial coverage");
        for (int m = 0; m < 3; ++m) {
            bool endpoints = actual.frames.size() == expected.size();
            std::size_t validityMismatches = 0, finitePairs = 0;
            double maximumDifference = 0;
            for (std::size_t i = 0; i < std::min (actual.frames.size(), expected.size()); ++i) {
                endpoints = endpoints && actual.frames[i].end_sample == expected[i].end_sample;
                const double a = value (actual.frames[i], m), b = value (expected[i], m);
                if (std::isfinite (a) != std::isfinite (b) || std::isinf (a) || std::isinf (b)) ++validityMismatches;
                if (std::isfinite (a) && std::isfinite (b)) {
                    ++finitePairs; maximumDifference = std::max (maximumDifference, std::abs (a - b));
                }
            }
            accepted[candidate][m] = endpoints && validityMismatches == 0 && finitePairs > 0
                                      && maximumDifference <= tolerance;
            allMatch = allMatch && accepted[candidate][m];
            std::cout << "LATE_CUE_COMPARE warmup_seconds=" << warmup << " metric=" << names[m]
                      << " endpoints_match=" << endpoints << " validity_mismatches=" << validityMismatches
                      << " finite_pairs=" << finitePairs << " max_abs=" << maximumDifference
                      << " tolerance=" << tolerance << " result=" << (accepted[candidate][m] ? "PASS" : "FAIL") << std::endl;
        }
    }
    for (int m = 0; m < 3; ++m)
        std::cout << "LATE_CUE_FALLBACK metric=" << names[m] << " selected_origin="
                  << (accepted[0][m] ? "cue_minus_5s" : accepted[1][m] ? "cue_minus_10s" : "zero")
                  << " fixture_only=true" << std::endl;
    std::cout << "LATE_CUE_RESULT " << (allMatch ? "PASS" : "FAIL: mismatched candidates require fallback") << std::endl;
    return allMatch ? 0 : 1;
}
#include "ReferenceMetricSharedCacheTest.h"
#include "ReferenceMetricContentCacheTest.h"
#include "ReferenceMetricFocusedRuntimeTest.h"
}
int main (int argc, char** argv)
{
    if (argc == 2 && std::string (argv[1]) == "--benchmark-late-cue") {
        try { return benchmarkLateCue(); }
        catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
    if (argc == 2 && (std::string (argv[1]) == "--benchmark" || std::string (argv[1]) == "--benchmark-cue"))
    {
        try {
            Fixture f (48000, 2, 48000 * 180);
            const auto copy = revision::snapshotCache().get (*f.source);
            require (copy->hasPrivateCopy(), "benchmark private snapshot");
            std::atomic<bool> cancelled {false};
            const auto start = std::chrono::steady_clock::now();
            const auto cpuStart = std::clock();
            double cueSeconds = -1;
            std::vector<KirinReferenceMetricFrame> cuePrefix;
            const auto result = metric::scanPreparedSource (f.source, f.root, 48000, 2, false, 36000, cancelled,
                [&] (const auto& frames, std::uint64_t total) {
                    if (cueSeconds < 0 && !frames.empty() && frames.back().end_sample >= 48000 * 20) {
                        require (total == 48000 * 180, "early benchmark retains true source length");
                        cueSeconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
                        cuePrefix = frames;
                        std::cout << "20s Cue ready: wall=" << cueSeconds << " full_complete=false" << std::endl;
                    }
                    return true;
                });
            const auto seconds = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
            const auto cpu = static_cast<double> (std::clock() - cpuStart) / CLOCKS_PER_SEC;
            std::cout << "180s 48k stereo: scan_wall=" << seconds << " cpu_seconds=" << cpu
                      << " one_core_percent=" << (100.0 * cpu / seconds) << '\n';
            require (result.scan.state == metric::ScanState::complete && result.scan.frames.size() == 1800,
                     "benchmark complete full PCM");
            if (std::string (argv[1]) == "--benchmark-cue") {
                require (cueSeconds >= 0 && cueSeconds < seconds && cueSeconds <= 30.0 && cpu / seconds <= 0.5,
                         "Cue priority benchmark requires <=30s early Cue and <=50% average one-core CPU");
                const auto same = [] (double a, double b) { return a == b || (std::isnan (a) && std::isnan (b)); };
                for (std::size_t i = 0; i < cuePrefix.size(); ++i) {
                    const auto& a = cuePrefix[i]; const auto& b = result.scan.frames[i];
                    require (a.end_sample == b.end_sample && same (a.psr_db, b.psr_db) && same (a.crest_db, b.crest_db)
                        && same (a.sharpness_acum, b.sharpness_acum), "benchmark prefix and full native history match");
                }
            } else require (seconds <= 30.0 && cpu / seconds <= 0.5, "Cue priority required: scan exceeds 30s or 50% one core");
            return 0;
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }

    try { verifyContentCacheFormatSeparation(); verifyAuthenticatedContentCache(); verifyFocusedRuntimeTransition(); verifySharedRoleCache(); verifyMetricCueCoverage(); verifySeriesLru(); verifyPreparedScan(); verifyDirectFallbackAndShortFile(); verifyRuntimeCache(); verifyFailureReasons(); verifyEarlyCuePublication(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "metric private source scan PASS\n";
}
