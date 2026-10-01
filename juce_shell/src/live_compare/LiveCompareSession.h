#pragma once

#include "LiveCompareCorrespondence.h"
#include "LiveCompareRecovery.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>

namespace hypha::live_compare
{
// PRE, Audio Thread: publishes the block only while a POST session demands PRE input. When the
// demand ends, the next demanded block opens a new run.
class PreFeeder
{
public:
    bool feed (Ring& ring, const BlockClock& block, const float* const* input, int channels) noexcept
    {
        if (ring.header.timing.sequence.load (std::memory_order_relaxed) == 0) publisher.reset();
        const auto continuity = timing.observe (ring.header.timing, block, ring.header.sampleRate.load (std::memory_order_relaxed));
        if (ring.header.demand.load (std::memory_order_acquire) == 0)
        {
            publisher.reset();
            return false;
        }
        publisher.publishAfterClock (ring, block, input, channels, continuity);
        return true;
    }

private:
    TimingPublisher timing;
    Publisher publisher;
};

// POST's audible level while a session runs or an approved attenuation is held (INV-LC14). It
// follows its target linearly in gain, down over 50 ms and up over 500 ms for the full range, so
// that applying an approved attenuation never clicks and returning to normal never jumps. The
// steps are atomics because the message thread configures them for the prepared sample rate.
class PostLevel
{
public:
    void configure (double sampleRate) noexcept
    {
        const auto frames = [sampleRate] (double seconds)
        { return static_cast<float> (std::max (1.0, sampleRate * seconds)); };
        downStep.store (1.0f / frames (0.05), std::memory_order_relaxed);
        upStep.store (1.0f / frames (0.5), std::memory_order_relaxed);
    }

    // Audio Thread: the gain for the next frame on the way to target (at most 1).
    float next (float target) noexcept
    {
        current = current > target ? std::max (target, current - downStep.load (std::memory_order_relaxed))
                                   : std::min (target, current + upStep.load (std::memory_order_relaxed));
        return current;
    }

    // Audio Thread: POST alone. At unity, settled, the audio is left bit-identical.
    void apply (float* const* io, int channels, int frames, float target) noexcept
    {
        if (target >= 1.0f && current >= 1.0f)
            return;
        for (int i = 0; i < frames; ++i)
        {
            const float gain = next (target);
            for (int channel = 0; channel < channels; ++channel)
                io[channel][i] *= gain;
        }
    }

    float value() const noexcept { return current; }

private:
    float current = 1.0f; // Audio Thread only
    std::atomic<float> downStep { 1.0f / 2400.0f }, upStep { 1.0f / 24000.0f };
};

// The PRE gain on its way to a new MATCH or AUTO value (plan 6.2): a linear ramp over 50 ms, so a
// gain that changes while PRE sounds never steps. While PRE is silent it moves at once.
class GainRamp
{
public:
    void configure (double sampleRate) noexcept
    {
        rampFrames = std::max (1, static_cast<int> (sampleRate * 0.05 + 0.5));
        settle (1.0f);
    }

    // Audio Thread: the gain for the next frame on the way to wanted. Each frame is placed on the
    // line from the ramp's start, so no rounding accumulates into a step.
    float next (float wanted) noexcept
    {
        if (std::fabs (wanted - target) > 0.0f)
        {
            start = current;
            target = wanted;
            position = 0;
        }
        if (position < rampFrames)
        {
            ++position;
            current = position >= rampFrames
                ? target
                : start + (target - start) * (static_cast<float> (position) / static_cast<float> (rampFrames));
        }
        return current;
    }

    // Audio Thread: PRE is silent, nothing hears the gain move.
    void settle (float wanted) noexcept
    {
        start = current = target = wanted;
        position = rampFrames;
    }

    // The largest gain the next block reaches: a rising ramp is checked at its end.
    float peak (float wanted) const noexcept { return std::max (current, wanted); }

    bool settledAt (float wanted) const noexcept
    {
        return position >= rampFrames && current >= wanted && current <= wanted
            && target >= wanted && target <= wanted;
    }

private:
    float start = 1.0f, current = 1.0f, target = 1.0f; // Audio Thread only
    int rampFrames = 2400, position = 2400;
};

struct RenderReport
{
    enum class AudibleSource : std::uint8_t { none, post, pre };

