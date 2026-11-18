#pragma once
#include "ReferenceMetricSeries.h"
#include <atomic>
#include <functional>
#include <memory>

namespace hypha::reference_audition::metrics
{
enum class ReadState { rendered, waiting, failed };
enum class ScanState { complete, cancelled, waiting, readFailed, invalidFormat, capacityExceeded, analysisFailed };
struct ScanResult
{
    ScanState state = ScanState::analysisFailed;
    std::vector<KirinReferenceMetricFrame> frames;
    std::uint64_t sourceFrames = 0;
    std::uint64_t scannedFrom = 0, scannedThrough = 0;
    bool wholeSource = true; // false for Cue ranges, never admit as a full-series cache
    Cue completeCue; // exact requested (start,end], not scanned warmup/lookahead
    std::uint8_t completeMetricMask = 7; // bits 0 PSR, 1 CREST, 2 SHARP; complement requires origin scan
};
// Only an independent non-RT job calls this. The reader must provide the same verified,
// gain-1 host-rate/channel PCM as AudioPages, not an audition bank or UI-owned reader.
// Waiting/failure/cancellation never publishes a partial series as a completed scan.
// The job boundary must catch allocation exceptions and recheck its immutable generation,
// source authority and host configuration before accepting a result. A fresh cancellation
// token belongs to each job; never turn a cancelled token back to false.
struct ScanWindow
{
    std::uint64_t start = 0, stop = 0; // aligned input boundaries, stop <= real complete PCM
    Cue retain; // only (start, end] observations retained; warmup/lookahead discarded
    std::uint8_t verifiedMetricMask = 0; // nonzero-origin metrics require independent convergence evidence
};
inline std::optional<ScanWindow> cueScanWindow (std::uint32_t rate, std::uint64_t sourceFrames,
                                               Cue cue, unsigned warmupSeconds, std::uint8_t verifiedMetricMask = 0)
{
    if (rate < 8000 || rate > 384000 || rate % 10 != 0 || cue.end <= cue.start
        || cue.end > sourceFrames || (warmupSeconds != 5 && warmupSeconds != 10)) return {};
    const std::uint64_t hop = rate / 10, warmup = static_cast<std::uint64_t> (rate) * warmupSeconds;
    const auto start = (cue.start > warmup ? cue.start - warmup : 0) / hop * hop;
    const auto last = cue.end / hop;
    const auto stop = std::min (sourceFrames / hop, last + 8) * hop;
    if (stop <= start || last <= cue.start / hop) return {};
    return ScanWindow { start, stop, cue, static_cast<std::uint8_t> (verifiedMetricMask & 7) };
}
using ScanProgress = std::function<bool (const std::vector<KirinReferenceMetricFrame>&, std::uint64_t)>;
using ReadAperture = std::function<ReadState (std::uint64_t, float*, std::size_t)>;
inline ScanResult scan (std::uint32_t rate, std::uint32_t channels, std::uint64_t sourceFrames,
                        std::size_t maxObservations, const ReadAperture& read,
                        const std::atomic<bool>& cancelled, const ScanProgress& progress = {}, const std::optional<ScanWindow>& window = {})
{
    const auto fail = [sourceFrames] (ScanState state) { return ScanResult {state, {}, sourceFrames}; };
    if (cancelled.load (std::memory_order_acquire)) return fail (ScanState::cancelled);
    if (rate < 8000 || rate > 384000 || rate % 10 != 0 || (channels != 1 && channels != 2) || !read)
        return fail (ScanState::invalidFormat);
    const auto aperture = rate / 10;
    const auto start = window ? window->start : 0;
    const auto stop = window ? window->stop : sourceFrames / aperture * aperture;
    if (sourceFrames > static_cast<std::uint64_t> (std::numeric_limits<std::int64_t>::max())
        || start % aperture != 0 || stop % aperture != 0 || stop < start || stop > sourceFrames
        || (window && (window->retain.end <= window->retain.start || window->retain.start < start
            || window->retain.end / aperture * aperture > stop || window->retain.end > sourceFrames))) return fail (ScanState::invalidFormat);
    const auto count = (stop - start) / aperture;
    if (count > maxObservations) return fail (ScanState::capacityExceeded);
    using Handle = std::unique_ptr<KirinReferenceMetrics, decltype (&kirin_reference_metrics_drop)>;
    Handle handle (kirin_reference_metrics_create_at (rate, channels, start), &kirin_reference_metrics_drop);
    if (!handle) return fail (ScanState::analysisFailed);
    ScanResult result {ScanState::complete, {}, sourceFrames};
    result.scannedFrom = start; result.scannedThrough = stop; result.wholeSource = !window;
    result.completeCue = window ? window->retain : Cue { 0, sourceFrames };
    result.completeMetricMask = window && start != 0 ? window->verifiedMetricMask & 7 : 7;
    result.frames.reserve (static_cast<std::size_t> (count));
    std::vector<float> input (static_cast<std::size_t> (aperture) * channels);
    KirinReferenceMetricFrame output[8] {};
    std::uint64_t seen = 0;
    const auto append = [&] (std::size_t written) {
        if (written > 8) return false;
        for (std::size_t i = 0; i < written; ++i)
        {
            const auto expected = start + (seen + 1) * static_cast<std::uint64_t> (aperture);
            if (seen >= count || output[i].end_sample != expected) return false;
            ++seen;
            if (!window || window->retain.containsEndpoint (output[i].end_sample)) {
                auto frame = output[i];
                if (!(result.completeMetricMask & 1)) frame.psr_db = missing;
                if (!(result.completeMetricMask & 2)) frame.crest_db = missing;
                if (!(result.completeMetricMask & 4)) frame.sharpness_acum = missing;
                result.frames.push_back (frame);
            }
        }
        return true;
    };
    for (std::uint64_t index = 0; index < count; ++index)
    {
        if (cancelled.load (std::memory_order_acquire)) return fail (ScanState::cancelled);
        // Poison each buffer so a reader that reports success without filling every
        // channel/sample fails the native finite-input validation, rather than reusing audio.
        std::fill (input.begin(), input.end(), std::numeric_limits<float>::quiet_NaN());
        ReadState state;
        try { state = read (start + index * aperture, input.data(), input.size()); }
        catch (...) { return fail (ScanState::readFailed); }
        if (cancelled.load (std::memory_order_acquire)) return fail (ScanState::cancelled);
        if (state == ReadState::waiting) return fail (ScanState::waiting);
        if (state != ReadState::rendered) return fail (ScanState::readFailed);
        std::size_t written = 0;
        if (!kirin_reference_metrics_push (handle.get(), input.data(), input.size(), output, 8, &written)
            || !append (written)) return fail (ScanState::analysisFailed);
        if (progress && written != 0 && !progress (result.frames, sourceFrames)) return fail (ScanState::readFailed);
    }
    if (cancelled.load (std::memory_order_acquire)) return fail (ScanState::cancelled);
    std::size_t written = 0;
    if (!kirin_reference_metrics_finish (handle.get(), output, 8, &written) || !append (written)
        || seen != count) return fail (ScanState::analysisFailed);
    if (cancelled.load (std::memory_order_acquire)) return fail (ScanState::cancelled);
    return result;
}
}
