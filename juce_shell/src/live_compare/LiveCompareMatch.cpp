#include "LiveCompareMatch.h"

#include "kirin_hypha_reference_ffi.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace hypha::live_compare
{
namespace
{
bool copyPost (const PostRenderer::HistoryView& view, std::int64_t start, std::int64_t frames,
               std::vector<float>& out)
{
    const auto mask = view.frames - 1;
    for (std::int64_t i = 0; i < frames; ++i)
    {
        const auto slot = static_cast<std::size_t> ((start + i) & mask) * 2;
        out[static_cast<std::size_t> (i) * 2] = view.samples[slot].load (std::memory_order_relaxed);
        out[static_cast<std::size_t> (i) * 2 + 1] = view.samples[slot + 1].load (std::memory_order_relaxed);
    }
    return true;
}

// PRE keeps writing ahead of the window; the copy is valid when, afterwards, the window still lies
// inside PRE's current run and within the ring capacity of its write end.
bool copyPre (const Ring& ring, std::int64_t start, std::int64_t frames, std::vector<float>& out)
{
    const auto& h = ring.header;
    const auto runBefore = h.run.load (std::memory_order_acquire);
    if (start < h.runStart.load (std::memory_order_acquire) || h.writeEnd.load (std::memory_order_acquire) < start + frames)
        return false;
    for (std::int64_t i = 0; i < frames; ++i)
    {
        out[static_cast<std::size_t> (i) * 2] = ring.samples[sampleSlot (start + i, 0)].load (std::memory_order_relaxed);
        out[static_cast<std::size_t> (i) * 2 + 1] = ring.samples[sampleSlot (start + i, 1)].load (std::memory_order_relaxed);
    }
    std::atomic_thread_fence (std::memory_order_acquire);
    return h.run.load (std::memory_order_relaxed) == runBefore
        && h.writeEnd.load (std::memory_order_relaxed) - start <= static_cast<std::int64_t> (ringCapacityFrames);
}

bool analyse (const std::vector<float>& post, const std::vector<float>& pre, std::int64_t frames,
              std::uint32_t sampleRate, MatchResult& result, double& postPeak, double& prePeak)
{
    KirinReferenceGainFacts active {};
    if (kirin_hypha_analyze_reference_gain (post.data(), pre.data(), static_cast<std::size_t> (frames),
                                            sampleRate, 2, &active))
    {
        result.measuredDb = active.paired_loudness_delta_median_millilu / 1000.0;
        postPeak = active.a_cue_true_peak_millidbtp / 1000.0;
        prePeak = active.b_cue_true_peak_millidbtp / 1000.0;
        result.analysisUnits = active.paired_block_count;
        return true;
    }
    KirinTrackEventGainFacts events {};
    if (kirin_hypha_analyze_track_event_gain (post.data(), pre.data(), static_cast<std::size_t> (frames),
                                              sampleRate, 2, &events))
    {
        result.measuredDb = events.paired_energy_delta_millidb / 1000.0;
        postPeak = events.post_cue_true_peak_millidbtp / 1000.0;
        prePeak = events.pre_cue_true_peak_millidbtp / 1000.0;
        result.analysisUnits = events.paired_window_count;
        return true;
    }
    return false;
}
}

MatchResult computeMatch (const Ring& ring, const PostRenderer& renderer, std::uint32_t sampleRate,
                          double maximumSeconds, double minimumSeconds)
{
    MatchResult result;
    const auto view = renderer.historyView();
    if (view.samples == nullptr || ! view.kValid)
        return result;
    const auto available = std::min (view.end - view.start, view.frames - 1);
    const auto wanted = static_cast<std::int64_t> (maximumSeconds * sampleRate);
    const auto frames = std::min (available, wanted);
    if (frames < static_cast<std::int64_t> (minimumSeconds * sampleRate))
    {
        result.failure = MatchFailure::tooShort;
        return result;
    }
    const auto postStart = view.end - frames;
    std::vector<float> post (static_cast<std::size_t> (frames) * 2), pre (post.size());
    copyPost (view, postStart, frames, post);
    // The history must not have advanced past the window while it was copied.
    if (renderer.historyWriteEnd() - postStart > view.frames
        || ! copyPre (ring, postStart - view.k, frames, pre))
    {
        result.failure = MatchFailure::overwritten;
        return result;
    }
    double postPeak = 0.0, prePeak = 0.0;
    if (! analyse (post, pre, frames, sampleRate, result, postPeak, prePeak))
    {
        result.failure = MatchFailure::notEnoughSignal;
        return result;
    }
    result.seconds = static_cast<double> (frames) / sampleRate;
    result.ceilingDbtp = std::max ({ -1.0, postPeak, prePeak });
    result.appliedDb = result.measuredDb;
    if (result.measuredDb > 0.0 && prePeak + result.measuredDb > result.ceilingDbtp + 1.0e-9)
    {
        result.limitedByTruePeak = true;
        result.appliedDb = std::max (0.0, result.ceilingDbtp - prePeak);
    }
    result.failure = MatchFailure::none;
    return result;
}
}