    Verdict verdict = Verdict::noClock;
    RecoveryReason reason = RecoveryReason::none;
    bool preAudible = false;   // PRE weight above zero at the end of the block
    bool preWaiting = false;   // PRE is selected but POST sounds because the block is not proven
    bool guardTripped = false; // PRE was not finite or, raised, peaked above the ceiling
    bool stableSource = false; // every frame used one direct, exact source path
    bool gainSettled = false;  // every frame used the command's PRE and POST gain targets
    bool timelineChanged = false;
    AudibleSource audibleSource = AudibleSource::none;
};

// POST, Audio Thread output (INV-LC4, INV-LC14). PRE sounds only in blocks whose every frame is
// proven and passes the guard. Returning to PRE fades symmetrically over proven samples; losing the
// proof switches to POST at the block start and never uses unproven PRE samples. The approved gain
// applies to the PRE copy, ramped when it changes while PRE sounds; an approved POST attenuation
// applies to POST, in and out of a session.
class PostRenderer
{
public:
    // Non-RT: sizes the scratch for the largest realtime block, the fade length and the POST input
    // history that MATCH reads (a power of two of at least historySeconds).
    void prepare (int maximumFrames, double sampleRate)
    {
        capacity = std::max (0, maximumFrames);
        left = std::make_unique<float[]> (static_cast<std::size_t> (capacity));
        right = std::make_unique<float[]> (static_cast<std::size_t> (capacity));
        fadeFrames = std::max (1, static_cast<int> (sampleRate * fadeSeconds + 0.5));
        preLevel.configure (sampleRate);
        std::int64_t frames = 1;
        while (static_cast<double> (frames) < sampleRate * historySeconds)
            frames <<= 1;
        historyFrames = frames;
        history = std::make_unique<std::atomic<float>[]> (static_cast<std::size_t> (frames) * 2);
        historyStart.store (0, std::memory_order_relaxed);
        historyEnd.store (0, std::memory_order_relaxed);
        projectRunStart.store (0, std::memory_order_relaxed);
        projectOffset.store (0, std::memory_order_relaxed);
        projectKnown.store (false, std::memory_order_relaxed);
        matchOffsetValid.store (false, std::memory_order_relaxed);
        consumer.reset();
        weight = 0.0f;
    }

    bool adoptInitialTiming (const Ring& ring, const BlockClock& block,
                             const TimingEvidence& evidence) noexcept
    { return consumer.adoptInitialTiming (ring, block, evidence); }

    // MATCH (non-RT) reads the POST input history, contiguous over [start, end) of POST's clock,
    // and the K that mapped the latest block, then verifies the history was not overwritten.
    struct HistoryView
    {
        const std::atomic<float>* samples = nullptr; // interleaved stereo, historyFrames long
        std::int64_t frames = 0;
        std::int64_t start = 0, end = 0, k = 0;
        bool kValid = false;
        std::uint64_t proof = 0, preRun = 0;
    };

    HistoryView historyView() const noexcept
    {
        HistoryView view;
        const auto seq = historySequence.load();
        if ((seq & 1u) != 0) return view;
        view.samples = history.get();
        view.frames = historyFrames;
        view.end = historyEnd.load (std::memory_order_acquire);
        view.start = historyStart.load (std::memory_order_acquire);
        view.k = matchOffset.load (std::memory_order_acquire);
        view.kValid = matchOffsetValid.load (std::memory_order_acquire);
        view.proof = historyProof.load();
        view.preRun = historyPreRun.load();
        if (seq != historySequence.load()) return {};
        return view;
    }

    bool historyStillValid (const HistoryView& view, std::int64_t start) const noexcept
    {
        const auto now = historyView();
        return now.kValid && now.proof == view.proof && now.preRun == view.preRun && now.k == view.k
            && start >= now.start && now.end - start <= now.frames;
    }

    std::int64_t historyWriteEnd() const noexcept { return historyEnd.load (std::memory_order_acquire); }

    // The stretch of the history whose project time advanced with POST's clock: from runStart on,
    // project = clock + offset. A loop wrap, a seek, a stop or the host's post-wrap clamp starts a
    // new stretch. A Pin needs its whole window inside one.
    struct ProjectView
    {
        std::int64_t runStart = 0, offset = 0;
        bool known = false;
    };

    bool projectView (ProjectView& out) const noexcept
    {
        const auto before = projectRunStart.load (std::memory_order_acquire);
        out.offset = projectOffset.load (std::memory_order_relaxed);
        out.known = projectKnown.load (std::memory_order_relaxed);
        std::atomic_thread_fence (std::memory_order_acquire);
        out.runStart = projectRunStart.load (std::memory_order_relaxed);
        return out.runStart == before;
    }

