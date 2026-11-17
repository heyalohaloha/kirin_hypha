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
};
// Only an independent non-RT job calls this. The reader must provide the same verified,
// gain-1 host-rate/channel PCM as AudioPages, not an audition bank or UI-owned reader.
// Waiting/failure/cancellation never publishes a partial series as a completed scan.
// The job boundary must catch allocation exceptions and recheck its immutable generation,
// source authority and host configuration before accepting a result. A fresh cancellation
// token belongs to each job; never turn a cancelled token back to false.
using ReadAperture = std::function<ReadState (std::uint64_t, float*, std::size_t)>;
inline ScanResult scan (std::uint32_t rate, std::uint32_t channels, std::uint64_t sourceFrames,
                        std::size_t maxObservations, const ReadAperture& read,
                        const std::atomic<bool>& cancelled)
{
    const auto fail = [sourceFrames] (ScanState state) { return ScanResult {state, {}, sourceFrames}; };
    if (cancelled.load (std::memory_order_acquire)) return fail (ScanState::cancelled);
    if (rate < 8000 || rate > 384000 || rate % 10 != 0 || (channels != 1 && channels != 2) || !read)
        return fail (ScanState::invalidFormat);
    const auto aperture = rate / 10;
    const auto count = sourceFrames / aperture;
    if (count > maxObservations) return fail (ScanState::capacityExceeded);
    using Handle = std::unique_ptr<KirinReferenceMetrics, decltype (&kirin_reference_metrics_drop)>;
    Handle handle (kirin_reference_metrics_create (rate, channels), &kirin_reference_metrics_drop);
    if (!handle) return fail (ScanState::analysisFailed);
    ScanResult result {ScanState::complete, {}, sourceFrames};
    result.frames.reserve (static_cast<std::size_t> (count));
    std::vector<float> input (static_cast<std::size_t> (aperture) * channels);
    KirinReferenceMetricFrame output[8] {};
    const auto append = [&] (std::size_t written) {
        if (written > 8) return false;
        for (std::size_t i = 0; i < written; ++i)
        {
            const auto expected = (result.frames.size() + 1) * static_cast<std::uint64_t> (aperture);
            if (result.frames.size() >= count || output[i].end_sample != expected) return false;
            result.frames.push_back (output[i]);
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
        try { state = read (index * aperture, input.data(), input.size()); }
        catch (...) { return fail (ScanState::readFailed); }
        if (cancelled.load (std::memory_order_acquire)) return fail (ScanState::cancelled);
        if (state == ReadState::waiting) return fail (ScanState::waiting);
        if (state != ReadState::rendered) return fail (ScanState::readFailed);
        std::size_t written = 0;
        if (!kirin_reference_metrics_push (handle.get(), input.data(), input.size(), output, 8, &written)
            || !append (written)) return fail (ScanState::analysisFailed);
    }
    if (cancelled.load (std::memory_order_acquire)) return fail (ScanState::cancelled);
    std::size_t written = 0;
    if (!kirin_reference_metrics_finish (handle.get(), output, 8, &written) || !append (written)
        || result.frames.size() != count) return fail (ScanState::analysisFailed);
    if (cancelled.load (std::memory_order_acquire)) return fail (ScanState::cancelled);
    return result;
}
}
