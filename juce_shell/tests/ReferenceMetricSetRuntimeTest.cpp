#include "reference_audition/ReferenceMetricSetRuntime.h"
#include "reference_audition/ReferenceTestCommitObserver.h"
#include <iostream>
#include <stdexcept>
#include <condition_variable>
#include <mutex>
namespace hypha::reference_audition
{
void stageReferenceTestWorker (const void*, const char*) noexcept {}
void notifyReferenceTestCommit() noexcept {}
}
namespace ref = hypha::reference_audition;
namespace metric = ref::metrics;
namespace
{
void require (bool passed, const char* message) { if (!passed) throw std::runtime_error (message); }
metric::SetRequest request()
{
    metric::SetRequest r;
    r.membershipKey = "set-revision-1"; r.generation = 1; r.rate = 48000; r.channels = 2;
    for (int i = 0; i < 3; ++i) {
        ref::RuntimeCandidate m; m.candidateId = juce::String (i); m.sourceArtifact.sha256 = "sha-" + juce::String (i);
        m.cues.push_back ({"cue", "", 48000, 0, 48000, false}); r.members.push_back (m);
    }
    return r;
}
metric::SetRuntime::TestResult measured (const ref::RuntimeCandidate& m)
{
    metric::SetRuntime::TestResult result; result.complete = true;
    const auto value = static_cast<double> (m.candidateId.getIntValue() * 10);
    for (auto& s : result.summaries) s = {value, 10, true};
    return result;
}
void drain (metric::SetRuntime& runtime, const metric::SetRequest& r)
{
    for (int i = 0; i < 5; ++i) { runtime.service (r); runtime.waitForTest(); }
    runtime.service (r, false);
}
void workspaceRequest()
{
    ref::RuntimeWorkspace workspace; workspace.publicationHash = "workspace";
    workspace.librarySets.emplace(); workspace.librarySets->hash = "set-hash";
    ref::RuntimeSongSet set; set.songSetId = "chosen"; set.revisionId = "revision";
    set.songs = request().members; set.songs[1].prepared = false;
    workspace.librarySets->songSets.push_back (set);
    const auto selected = metric::metricSetRequest (&workspace, "chosen", 7, 48000, 2);
    require (selected.generation == 7 && selected.members.size() == 3 && !selected.members[1].prepared,
             "workspace request retains all members, including unprepared");
    const auto key = selected.membershipKey;
    workspace.librarySets->hash = "new-hash";
    require (metric::metricSetKey (&workspace, "chosen") != key, "workspace set revision invalidates publication key");
    require (metric::metricSetRequest (&workspace, "missing", 7, 48000, 2).members.empty(),
             "missing chosen set never silently falls back to another set");
}
void completeAndMissing()
{
    metric::SetRuntime runtime (juce::File::getSpecialLocation (juce::File::tempDirectory));
    const auto r = request();
    std::atomic<int> called {0};
    runtime.testWork = [&] (const auto& member, const auto&) { ++called; return measured (member); };
    runtime.service (r, false);
    require (!runtime.activeForTest() && called == 0, "selected song busy prevents background admission");
    runtime.service (r); runtime.waitForTest(); runtime.service (r, false);
    require (runtime.snapshot()->expected == 3 && runtime.snapshot()->completed == 1 && !runtime.snapshot()->complete,
             "partial set retains full denominator");
    require (!std::isfinite (runtime.snapshot()->bands[0].low), "partial set cannot narrow band");
    drain (runtime, r);
    const auto p = runtime.snapshot();
    require (p->complete && p->completed == 3 && called == 3, "one job per member completes set");
    require (p->bands[0].low == 0 && p->bands[0].high == 20, "equal song weights nearest-rank p10 p90");
    auto next = r; next.generation = 2;
    runtime.testWork = [&] (const auto& member, const auto&) {
        auto result = measured (member); if (member.candidateId == "1") { result.complete = false; result.reason = "missing"; } return result;
    };
    drain (runtime, next);
    require (!runtime.snapshot()->complete && runtime.snapshot()->completed == 2
        && runtime.snapshot()->reason == "missing" && !std::isfinite (runtime.snapshot()->bands[2].low),
        "failed member is retained, never omitted");
    next.generation = 3;
    runtime.testWork = [] (const auto& member, const auto&) {
        auto result = measured (member); result.summaries[2].median = metric::missing; return result;
    };
    drain (runtime, next);
    require (runtime.snapshot()->complete && !std::isfinite (runtime.snapshot()->bands[2].low),
             "legitimate SHARP missing never becomes zero or a band");
}
void boundedPreparationRetry()
{
    require (metric::metricPreparationWait (metric::ScanState::waiting, 0), "zero-PCM preparation wait is retryable");
    require (!metric::metricPreparationWait (metric::ScanState::waiting, 48000)
        && !metric::metricPreparationWait (metric::ScanState::readFailed, 0)
        && !metric::metricPreparationWait (metric::ScanState::cancelled, 0),
        "partial reads, read failures and cancellation are never retryable");
    metric::SetRuntime runtime (juce::File::getSpecialLocation (juce::File::tempDirectory));
    auto r = request(); r.members.resize (1);
    double now = 0; std::atomic<int> calls {0};
    runtime.testNowSeconds = [&] { return now; };
    runtime.testWork = [&] (const auto& member, const auto&) {
        if (++calls > 1) return measured (member);
        metric::SetRuntime::TestResult wait; wait.preparationWait = true;
        wait.reason = "reference_source_revalidating"; return wait;
    };
    runtime.service (r); runtime.waitForTest(); runtime.service (r, false);
    require (!runtime.snapshot()->complete && calls == 1, "preparation wait remains incomplete");
    now = 0.999; runtime.service (r);
    require (!runtime.activeForTest() && calls == 1, "backoff prevents rapid rescan");
    now = 1.0; runtime.service (r); runtime.waitForTest(); runtime.service (r, false);
    require (runtime.snapshot()->complete && calls == 2 && runtime.snapshot()->reason.isEmpty(),
             "same request recovers after bounded preparation retry");
    ++r.generation; calls = 0;
    runtime.testWork = [&] (const auto&, const auto&) {
        ++calls; metric::SetRuntime::TestResult wait; wait.preparationWait = true; wait.reason = "preparing"; return wait;
    };
    for (int attempt = 0; attempt < 6; ++attempt) {
        runtime.service (r); runtime.waitForTest(); runtime.service (r, false); now += 1.0;
    }
    now += 100; runtime.service (r);
    require (calls == 6 && !runtime.activeForTest() && !runtime.snapshot()->complete
        && runtime.snapshot()->reason == "reference_metric_preparation_retry_exhausted",
        "five retries cap persistent preparation waits without a false band");
    ++r.generation; calls = 0;
    runtime.testWork = [&] (const auto&, const auto&) {
        ++calls; metric::SetRuntime::TestResult failure; failure.reason = "mid_scan_wait"; return failure;
    };
    runtime.service (r); runtime.waitForTest(); runtime.service (r, false);
    now += 100; runtime.service (r);
    require (calls == 1 && !runtime.activeForTest(), "a mid-scan failure cannot be repeatedly rescanned");
    ++r.generation; calls = 0;
    runtime.testWork = [&] (const auto&, const auto&) {
        ++calls; metric::SetRuntime::TestResult wait; wait.preparationWait = true; return wait;
    };
    runtime.service (r); runtime.waitForTest(); runtime.service (r, false);
    ++r.generation;
    runtime.testWork = [&] (const auto& member, const auto&) { ++calls; return measured (member); };
    runtime.service (r); runtime.waitForTest(); runtime.service (r, false);
    require (calls == 2 && runtime.snapshot()->complete && runtime.snapshot()->generation == r.generation,
             "new generation drops the old preparation retry deadline and result");
}
void cancellationAndIdentity()
{
    metric::SetRuntime runtime (juce::File::getSpecialLocation (juce::File::tempDirectory));
    auto r = request();
    struct Gate
    {
        std::mutex mutex;
        std::condition_variable changed;
        bool entered = false, released = false;
        void open()
        {
            { const std::lock_guard<std::mutex> lock (mutex); released = true; }
            changed.notify_all();
        }
    };
    const auto gate = std::make_shared<Gate>();
    struct Unblock { std::shared_ptr<Gate> gate; ~Unblock() { gate->open(); } } unblock {gate};
    const auto sawCancellation = std::make_shared<std::atomic<bool>> (false);
    runtime.testWork = [gate, sawCancellation] (const auto& member, const std::atomic<bool>& cancel) {
        std::unique_lock<std::mutex> lock (gate->mutex);
        gate->entered = true; gate->changed.notify_all();
        gate->changed.wait (lock, [&] { return gate->released; });
        sawCancellation->store (cancel.load());
        return measured (member);
    };
    runtime.service (r);
    const auto completed = runtime.completionForTest();
    {
        std::unique_lock<std::mutex> lock (gate->mutex);
        require (gate->changed.wait_for (lock, std::chrono::seconds (5), [&] { return gate->entered; }),
                 "cancellable job entered");
    }
    runtime.clear(); gate->open(); completed();
    require (sawCancellation->load() && !runtime.snapshot(), "clear cancels without publishing old results");
    runtime.testWork = [] (const auto& member, const auto&) { return measured (member); };
    r.generation = 8;
    runtime.service (r);
    require (runtime.activeForTest(), "completion notification proves admission is free for the next generation");
    drain (runtime, r);
    require (runtime.snapshot()->generation == 8 && runtime.snapshot()->complete, "new generation cannot accept old completion");
    r.members[1].cues[0].endSample = 24000;
    runtime.service (r, false);
    require (runtime.snapshot()->completed == 0, "Cue change invalidates collection even without external generation change");
    r.members.resize (65); runtime.service (r);
    require (!runtime.activeForTest() && !runtime.snapshot()->complete && runtime.snapshot()->reason == "reference_metric_set_invalid",
             "oversized set rejected without silently truncating membership");
}
}
int main()
{
    try {
        require (metric::metricBackgroundCpuBudget (200, 0, 70), "long background scan can finish at bounded CPU");
        require (metric::metricBackgroundCpuBudget (10, 0, 5), "half-core boundary accepted");
        require (!metric::metricBackgroundCpuBudget (10, 0, 5.01), "CPU excess remains rejected");
        require (!metric::metricBackgroundCpuBudget (0, 0, 0)
            && !metric::metricBackgroundCpuBudget (10, -1, 1)
            && !metric::metricBackgroundCpuBudget (10, 2, 1)
            && !metric::metricBackgroundCpuBudget (std::numeric_limits<double>::infinity(), 0, 1), "invalid timings rejected");
        workspaceRequest(); completeAndMissing(); boundedPreparationRetry(); cancellationAndIdentity(); std::cout << "Reference metric set runtime PASS\n"; return 0; }
    catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