    RenderReport render (const Ring& ring, std::uint64_t pairKey, std::uint32_t sampleRate,
                         const BlockClock& block, float* const* io, int channels,
                         bool preSelected, float preGain, PostLevel& post, float postTarget,
                         float ceilingLinear, bool blind = false) noexcept
    {
        RenderReport report;
        if (io == nullptr || channels <= 0 || channels > 2 || block.frames <= 0)
        {
            invalidateHistory();
            report.reason = RecoveryReason::formatChanged;
            weight = 0.0f;
            report.preWaiting = preSelected;
            return report;
        }
        if (block.frames > capacity)
        {
            invalidateHistory();
            report.reason = RecoveryReason::blockTooLarge;
            weight = 0.0f;
            report.preWaiting = preSelected;
            post.apply (io, channels, block.frames, postTarget);
            return report;
        }
        float* scratch[] = { left.get(), right.get() };
        const auto decision = consumer.process (ring, pairKey, sampleRate, block, scratch, 2);
        recordHistory (block, io, channels, decision);
        report.timelineChanged = decision.timelineChanged || decision.invalidatedByDisagreement;
        report.verdict = decision.verdict;
        report.reason = recoveryReason (decision.verdict);
        if (report.timelineChanged)
            report.reason = block.afterGap ? RecoveryReason::callbackGap
                : ! block.playing ? RecoveryReason::stopped
                : ! block.clockValid ? RecoveryReason::clockMissing
                : ! block.projectValid ? RecoveryReason::projectClockMissing : RecoveryReason::positionChanged;
        if (decision.verdict != Verdict::accepted)
        {
            weight = 0.0f; // switch to POST at the block start
            report.preWaiting = preSelected;
            post.apply (io, channels, block.frames, postTarget);
            return report;
        }
        if (weight <= 0.0f)
            preLevel.settle (preGain);
        bool finitePost = true;
        if (blind)
            for (int channel = 0; channel < channels; ++channel)
                for (std::int32_t i = 0; i < block.frames; ++i)
                    finitePost = finitePost && std::isfinite (io[channel][i]);
        report.reason = ! finitePost ? RecoveryReason::nonFinite
            : (blind || preSelected || weight > 0.0f)
                ? guardFailure (block.frames, preLevel.peak (preGain), ceilingLinear) : RecoveryReason::none;
        if (report.reason != RecoveryReason::none)
        {
            weight = 0.0f;
            report.guardTripped = true;
            post.apply (io, channels, block.frames, postTarget);
            return report;
        }
        const float target = preSelected ? 1.0f : 0.0f;
        if (weight <= 0.0f && target <= 0.0f)
        {
            const bool settled = post.value() >= postTarget && post.value() <= postTarget;
            post.apply (io, channels, block.frames, postTarget);
            report.stableSource = settled;
            report.gainSettled = settled;
            report.audibleSource = settled ? RenderReport::AudibleSource::post
                                           : RenderReport::AudibleSource::none;
            return report;
        }
        // Once both ramps are already settled, bypass the crossfade arithmetic entirely. The
        // Blind receipt therefore describes the exact path that produced this whole block, not
        // an inference from ramp state after a multiply/add loop. This is also the cheap steady
        // state: one PRE gain multiply, or the bit-identical POST fast path above.
        if (weight >= 1.0f && target >= 1.0f && preLevel.settledAt (preGain)
            && post.value() >= postTarget && post.value() <= postTarget)
        {
            for (int channel = 0; channel < channels; ++channel)
                for (std::int32_t i = 0; i < block.frames; ++i)
                    io[channel][i] = scratch[std::min (channel, 1)][i] * preGain;
            report.preAudible = true;
            report.stableSource = true;
            report.gainSettled = true;
            report.audibleSource = RenderReport::AudibleSource::pre;
            return report;
        }
        const float step = 1.0f / static_cast<float> (fadeFrames);
        bool gainSettled = true;
        for (std::int32_t i = 0; i < block.frames; ++i)
        {
            weight = weight < target ? std::min (target, weight + step) : std::max (target, weight - step);
            const float postGain = post.next (postTarget) * (1.0f - weight);
            const float gain = preLevel.next (preGain);
            gainSettled = gainSettled && gain >= preGain && gain <= preGain
                && post.value() >= postTarget && post.value() <= postTarget;
            for (int channel = 0; channel < channels; ++channel)
            {
                const float pre = scratch[std::min (channel, 1)][i] * gain;
                io[channel][i] = io[channel][i] * postGain + pre * weight;
            }
        }
        // Even if a ramp reaches its target during this block, its samples came through the
        // transition equation. The next direct-path block earns the audible receipt.
        report.stableSource = false;
        report.gainSettled = gainSettled;
        report.preAudible = weight > 0.0f;
        return report;
    }

