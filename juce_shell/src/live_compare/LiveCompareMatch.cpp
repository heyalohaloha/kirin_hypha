#include "LiveCompareMatch.h"
#include "LiveCompareWindows.h"

#include "kirin_hypha_reference_ffi.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace hypha::live_compare
{
namespace
{
bool analyse (const std::vector<float>& post, const std::vector<float>& pre, std::int64_t frames,
              std::uint32_t sampleRate, MatchResult& result)
{
    KirinReferenceGainFacts active {};
    if (kirin_hypha_analyze_reference_gain (post.data(), pre.data(), static_cast<std::size_t> (frames),
                                            sampleRate, 2, &active))
    {
        result.measuredDb = active.paired_loudness_delta_median_millilu / 1000.0;
        result.postPeakDbtp = active.a_cue_true_peak_millidbtp / 1000.0;
        result.prePeakDbtp = active.b_cue_true_peak_millidbtp / 1000.0;
        result.analysisUnits = active.paired_block_count;
        return true;
    }
    KirinTrackEventGainFacts events {};
    if (kirin_hypha_analyze_track_event_gain (post.data(), pre.data(), static_cast<std::size_t> (frames),
                                              sampleRate, 2, &events))
    {
        result.measuredDb = events.paired_energy_delta_millidb / 1000.0;
        result.postPeakDbtp = events.post_cue_true_peak_millidbtp / 1000.0;
        result.prePeakDbtp = events.pre_cue_true_peak_millidbtp / 1000.0;
        result.analysisUnits = events.paired_window_count;
        return true;
    }
    return false;
}
}

MatchPlan planMatch (const MatchResult& result, double heldPostDb) noexcept
{
    MatchPlan plan;
    plan.generation = result.generation;
    plan.generationBound = result.generationBound;
    plan.ceilingDbtp = result.ceilingDbtp;
    const double held = std::min (0.0, heldPostDb);
    const double needed = result.measuredDb + held; // POST already sounds `held` dB quieter
    plan.neededPreGainDb = needed;
    plan.postGainDb = held;
    if (needed <= 0.0 || result.prePeakDbtp + needed <= result.ceilingDbtp + 1.0e-9)
    {
        plan.preGainDb = needed;
        return plan;
    }
    plan.needsApproval = true;
    plan.lowerPostGainDb = held - needed;
    plan.limitedPreGainDb = std::max (0.0, result.ceilingDbtp - result.prePeakDbtp);
    plan.preGainDb = plan.limitedPreGainDb;
    return plan;
}

FollowStep followStep (const MatchResult& result, double heldPostDb, double approvedPreDb, double ceilingDbtp,
                       double currentPreDb) noexcept
{
    FollowStep step;
    if (! result.ok())
        return step;
    const double needed = result.measuredDb + std::min (0.0, heldPostDb);
    if (result.prePeakDbtp + needed > ceilingDbtp + 1.0e-9)
        step.action = FollowAction::stopCeiling;
    else if (std::fabs (needed - approvedPreDb) > followReachDb)
        step.action = FollowAction::stopReach;
    else if (std::fabs (needed - currentPreDb) >= followToleranceDb)
    {
        step.action = FollowAction::move;
        step.preGainDb = needed;
    }
    return step;
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
    copyPostHistory (view, postStart, frames, post);
    // The history must not have advanced past the window while it was copied.
    if (renderer.historyWriteEnd() - postStart > view.frames
        || ! copyPreRing (ring, postStart - view.k, frames, pre))
    {
        result.failure = MatchFailure::overwritten;
        return result;
    }
    if (! analyse (post, pre, frames, sampleRate, result))
    {
        result.failure = MatchFailure::notEnoughSignal;
        return result;
    }
    result.seconds = static_cast<double> (frames) / sampleRate;
    result.ceilingDbtp = std::max ({ -1.0, result.postPeakDbtp, result.prePeakDbtp });
    result.failure = std::isfinite (result.measuredDb) && std::fabs (result.measuredDb) <= maximumMatchDb
        ? MatchFailure::none : MatchFailure::outOfRange;
    return result;
}
}
