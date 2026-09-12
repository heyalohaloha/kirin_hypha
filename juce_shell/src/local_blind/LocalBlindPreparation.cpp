#include "LocalBlindPreparation.h"
#include "kirin_hypha_reference_ffi.h"
#include <algorithm>
#include <cmath>

namespace hypha::local_blind
{
PreparedCandidate prepareLocalBlindCandidate (
    const ExactRangeCapture& post, const ExactRangeCapture& pre, const TrialFormat& f,
    std::int64_t expectedPreStart, std::size_t budget, GainMatchPolicy policy,
    const std::function<bool()>& random) noexcept
{
    PreparedCandidate result;
    result.gainPolicy = policy;
    try
    {
        const auto* a = post.completedPcm();
        const auto* b = pre.completedPcm();
        if (a == nullptr || b == nullptr)
        { result.failure = PreparationFailure::incompleteCapture; return result; }
        const auto matches = [&f] (const CaptureRange& r, std::int64_t start)
        {
            return r.generation == f.epochs.capture && r.sampleRate == f.sampleRate
                && r.channels == f.channels && r.frames == f.frames && r.start == start;
        };
        if (! f.epochs.valid() || ! matches (post.range(), f.start) || ! matches (pre.range(), expectedPreStart)
            || a->size() != b->size())
        { result.failure = PreparationFailure::rangeMismatch; return result; }
        if (a->size() > budget / sizeof (float) / 2)
        { result.failure = PreparationFailure::capacity; return result; }
        std::int64_t gainDeltaMilli = 0, postPeakMilli = 0, prePeakMilli = 0;
        if (policy == GainMatchPolicy::alignedActiveBlocksV1)
        {
            KirinReferenceGainFacts facts {};
            // Existing named policy stays intact: 400 ms / 100 ms hop, 27 contiguous active blocks.
            if (! kirin_hypha_analyze_reference_gain (
                    a->data(), b->data(), static_cast<std::size_t> (f.frames), f.sampleRate,
                    static_cast<std::uint32_t> (f.channels), &facts))
            { result.failure = PreparationFailure::gainUnavailable; return result; }
            gainDeltaMilli = facts.paired_loudness_delta_median_millilu;
            postPeakMilli = facts.a_cue_true_peak_millidbtp;
            prePeakMilli = facts.b_cue_true_peak_millidbtp;
            result.matchedAnalysisUnits = facts.paired_block_count;
        }
        else
        {
            KirinTrackEventGainFacts facts {};
            if (! kirin_hypha_analyze_track_event_gain (
                    a->data(), b->data(), static_cast<std::size_t> (f.frames), f.sampleRate,
                    static_cast<std::uint32_t> (f.channels), &facts))
            { result.failure = PreparationFailure::gainUnavailable; return result; }
            gainDeltaMilli = facts.paired_energy_delta_millidb;
            postPeakMilli = facts.post_cue_true_peak_millidbtp;
            prePeakMilli = facts.pre_cue_true_peak_millidbtp;
            result.matchedAnalysisUnits = facts.paired_window_count;
        }
        if (post.completedPcm() == nullptr || pre.completedPcm() == nullptr)
        { result.failure = PreparationFailure::incompleteCapture; return result; }
        result.fixedPreGainDb = gainDeltaMilli / 1000.0;
        const auto postPeak = postPeakMilli / 1000.0;
        const auto prePeak = prePeakMilli / 1000.0;
        const auto ceiling = std::max ({ -1.0, postPeak, prePeak });
        TrialGain gain;
        gain.fixedPre = static_cast<float> (std::pow (10.0, result.fixedPreGainDb / 20.0));
        gain.requiresLowerPostApproval = result.fixedPreGainDb > 0
            && prePeak + result.fixedPreGainDb > ceiling + 1.0e-9;
        if (gain.requiresLowerPostApproval)
        {
            result.lowerPostGainDb = -result.fixedPreGainDb;
            gain.lowerPost = static_cast<float> (std::pow (10.0, result.lowerPostGainDb / 20.0));
        }
        // Assignment is off RT and failure never falls back to a deterministic source order.
        const bool firstIsPre = random();
        result.trial = std::make_unique<LocalBlindTrial> (f, gain, *a, *b, firstIsPre, budget);
        if (post.completedPcm() == nullptr || pre.completedPcm() == nullptr)
        { result.trial.reset(); result.failure = PreparationFailure::incompleteCapture; }
    }
    catch (...)
    {
        result.trial.reset();
        result.failure = PreparationFailure::preparationFailed;
    }
    return result;
}
}