    // Audio Thread: the host stopped processing the session (end, format change).
    void silenceTransition() noexcept { weight = 0.0f; }
    bool hasPre() const noexcept { return weight > 0.0f; } // Audio Thread only

private:
    static constexpr double fadeSeconds = 0.005; // the symmetric transition of Local Blind (INV-S25)
    static constexpr double historySeconds = 8.0; // MATCH reads up to 4 s of it

    // INV-LC14: a PRE block is never output when a sample is not finite or, raised by the approved
    // gain, peaks above the ceiling fixed at MATCH. Sample peaks miss inter-sample peaks, so this
    // guard alone does not prove the true peak.
    RecoveryReason guardFailure (std::int32_t frames, float preGain, float ceilingLinear) const noexcept
    {
        const float limit = preGain > 1.0f ? ceilingLinear / preGain : std::numeric_limits<float>::infinity();
        for (const float* channel : { left.get(), right.get() })
            for (std::int32_t i = 0; i < frames; ++i)
            {
                if (! std::isfinite (channel[i])) return RecoveryReason::nonFinite;
                if (std::fabs (channel[i]) > limit) return RecoveryReason::ceiling;
            }
        return RecoveryReason::none;
    }

    // Audio Thread: the POST input (A, before any output mixing) indexed by POST's continuous clock.
    void recordHistory (const BlockClock& block, float* const* io, int channels, const Decision& decision) noexcept
    {
        if (history == nullptr || ! block.clockValid)
        {
            invalidateHistory();
            return;
        }
        historySequence.fetch_add (1);
        const auto end = historyEnd.load (std::memory_order_relaxed);
        const bool proven = decision.verdict == Verdict::accepted;
        const bool restarted = block.afterGap || ! block.playing || block.clock != end || end == 0
            || ! proven || ! matchOffsetValid.load (std::memory_order_relaxed)
            || decision.k != matchOffset.load (std::memory_order_relaxed)
            || decision.run != historyPreRun.load();
        if (restarted)
        {
            historyStart.store (block.clock, std::memory_order_release);
            historyProof.fetch_add (1);
        }
        historyPreRun.store (decision.run);
        matchOffset.store (decision.k, std::memory_order_release);
        matchOffsetValid.store (proven, std::memory_order_release);
        const auto offset = block.project - block.clock;
        if (restarted || ! block.projectValid || ! projectKnown.load (std::memory_order_relaxed)
            || offset != projectOffset.load (std::memory_order_relaxed))
        {
            projectOffset.store (offset, std::memory_order_relaxed);
            projectKnown.store (block.projectValid && block.playing, std::memory_order_relaxed);
            projectRunStart.store (block.clock, std::memory_order_release);
        }
        const auto mask = historyFrames - 1;
        for (std::int32_t i = 0; i < block.frames; ++i)
        {
            const auto slot = static_cast<std::size_t> ((block.clock + i) & mask) * 2;
            history[slot].store (io[0][i], std::memory_order_relaxed);
            history[slot + 1].store (io[std::min (1, channels - 1)][i], std::memory_order_relaxed);
        }
        historyEnd.store (block.clock + block.frames, std::memory_order_release);
        historySequence.fetch_add (1);
    }

    void invalidateHistory() noexcept
    {
        historySequence.fetch_add (1);
        matchOffsetValid.store (false, std::memory_order_release);
        historyProof.fetch_add (1);
        historySequence.fetch_add (1);
    }

    Consumer consumer;
    GainRamp preLevel;
    std::unique_ptr<float[]> left, right;
    int capacity = 0;
    int fadeFrames = 240;
    float weight = 0.0f;
    std::unique_ptr<std::atomic<float>[]> history;
    std::int64_t historyFrames = 0;
    std::atomic<std::int64_t> historyStart { 0 }, historyEnd { 0 }, matchOffset { 0 };
    std::atomic<std::int64_t> projectRunStart { 0 }, projectOffset { 0 };
    std::atomic<bool> projectKnown { false };
    std::atomic<bool> matchOffsetValid { false };
    std::atomic<std::uint64_t> historySequence { 0 }, historyProof { 0 }, historyPreRun { 0 };
};
}
