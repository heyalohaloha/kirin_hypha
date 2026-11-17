#include "TestPathBudget.h"
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
    cancelled.service (f.source, 8000, 2, 9, cue);
    require (gate->awaitEntry(), "second independent job reaches early publication");
    cancelled.clear();
    require (!cancelled.snapshot(), "clear immediately removes old prefix and reasons");
    cancelled.service (f.source, 0, 2, 10, cue);
    gate->open(); cancelled.waitForTest();
    require (cancelled.snapshot()->selectionGeneration == 10 && cancelled.snapshot()->reason == "unsupported_format"
        && !cancelled.snapshot()->series, "cancelled old job cannot replace the new generation or its reason");
}
}
int main (int argc, char** argv)
{
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

    try { verifyPreparedScan(); verifyDirectFallbackAndShortFile(); verifyRuntimeCache(); verifyFailureReasons(); verifyEarlyCuePublication(); }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    std::cout << "metric private source scan PASS\n";
}
