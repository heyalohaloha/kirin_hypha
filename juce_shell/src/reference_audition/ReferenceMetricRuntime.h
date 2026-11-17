#pragma once
#include "ReferenceMetricPublication.h"
#include "ReferenceMetricJob.h"
#include "ReferenceMetricSourceScan.h"
#include <array>

namespace hypha::reference_audition::metrics
{
// Single existing control-worker owner. UI reads only the immutable publication;
// it never starts readers, sorts frames, or joins cancelled background work.
class Runtime final
{
    struct EarlyState
    {
        std::uint64_t serial = 0;
        juce::String key;
        std::uint32_t rate = 0, channels = 0;
        std::atomic<std::uint64_t> requestedEnd {0};
        std::shared_ptr<const SeriesData> prefix;
        juce::String finalReason; // Worker writes; control owner reads only after accepted Job::take.
    };
public:
    explicit Runtime (juce::File rootIn) : root (std::move (rootIn)), repository (root) {}
    std::shared_ptr<const Publication> snapshot() const { return std::atomic_load (&published); }
   #if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
    void waitForTest() { job.waitForTest(); }
    std::function<void()> afterEarlyForTest;
    std::size_t startsForTest() const { return testStarts; }
    static juce::String failureReasonForTest (ScanState state, const juce::String& raw) { return failureReason (state, raw); }
   #endif
    void clear()
    {
        job.cancel(); early.reset(); activeKey.clear(); attemptedKey.clear(); terminalReason.clear(); ++serial;
        std::atomic_store (&published, std::shared_ptr<const Publication> {});
    }
    void service (const std::shared_ptr<const RuntimeSource>& source, std::uint32_t rate,
                  std::uint32_t channels, std::uint64_t selectionGeneration, Cue cue)
    {
        if (selectionGeneration == 0) { clear(); return; }
        if (!source || rate < 8000 || rate > 384000 || rate % 10 != 0 || (channels != 1 && channels != 2))
        {
            clear(); publish (Availability::unavailable, selectionGeneration, cue, {}, source ? "unsupported_format" : "read_failed");
            return;
        }
        const auto sourceFailure = repository.verifySourceRevision (*source);
        if (sourceFailure.isNotEmpty())
        {
            clear();
            const auto reason = failureReason (ScanState::readFailed, sourceFailure);
            publish (reason == "preparing" ? Availability::measuring : Availability::unavailable,
                     selectionGeneration, cue, {}, reason);
            return;
        }
        const auto key = keyFor (*source, rate, channels);
        if (key != activeKey)
        {
            job.cancel(); early.reset(); activeKey = key; attemptedKey.clear(); terminalReason.clear(); ++serial;
            publish (Availability::measuring, selectionGeneration, cue, {});
        }
        if (cache && cacheKey == key)
        {
            const auto existing = snapshot();
            if (!existing || existing->state != Availability::ready || !existing->series
                || existing->selectionGeneration != selectionGeneration
                || lastCue.start != cue.start || lastCue.end != cue.end)
                publish (Availability::ready, selectionGeneration, cue, cache);
            return;
        }
        if (auto result = job.take (serial))
        {
            if (result->scan.state == ScanState::complete)
            {
                auto data = std::make_shared<SeriesData>();
                data->frames = std::move (result->scan.frames); data->sourceFrames = result->scan.sourceFrames;
                data->rate = rate; data->channels = channels; data->fullScanComplete = true;
                cache = std::move (data); cacheKey = key;
                publish (Availability::ready, selectionGeneration, cue, cache);
                return;
            }
            // Retry only preparation waits that consumed no PCM. A partially-read
            // source must not trigger unbounded full rescans every control tick.
            const auto reason = early && early->serial == serial && early->key == key
                ? early->finalReason : failureReason (result->scan.state, {});
            early.reset(); // A failed/cancelled source cannot leave an accepted prefix alive.
            const bool waits = result->scan.state == ScanState::waiting && result->scan.sourceFrames == 0;
            publish (waits ? Availability::measuring : Availability::unavailable, selectionGeneration, cue, {},
                     reason.isNotEmpty() ? reason : failureReason (result->scan.state, {}));
            if (waits) { attemptedKey.clear(); retryAt = juce::Time::getMillisecondCounterHiRes() + 1000.0; }
            else terminalReason = snapshot()->reason;
        }
        // A terminal source failure survives Cue/selection changes without another scan.
        // It belongs only to activeKey; clear/source/format changes discard it above.
        if (terminalReason.isNotEmpty())
        {
            const auto existing = snapshot();
            if (!existing || existing->selectionGeneration != selectionGeneration
                || lastCue.start != cue.start || lastCue.end != cue.end)
                publish (Availability::unavailable, selectionGeneration, cue, {}, terminalReason);
            return;
        }
        if (early && early->serial == serial && early->key == key && early->rate == rate && early->channels == channels)
        {
            early->requestedEnd.store (cue.end, std::memory_order_release);
            const auto prefix = std::atomic_load (&early->prefix);
            const auto existing = snapshot();
            if (covers (prefix, cue))
            {
                if (!existing || existing->state != Availability::ready || existing->series != prefix
                    || existing->selectionGeneration != selectionGeneration || lastCue.start != cue.start || lastCue.end != cue.end)
                    publish (Availability::ready, selectionGeneration, cue, prefix);
            }
            else if (!existing || existing->state != Availability::measuring || existing->selectionGeneration != selectionGeneration
                     || lastCue.start != cue.start || lastCue.end != cue.end)
                publish (Availability::measuring, selectionGeneration, cue, {});
        }
        if (attemptedKey == key || juce::Time::getMillisecondCounterHiRes() < retryAt) return;
        // The description must not retain the controller's prepared snapshot. A refused
        // start may destroy it on the caller; actual I/O ownership starts inside Work.
        auto description = *source;
        description.preparedAudio = std::make_shared<revision_detail::PreparedSourceAudio>();
        const auto taskRoot = root;
        auto progressState = std::make_shared<EarlyState>();
        progressState->serial = serial; progressState->key = key; progressState->rate = rate; progressState->channels = channels;
        progressState->requestedEnd.store (cue.end);
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
        const auto earlyHook = afterEarlyForTest;
#endif
        const auto started = job.start (serial, [description = std::move (description), taskRoot, rate, channels, progressState
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
            , earlyHook
#endif
            ]
            (const std::atomic<bool>& cancelled) mutable {
                auto privateDescription = std::make_shared<RuntimeSource> (description);
                std::uint64_t lastPublishedEnd = 0;
                auto scanned = scanPreparedSource (std::move (privateDescription), taskRoot, rate, channels,
                    true, maxObservations, cancelled, [&] (const auto& frames, std::uint64_t sourceFrames) {
                        if (cancelled.load (std::memory_order_acquire)) return false;
                        const auto target = progressState->requestedEnd.load (std::memory_order_acquire);
                        if (frames.empty() || target == 0 || frames.back().end_sample < target) return true;
                        const auto end = frames.back().end_sample;
                        // A covered Cue needs no growing copies. Publish again only when
                        // a newly enlarged Cue requires a later exact endpoint.
                        if (target <= lastPublishedEnd) return true;
                        auto prefix = std::make_shared<SeriesData>();
                        prefix->frames = frames; prefix->sourceFrames = sourceFrames;
                        prefix->rate = rate; prefix->channels = channels;
                        if (cancelled.load (std::memory_order_acquire)) return false;
                        std::atomic_store (&progressState->prefix, std::shared_ptr<const SeriesData> (std::move (prefix)));
                        lastPublishedEnd = end;
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
                        if (earlyHook) earlyHook();
#endif
                        return true;
                    });
                progressState->finalReason = failureReason (scanned.scan.state, scanned.reason);
                return std::move (scanned.scan);
            });
        if (started == Job::Start::started)
        {
            attemptedKey = key; early = std::move (progressState);
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
            ++testStarts;
#endif
        }
        else if (started == Job::Start::unavailable)
        { attemptedKey = key; terminalReason = "analysis_failed"; publish (Availability::unavailable, selectionGeneration, cue, {}, terminalReason); }
    }
private:
    static juce::String failureReason (ScanState state, const juce::String& raw)
    {
        if (raw == "reference_source_revalidating" || raw == "reference_metric_source_preparing") return "preparing";
        if (raw == "reference_source_changed") return "source_changed";
        if (raw == "reference_metric_private_copy_required") return "private_copy_required";
        if (raw == "reference_metric_empty_source") return "audio_too_short";
        if (raw == "reference_metric_format_unsupported" || state == ScanState::invalidFormat) return "unsupported_format";
        if (state == ScanState::capacityExceeded) return "capacity_exceeded";
        if (state == ScanState::waiting) return "preparing";
        if (state == ScanState::readFailed) return "read_failed";
        if (state == ScanState::complete && raw.isEmpty()) return {};
        return "analysis_failed"; // Raw paths/errors are never published to the face.
    }
    static bool covers (const std::shared_ptr<const SeriesData>& data, Cue cue)
    {
        return data && cue.end > cue.start && cue.end <= data->sourceFrames
            && (data->fullScanComplete || (!data->frames.empty() && cue.end <= data->frames.back().end_sample));
    }
    static juce::String keyFor (const RuntimeSource& source, std::uint32_t rate, std::uint32_t channels)
    {
        const auto& r = source.revision;
        return source.absolutePath + "\n" + source.sourceIdentityKey + "\n" + source.sourceFileSha256
            + "\n" + r.deviceId + ":" + r.fileId + ":" + r.sizeBytes + ":" + r.modifiedTimeNs + ":" + r.changedTimeNs
            + "\n" + juce::String (rate) + ":" + juce::String (channels) + ":native-metrics-v1:gain1";
    }
    void publish (Availability state, std::uint64_t generation, Cue cue, std::shared_ptr<const SeriesData> data, juce::String reason = {})
    {
        auto next = std::make_shared<Publication>();
        if (state == Availability::ready)
        {
            if (data && data->fullScanComplete && data->sourceFrames < data->rate / 10)
            { state = Availability::unavailable; reason = "audio_too_short"; }
            else if (!covers (data, cue))
            { state = Availability::unavailable; reason = cue.end <= cue.start ? "cue_too_short" : "no_valid_value"; }
        }
        next->state = state; next->selectionGeneration = generation; next->sourceKey = activeKey;
        next->reason = std::move (reason);
        next->series = state == Availability::ready ? std::move (data) : nullptr;
        if (next->series)
        {
            const bool hasEndpoint = std::any_of (next->series->frames.begin(), next->series->frames.end(),
                [&] (const auto& frame) { return cue.containsEndpoint (frame.end_sample); });
            for (std::size_t i = 0; i < next->cue.size(); ++i)
            {
                next->cue[i] = summarise (next->series->frames, static_cast<Metric> (i), cue, true);
                if (next->cue[i].observations == 0 || !std::isfinite (next->cue[i].median))
                    next->cueReasons[i] = hasEndpoint ? "no_valid_value" : "cue_too_short";
            }
        }
        lastCue = cue;
        std::atomic_store (&published, std::shared_ptr<const Publication> (std::move (next)));
    }
    static constexpr std::size_t maxObservations = 36000; // One hour, bounded frames, never whole PCM.
    juce::File root;
    RuntimeV2SourceRepository repository;
    Job job;
    std::uint64_t serial = 1;
    juce::String activeKey, attemptedKey, cacheKey, terminalReason;
#if defined(KIRIN_REFERENCE_METRIC_JOB_TEST)
    std::size_t testStarts = 0;
#endif
    double retryAt = 0;
    Cue lastCue;
    std::shared_ptr<const SeriesData> cache;
    std::shared_ptr<EarlyState> early;
    std::shared_ptr<const Publication> published;
};
}
