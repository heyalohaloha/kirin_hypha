#pragma once
#include "ReferenceMetricPageReader.h"
#include "ReferencePinnedSource.h"
#include <chrono>
#include <thread>
#if JUCE_MAC
 #include <pthread.h>
#endif

namespace hypha::reference_audition::metrics
{
#if JUCE_WINDOWS
namespace windows_detail { bool lowerThreadPriority(); }
#endif

struct CueScanRequest
{
    Cue cue;
    unsigned warmupSeconds = 5;
    std::uint8_t verifiedMetricMask = 0;
};
struct SourceScanResult
{
    ScanResult scan;
    juce::String reason;
};
// Called inside Job::Work, not before admission. Only verified private snapshots may
// feed this independent reader. The result still needs the caller's selection/host
// generation check; a successful scan grants no ongoing source authority.
inline SourceScanResult scanPreparedSource (std::shared_ptr<const RuntimeSource> source,
    const juce::File& transportRoot, std::uint32_t rate, std::uint32_t channels,
    bool conversionApproved, std::size_t maxObservations, const std::atomic<bool>& cancelled, const ScanProgress& progress = {}, const std::optional<ScanWindow>& window = {}, const std::optional<CueScanRequest>& cueRequest = {})
{
    const auto failed = [] (ScanState state, const juce::String& reason) {
        return SourceScanResult {{state, {}, 0}, reason};
    };
    if (cancelled.load (std::memory_order_acquire)) return failed (ScanState::cancelled, "cancelled");
    if (!source || rate < 8000 || rate > 384000 || rate % 10 != 0 || (channels != 1 && channels != 2))
        return failed (ScanState::invalidFormat, "reference_metric_format_unsupported");
   #if JUCE_MAC
    if (pthread_set_qos_class_self_np (QOS_CLASS_UTILITY, 0) != 0)
        return failed (ScanState::analysisFailed, "reference_metric_priority_unavailable");
   #elif JUCE_WINDOWS
    if (!windows_detail::lowerThreadPriority())
        return failed (ScanState::analysisFailed, "reference_metric_priority_unavailable");
   #endif
    if (window && cueRequest) return failed (ScanState::invalidFormat, "reference_metric_range_unsupported");
    if (window && window->start > static_cast<std::uint64_t> (std::numeric_limits<std::int64_t>::max()))
        return failed (ScanState::invalidFormat, "reference_metric_range_unsupported");
    const auto prepared = revision_detail::snapshotCache().prepared (*source);
    if (!prepared) return failed (ScanState::waiting, "reference_metric_source_preparing");
    if (!prepared->hasPrivateCopy())
        return failed (ScanState::readFailed, "reference_metric_private_copy_required");
    if (!revision_detail::equal (prepared->revision(), source->revision))
        return failed (ScanState::readFailed, "reference_source_changed");
    RuntimeV2SourceRepository repository (transportRoot);
    const auto pinned = std::make_shared<revision_detail::PinnedSource> (source);
    if (cancelled.load (std::memory_order_acquire)) return failed (ScanState::cancelled, "cancelled");
    AudioPages pages { AudioPages::ServiceMode::manual };
    const auto reason = pages.open (*source, rate, static_cast<int> (channels), conversionApproved,
        [&repository, pinned] (RuntimeFileRevision& revision) { return pinned->verified (repository, revision); },
        pinned->input(), [pinned] { return pinned->sameIdentity(); }, [pinned] { return pinned->wasRejected(); },
        pinned->authorityOwner(), pinned->invalidation(), pinned->initialInvalidation(),
        [pinned] { return pinned->readSucceeded(); }, window ? static_cast<std::int64_t> (window->start) : 0);
    if (cancelled.load (std::memory_order_acquire)) return failed (ScanState::cancelled, "cancelled");
    if (reason.isNotEmpty() || pages.sourceInvalidated())
        return failed (reason == "reference_source_revalidating" ? ScanState::waiting : ScanState::readFailed,
                       reason.isNotEmpty() ? reason : juce::String ("reference_source_changed"));
    const auto length = pages.lengthInSamples();
    if (length <= 0) return failed (ScanState::readFailed, "reference_metric_empty_source");
    auto effectiveWindow = window;
    if (cueRequest) {
        // Host-converted length belongs to AudioPages, never estimate it from native metadata.
        effectiveWindow = cueScanWindow (rate, static_cast<std::uint64_t> (length), cueRequest->cue,
                                        cueRequest->warmupSeconds, cueRequest->verifiedMetricMask);
        if (!effectiveWindow) return failed (ScanState::invalidFormat, "reference_metric_range_unsupported");
    }
    PageReader reader (pages, rate, channels, cancelled);
    // Yield at least as long as the preceding aperture's wall work. Wall work
    // bounds this worker's CPU time from above, including decode/native analysis;
    // utility priority alone is not a CPU budget. Cancellation remains responsive.
    using Clock = std::chrono::steady_clock;
    auto workBegan = Clock::now();
    const auto rest = [&] {
        const auto work = Clock::now() - workBegan;
        const auto until = Clock::now() + work + work / 5;
        while (!cancelled.load (std::memory_order_acquire) && Clock::now() < until)
            std::this_thread::sleep_for (std::min (until - Clock::now(),
                std::chrono::duration_cast<Clock::duration> (std::chrono::milliseconds (5))));
        workBegan = Clock::now();
    };
    auto result = scan (rate, channels, static_cast<std::uint64_t> (length), maxObservations,
        [&] (std::uint64_t at, float* target, std::size_t count) {
            if (at != 0) rest();
            return reader.read (at, target, count);
        }, cancelled, progress ? ScanProgress { [&] (const auto& frames, std::uint64_t total) {
            RuntimeFileRevision current;
            if (cancelled.load (std::memory_order_acquire) || !pinned->verified (repository, current)
                || pages.sourceInvalidated() || pages.sourceUnavailable()) return false;
            return progress (frames, total);
        } } : ScanProgress {}, effectiveWindow);
    rest();
    RuntimeFileRevision revision;
    if (cancelled.load (std::memory_order_acquire)) return failed (ScanState::cancelled, "cancelled");
    if (!pinned->verified (repository, revision) || pages.sourceInvalidated() || pages.sourceUnavailable())
        return failed (ScanState::readFailed, "reference_source_changed");
    return {std::move (result), {}};
}
}
