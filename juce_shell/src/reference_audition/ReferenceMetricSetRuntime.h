#pragma once
#include "ReferenceMetricJob.h"
#include "ReferenceMetricSourceScan.h"
#include <array>
#include <chrono>
#include <ctime>

namespace hypha::reference_audition::metrics
{
#if JUCE_WINDOWS
namespace windows_detail { double threadCpuSeconds(); }
#endif

inline bool metricPreparationWait (ScanState state, std::uint64_t sourceFrames) noexcept
{ return state == ScanState::waiting && sourceFrames == 0; }
inline bool metricBackgroundCpuBudget (double wall, double start, double end) noexcept
{ return std::isfinite (wall) && std::isfinite (start) && std::isfinite (end)
    && wall > 0 && start >= 0 && end >= start && (end - start) / wall <= 0.5; }
struct SetRequest
{
    juce::String membershipKey;
    std::uint64_t generation = 0;
    std::uint32_t rate = 0, channels = 0;
    std::vector<RuntimeCandidate> members; // Full denominator, including unprepared members.
};
struct SetPublication
{
    juce::String membershipKey, reason;
    std::uint64_t generation = 0;
    std::uint32_t rate = 0, channels = 0;
    std::size_t expected = 0, completed = 0;
    bool complete = false;
    std::array<Band, 3> bands;
};
inline const RuntimeSongSet* metricSongSet (const RuntimeWorkspace* workspace, const juce::String& id)
{
    if (!workspace || !workspace->librarySets || id.isEmpty()) return nullptr;
    for (const auto& set : workspace->librarySets->songSets) if (set.songSetId == id) return &set;
    return nullptr;
}
inline juce::String metricSetKey (const RuntimeWorkspace* workspace, const juce::String& id)
{
    const auto* set = metricSongSet (workspace, id);
    return set ? workspace->publicationHash + "\n" + workspace->librarySets->hash + "\n"
        + juce::String (workspace->librarySets->revision) + "\n" + id + "\n" + set->revisionId : juce::String {};
}
inline SetRequest metricSetRequest (const RuntimeWorkspace* workspace, const juce::String& id,
    std::uint64_t generation, std::uint32_t rate, std::uint32_t channels)
{
    SetRequest request;
    request.membershipKey = metricSetKey (workspace, id); request.generation = generation;
    request.rate = rate; request.channels = channels;
    if (const auto* set = metricSongSet (workspace, id)) request.members = set->songs;
    return request;
}
// Control-worker owned. service(false) drains but admits no new work, so the caller
// can give the selected song priority. Job still enforces global single admission.
class SetRuntime final
{
    struct Result
    {
        std::array<Summary, 3> summaries;
        std::shared_ptr<const RuntimeSource> authority; // Description only, no private PCM owner.
        juce::String key, reason;
        bool complete = false;
        bool preparationWait = false; // Only an explicit wait before any PCM scan.
    };
    struct CacheEntry { juce::String key; std::array<Summary, 3> summaries; };
public:
    explicit SetRuntime (juce::File directory) : root (std::move (directory)), repository (root) {}
    std::shared_ptr<const SetPublication> snapshot() const { return std::atomic_load (&published); }
    // Control-worker only. Ownership changes cancel and invalidate old publications;
    // detached Work retains the previous lease until all reader teardown finishes.
    void setPublisherLease (std::shared_ptr<void> lease)
    {
        if (publisherLease == lease) return;
        clear(); publisherLease = std::move (lease);
    }
    void clear()
    {
        job.cancel(); ++serial; identity.clear(); results.clear(); done.clear(); retries.clear(); retryAt.clear(); pending.reset(); active.reset();
        std::atomic_store (&published, std::shared_ptr<const SetPublication> {});
    }
    void service (const SetRequest& request, bool admit = true)
    {
        const auto id = requestIdentity (request);
        if (id != identity)
        {
            clear(); identity = id; current = request;
            if (valid (request)) { results.resize (request.members.size()); done.assign (request.members.size(), false);
                retries.assign (request.members.size(), 0); retryAt.assign (request.members.size(), 0.0); }
        }
        if (!valid (current)) { publish ("reference_metric_set_invalid"); return; }
        if (auto finished = job.take (serial))
        {
            if (active && pending && *active < results.size())
            {
                if (finished->scan.state == ScanState::complete && pending->complete)
                {
                    results[*active] = *pending;
                    if (pending->authority && !authorityValid (*pending)) results[*active].complete = false;
                    if (results[*active].complete && pending->key.isNotEmpty())
                    {
                        cache.push_back ({pending->key, pending->summaries});
                        if (cache.size() > maxMembers) cache.erase (cache.begin());
                    }
                }
                else
                {
                    results[*active] = {};
                    results[*active].reason = pending->reason.isNotEmpty() ? pending->reason : "reference_metric_set_scan_failed";
                }
                const bool retry = !results[*active].complete && pending->preparationWait && retries[*active] < maxPreparationRetries;
                done[*active] = !retry;
                if (retry) { ++retries[*active]; retryAt[*active] = nowSeconds() + preparationRetrySeconds; }
                else if (pending->preparationWait && !results[*active].complete)
                    results[*active].reason = "reference_metric_preparation_retry_exhausted";
            }
            active.reset(); pending.reset();
        }
        // Recheck descriptions at acceptance and on every publication; cached PCM
        // facts do not grant ongoing authority over a changed/deleted source.
        for (auto& result : results)
            if (result.complete && result.authority && !authorityValid (result))
            { result.complete = false; result.reason = "reference_source_changed"; }
        publish ({});
        if (active || !admit) return;
        std::size_t next = 0;
        const auto now = nowSeconds();
        while (next < done.size() && (done[next] || now < retryAt[next])) ++next;
        if (next == done.size()) return;
        const auto candidate = current.members[next];
        auto box = std::make_shared<Result>();
        const auto rootCopy = root; const auto rate = current.rate, channels = current.channels;
        const auto savedCache = cache;
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
        const auto hook = testWork;
#endif
        const auto start = job.start (serial, [candidate, box, rootCopy, rate, channels, savedCache, lease = publisherLease
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
            , hook
#endif
            ] (const std::atomic<bool>& cancel) {
                juce::ignoreUnused (lease); // Keep the chain claim through detached reader teardown.
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
                if (hook) { *box = hook (candidate, cancel); return ScanResult {box->complete ? ScanState::complete : ScanState::analysisFailed, {}, 0}; }
#endif
                *box = measure (candidate, rootCopy, rate, channels, savedCache, cancel);
                return ScanResult {box->complete ? ScanState::complete : ScanState::analysisFailed, {}, 0};
            });
        if (start == Job::Start::started) { active = next; pending = std::move (box); }
        else if (start == Job::Start::unavailable)
        { done[next] = true; results[next].reason = "reference_metric_worker_unavailable"; publish ({}); }
    }
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
    using TestResult = Result;
    std::function<Result (const RuntimeCandidate&, const std::atomic<bool>&)> testWork;
    std::function<double()> testNowSeconds;
    void waitForTest() { if (active) job.waitForTest(); }
    std::function<void()> completionForTest() { return job.completionForTest(); }
    bool activeForTest() const { return active.has_value(); }
#endif
private:
    static constexpr std::size_t maxMembers = 64, maxObservations = 36000;
    static constexpr unsigned maxPreparationRetries = 5; // At most six total attempts per request/member.
    static constexpr double preparationRetrySeconds = 1.0;
    double nowSeconds() const
    {
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
        if (testNowSeconds) return testNowSeconds();
#endif
        return std::chrono::duration<double> (std::chrono::steady_clock::now().time_since_epoch()).count();
    }
    static bool valid (const SetRequest& r)
    { return r.generation != 0 && r.membershipKey.isNotEmpty() && r.rate >= 8000 && r.rate <= 384000
        && r.rate % 10 == 0 && (r.channels == 1 || r.channels == 2) && !r.members.empty() && r.members.size() <= maxMembers; }
    static juce::String requestIdentity (const SetRequest& r)
    {
        auto key = r.membershipKey + ":" + juce::String (r.generation) + ":" + juce::String (r.rate) + ":" + juce::String (r.channels);
        for (const auto& m : r.members)
        {
            key += "\n" + m.candidateId + ":" + m.sourceIdentityKey + ":" + m.sourceKind + ":" + m.sourceArtifact.relativePath + ":" + m.sourceArtifact.sha256
                + ":" + juce::String (m.sourceArtifact.bytes) + ":" + (m.prepared ? "1" : "0");
            for (const auto& c : m.cues) key += "\n" + c.cueId + ":" + juce::String (c.sampleRateHz)
                + ":" + juce::String (c.startSample) + ":" + juce::String (c.endSample);
        }
        return key;
    }
    bool authorityValid (const Result& r) const
    { return repository.verifySourceRevision (*r.authority).isEmpty(); }
    static double cpuSeconds()
    {
#if JUCE_WINDOWS
        return windows_detail::threadCpuSeconds();
#else
        timespec value {};
        return clock_gettime (CLOCK_THREAD_CPUTIME_ID, &value) == 0
            ? static_cast<double> (value.tv_sec) + static_cast<double> (value.tv_nsec) * 1.0e-9 : -1;
#endif
    }
    static Result measure (const RuntimeCandidate& candidate, const juce::File& root,
        std::uint32_t rate, std::uint32_t channels, const std::vector<CacheEntry>& cached, const std::atomic<bool>& cancel)
    {
        Result result;
        const auto fail = [&result] (const juce::String& reason) { result.reason = reason; return result; };
        if (cancel.load() || !candidate.prepared || candidate.cues.empty()) return fail ("reference_metric_member_unprepared");
        const auto began = std::chrono::steady_clock::now(); const auto cpuStart = cpuSeconds();
        RuntimeV2SourceRepository repo (root);
        const auto loaded = repo.load (candidate);
        if (!loaded.accepted()) return fail (loaded.rejectionCode);
        const auto& source = *loaded.source;
        const auto verified = repo.verifySourceFile (source, false);
        if (cancel.load()) return fail ("cancelled");
        if (verified.isNotEmpty())
        {
            result.preparationWait = verified == "reference_source_revalidating";
            return fail (verified);
        }
        const auto& rev = source.revision;
        result.key = source.sourceFileSha256 + "\n" + source.absolutePath + "\n" + source.sourceIdentityKey
            + "\n" + rev.deviceId + ":" + rev.fileId + ":" + rev.sizeBytes + ":" + rev.modifiedTimeNs + ":" + rev.changedTimeNs
            + ":" + juce::String (rate) + ":" + juce::String (channels) + ":native-metrics-v1:gain1";
        std::vector<Cue> cues;
        for (const auto& cue : candidate.cues)
        {
            if (cue.sampleRateHz <= 0 || cue.startSample < 0 || cue.endSample <= cue.startSample)
                return fail ("reference_metric_cue_invalid");
            const auto map = [rate, &cue] (std::int64_t sample) { return static_cast<long double> (sample) * rate / cue.sampleRateHz; };
            const auto start = map (cue.startSample), end = map (cue.endSample);
            const auto duration = static_cast<long double> (source.audio.totalSampleFrames) * rate / source.audio.sampleRateHz;
            if (!(source.audio.sampleRateHz > 0) || end > duration || end > static_cast<long double> (INT64_MAX))
                return fail ("reference_metric_cue_outside_source");
            const Cue region {static_cast<std::uint64_t> (std::llround (start)), static_cast<std::uint64_t> (std::llround (end))};
            if (region.end <= region.start) return fail ("reference_metric_cue_empty");
            cues.push_back (region);
            result.key += "\n" + juce::String (region.start) + ":" + juce::String (region.end);
        }
        auto description = std::make_shared<RuntimeSource> (source);
        description->preparedAudio = std::make_shared<revision_detail::PreparedSourceAudio>();
        result.authority = std::move (description);
        const auto hit = std::find_if (cached.begin(), cached.end(), [&result] (const auto& item) { return item.key == result.key; });
        if (hit != cached.end()) { result.summaries = hit->summaries; result.complete = true; return result; }
        auto scanned = scanPreparedSource (loaded.source, root, rate, channels, true, maxObservations, cancel);
        if (scanned.scan.state != ScanState::complete)
        {
            result.preparationWait = !cancel.load() && metricPreparationWait (scanned.scan.state, scanned.scan.sourceFrames);
            return fail (scanned.reason.isNotEmpty() ? scanned.reason : "reference_metric_set_scan_failed");
        }
        const auto wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - began).count();
        const auto cpuEnd = cpuSeconds();
        // Selected Cue facts publish independently before full EOF. A long background
        // set scan is diagnostic, not permission to weaken CPU or completeness gates.
        if (!metricBackgroundCpuBudget (wall, cpuStart, cpuEnd))
            return fail ("reference_metric_cpu_budget_exceeded");
        if (wall > 30.0) result.reason = "reference_metric_background_scan_long";
        if (cancel.load() || repo.verifySourceRevision (source).isNotEmpty()) return fail ("reference_source_changed");
        for (std::size_t i = 0; i < 3; ++i) result.summaries[i] = summarise (scanned.scan.frames, static_cast<Metric> (i), cues, true);
        result.complete = true;
        return result;
    }
    void publish (juce::String reason)
    {
        auto p = std::make_shared<SetPublication>();
        p->generation = current.generation; p->membershipKey = current.membershipKey;
        p->rate = current.rate; p->channels = current.channels; p->expected = current.members.size();
        std::array<std::vector<std::optional<Summary>>, 3> songs;
        for (const auto& r : results)
        {
            if (r.complete) ++p->completed;
            if (reason.isEmpty() && r.reason.isNotEmpty()) reason = r.reason;
            for (std::size_t i = 0; i < 3; ++i) songs[i].push_back (r.complete ? std::optional<Summary> (r.summaries[i]) : std::nullopt);
        }
        p->complete = valid (current) && p->completed == p->expected;
        if (p->complete) for (std::size_t i = 0; i < 3; ++i) p->bands[i] = setBand (songs[i]);
        p->reason = reason;
        std::atomic_store (&published, std::shared_ptr<const SetPublication> (std::move (p)));
    }
    juce::File root;
    RuntimeV2SourceRepository repository;
    Job job;
    std::shared_ptr<void> publisherLease;
    std::uint64_t serial = 1;
    juce::String identity;
    SetRequest current;
    std::vector<Result> results;
    std::vector<bool> done;
    std::vector<unsigned> retries;
    std::vector<double> retryAt;
    std::vector<CacheEntry> cache;
    std::optional<std::size_t> active;
    std::shared_ptr<Result> pending;
    std::shared_ptr<const SetPublication> published;
};
}
