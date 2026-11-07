#pragma once

#include "LiveCompareCorrespondence.h"
#include "LiveCompareRecovery.h"
#include "LiveCompareGuardRecovery.h"
#include "LiveCompareGainRamp.h"
#include "CandidateTransition.h"

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
        if (ring.header.demand.load (std::memory_order_acquire) == 0
            && ring.header.candidate.readers.load (std::memory_order_acquire) == 0)
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
        if (reapplyFromUnity) { if (target < 1.0f) current = 1.0f; reapplyFromUnity = false; }
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

    void bypass() noexcept { reapplyFromUnity = true; }
    float value() const noexcept { return current; }

private:
    bool reapplyFromUnity = false; // Audio Thread only; END retains its slow upward ramp.
    float current = 1.0f; // Audio Thread only
    std::atomic<float> downStep { 1.0f / 2400.0f }, upStep { 1.0f / 24000.0f };
};

struct RenderReport
{
    enum class AudibleSource : std::uint8_t { none, post, pre };

    Verdict verdict = Verdict::noClock;
    RecoveryReason reason = RecoveryReason::none;
    bool preAudible = false;   // PRE weight above zero at the end of the block
    bool preWaiting = false;   // PRE is selected but POST sounds because the block is not proven
    bool alignWaiting = false; // both anonymous sources lack current content confirmation
    bool guardTripped = false; // PRE was not finite or, raised, peaked above the ceiling
    bool stableSource = false; // every frame used one direct, exact source path
    bool gainSettled = false;  // every frame used the command's PRE and POST gain targets
    bool timelineChanged = false;
    TimelineBreak loss = TimelineBreak::none;
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
        candidateTransition.reset();
        capacity = std::max (0, maximumFrames);
        left = std::make_unique<float[]> (static_cast<std::size_t> (capacity));
        right = std::make_unique<float[]> (static_cast<std::size_t> (capacity));
        fadeFrames = std::max (1, static_cast<int> (sampleRate * fadeSeconds + 0.5));
        preLevel.configure (sampleRate);
        guardRecovery.configure (sampleRate);
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
        alignment.readiness.newContext(); // each new session confirms its own content, retaining only the value
        weight = 0.0f;
    }

    bool adoptInitialTiming (const Ring& ring, const BlockClock& block,
                             const TimingEvidence& evidence) noexcept
    { return consumer.adoptInitialTiming (ring, block, evidence); }
    bool needsNewAdmission() const noexcept { return consumer.needsNewAdmission(); }
    void renewNamedTiming() noexcept // Audio Thread, only after NamedReentry grant
    {
        weight = 0.0f;
        consumer.reset();
        invalidateHistory(); // no old measurement/approval window survives re-entry
        projectKnown.store (false, std::memory_order_release);
    }
    void revokeTiming() noexcept
    { weight = 0.0f; consumer.revoke(); invalidateHistory(); }

    // MATCH (non-RT) reads the POST input history, contiguous over [start, end) of POST's clock,
    // and the K that mapped the latest block, then verifies the history was not overwritten.
    struct HistoryView
    {
        const std::atomic<float>* samples = nullptr; // interleaved stereo, historyFrames long
        std::int64_t frames = 0;
        std::int64_t start = 0, end = 0, k = 0;
        std::int32_t blockFrames = 0;
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
        view.blockFrames = historyBlockFrames.load (std::memory_order_relaxed);
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

    // PIN needs one contiguous project-time stretch.
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
                         float ceilingLinear, bool blind = false,
                         RecoveryReason timingBlock = RecoveryReason::none,
                         Decision* diagnosticDecision = nullptr, AlignPlayback align = AlignPlayback(),
                         std::uint64_t candidateToken = 0, bool candidateReceiver = false) noexcept
    {
        auto report = renderBlock (ring, pairKey, sampleRate, block, io, channels, preSelected,
            preGain, post, postTarget, ceilingLinear, blind, timingBlock, diagnosticDecision, align, candidateToken);
        if (candidateToken != 0 || candidateReceiver)
        {
            if (candidateTransition.apply (io, channels, block.frames, fadeFrames,
                report.verdict == Verdict::accepted && ! report.guardTripped && ! report.alignWaiting,
                report.timelineChanged, report.preAudible))
            {
                report.preAudible = report.preAudible || candidateTransition.pending();
                report.stableSource = false;
                report.audibleSource = RenderReport::AudibleSource::none;
            }
        }
        else candidateTransition.reset();
        return report;
    }

    RenderReport renderBlock (const Ring& ring, std::uint64_t pairKey, std::uint32_t sampleRate,
                         const BlockClock& block, float* const* io, int channels,
                         bool preSelected, float preGain, PostLevel& post, float postTarget,
                         float ceilingLinear, bool blind = false,
                         RecoveryReason timingBlock = RecoveryReason::none,
                         Decision* diagnosticDecision = nullptr, AlignPlayback align = AlignPlayback(),
                         std::uint64_t candidateToken = 0) noexcept
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
        align.context = alignOwner (align.context, ring);
        const auto aligned = alignment.update (align, blind, preSelected, weight);
        const bool alignFading = aligned.fading;
        const auto decision = consumer.process (ring, pairKey, sampleRate, block, scratch, 2, alignment.value());
        if (align.verify) alignment.readiness.observe (align.enabled, block.playing, decision.kValid, decision.k);
        if (decision.verdict == Verdict::accepted) alignment.publishApplied();
        if (diagnosticDecision != nullptr) *diagnosticDecision = decision;
        std::int64_t inputStart = 0, inputEnd = 0;
        const bool inputPosition = decision.kValid && checkedClockSubtract (block.clock, decision.k, inputStart)
            && checkedClockAdd (inputStart, block.frames, inputEnd);
        if (candidateToken != 0 && (decision.verdict != Verdict::accepted || !inputPosition
            || ! ring.header.candidate.covers (candidateToken, decision.run,
                inputStart, inputEnd)))
            timingBlock = RecoveryReason::notWritten;
        if (timingBlock != RecoveryReason::none) invalidateHistory();
        else if (candidateToken == 0 || ring.header.candidate.settled (candidateToken, decision.run,
            inputStart, inputEnd, sampleRate)) recordHistory (block, io, channels, decision);
        else invalidateHistory();
        if (candidateToken != 0)
        {
            const auto duration = timingBlock == RecoveryReason::none ? ring.header.candidate.contiguousFrames (
                candidateToken, decision.run, inputEnd) : 0;
            const auto view = historyView();
            const auto remaining = std::max<std::int64_t> (0, sampleRate * 4 - duration)
                + std::max<std::int64_t> (0, sampleRate * 4 - (view.kValid ? view.end - view.start : 0));
            candidateMatchSeconds.store (static_cast<int> ((remaining + sampleRate - 1) / sampleRate), std::memory_order_release);
        }
        report.timelineChanged = decision.timelineChanged || decision.invalidatedByDisagreement;
        report.loss = decision.loss;
        report.verdict = decision.verdict;
        report.reason = recoveryReason (decision.verdict);
        if (decision.loopFailure != LoopEntryFailure::none) report.reason = recoveryReason (decision.loopFailure);
        if (reaperProcessing (block) && decision.loopFailure == LoopEntryFailure::loopTooShort)
            report.reason = RecoveryReason::reaperLoopTooShort;
        if (report.timelineChanged)
            report.reason = recoveryReason (decision.loss);
        if (timingBlock != RecoveryReason::none)
        {
            report.verdict = Verdict::loopUnproven;
            report.reason = timingBlock;
        }
        if (report.verdict != Verdict::accepted)
        {
            guardRecovery.discontinuity();
            weight = 0.0f; // switch to POST at the block start
            report.preWaiting = preSelected;
            post.apply (io, channels, block.frames, postTarget);
            return report;
        }
        const bool sampleMismatch = align.enabled && alignment.value().verifySamples
            && ! matchingScaledBlock (io, scratch, channels, block.frames, alignment.value().frames == 0);
        if (align.verify && sampleMismatch) alignment.readiness.recheck();
        if (align.verify && align.enabled && (alignment.readiness.pending() || ! aligned.current
            || aligned.revision != alignment.readiness.generation() || sampleMismatch))
        {
            weight = 0.0f; // the previous address is unverified: never fade through it
            report.preWaiting = preSelected;
            report.alignWaiting = true;
            report.reason = sampleMismatch ? RecoveryReason::contentAmbiguous : alignment.readiness.late() ? RecoveryReason::preLate
                : alignment.readiness.ambiguous() ? RecoveryReason::contentAmbiguous : RecoveryReason::calibrating;
            const bool settled = post.value() >= postTarget && post.value() <= postTarget;
            post.apply (io, channels, block.frames, postTarget);
            report.stableSource = report.gainSettled = settled;
            report.audibleSource = settled ? RenderReport::AudibleSource::post : RenderReport::AudibleSource::none;
            return report;
        }
        if (align.verify && aligned.current) alignment.confirmApplied (aligned.revision);
        // Validate the raw wanted gain before peak(): std::max(current, NaN) can hide NaN.
        const bool validGain = std::isfinite (preGain) && preGain > 0.0f
            && std::isfinite (ceilingLinear) && ceilingLinear > 0.0f;
        if (validGain && weight <= 0.0f)
            preLevel.settle (preGain);
        bool finitePost = true;
        if (blind)
            for (int channel = 0; channel < channels; ++channel)
                for (std::int32_t i = 0; i < block.frames; ++i)
                    finitePost = finitePost && std::isfinite (io[channel][i]);
        report.reason = ! finitePost || ! validGain ? RecoveryReason::nonFinite
            : (blind || preSelected || weight > 0.0f)
                ? guardFailure (block.frames, preLevel.peak (preGain), ceilingLinear) : RecoveryReason::none;
        report.reason = guardRecovery.observe (report.reason, block.frames);
        if (report.reason != RecoveryReason::none)
        {
            report.preWaiting = preSelected;
            weight = 0.0f;
            report.guardTripped = true;
            post.apply (io, channels, block.frames, postTarget);
            return report;
        }
        const float target = preSelected && align.outputAllowed && ! alignFading ? 1.0f : 0.0f;
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

    // Message thread (INV-LC28): the content value for later blocks; 0 reads PRE at K alone.
    void requestAlign (AlignValue value, AlignContext context = {}, std::uint64_t measured = 0) noexcept
    { alignment.request (value, context, measured); }
    std::uint64_t alignRevision() const noexcept { return alignment.readiness.generation(); }
    std::int64_t alignClockK() const noexcept { return alignment.readiness.clockK(); }
    bool alignPending() const noexcept { return alignment.readiness.pending(); }
    void reportAlignLate (std::uint64_t measured) noexcept { alignment.readiness.late (measured); }
    void reportAlignAmbiguous (std::uint64_t measured) noexcept { alignment.readiness.ambiguous (measured); }
    AlignValue alignRequested (AlignContext context = {}) const noexcept { return alignment.requestFor (context); }
    std::int64_t alignFrames (AlignContext context) const noexcept { return alignment.displayed (context); }

    // Audio Thread: the host stopped processing the session (end, format change).
    void silenceTransition() noexcept { weight = 0.0f; }
    void bypassTransition() noexcept { candidateTransition.reset(); weight = 0.0f; }
    bool fadeToInput (float* const* io, int channels, int frames) noexcept // PRE2 outside render()
    { candidateTransition.apply (io, channels, frames, fadeFrames, false, true, false); weight = 0.0f; return candidateTransition.pending(); }
    int matchSecondsRemaining() const noexcept { return candidateMatchSeconds.load (std::memory_order_acquire); }
    bool hasPre() const noexcept { return weight > 0.0f; } // Audio Thread only

private:
    std::atomic<int> candidateMatchSeconds { 8 };
    GuardRecovery guardRecovery;
    CandidateTransition candidateTransition;
    static constexpr double fadeSeconds = 0.005; // the symmetric transition of Local Blind (INV-S25)
    static constexpr double historySeconds = 8.0; // MATCH reads up to 4 s of it

    // INV-LC14: a PRE block is never output when a sample is not finite or, raised by the approved
    // gain, peaks above the ceiling fixed at MATCH. Sample peaks miss inter-sample peaks, so this
    // guard alone does not prove the true peak.
    RecoveryReason guardFailure (std::int32_t frames, float preGain, float ceilingLinear) const noexcept
    {
        if (! std::isfinite (preGain) || preGain <= 0.0f) return RecoveryReason::nonFinite;
        float limit = std::numeric_limits<float>::infinity();
        if (preGain > 1.0f)
        {
            const double exact = static_cast<double> (ceilingLinear) / static_cast<double> (preGain);
            limit = static_cast<float> (exact);
            // A rounded-up threshold can admit a sample whose gained value exceeds even FLT_MAX.
            if (static_cast<double> (limit) > exact) limit = std::nextafter (limit, 0.0f);
        }
        for (const float* channel : { left.get(), right.get() })
            for (std::int32_t i = 0; i < frames; ++i)
            {
                if (! std::isfinite (channel[i])) return RecoveryReason::nonFinite;
                if (std::fabs (channel[i]) > limit) return RecoveryReason::ceiling;
            }
        return RecoveryReason::none;
    }

#include "LiveCompareRendererHistory.h"

    Consumer consumer;
    GainRamp preLevel;
    std::unique_ptr<float[]> left, right;
    int capacity = 0;
    int fadeFrames = 240;
    float weight = 0.0f;
    std::unique_ptr<std::atomic<float>[]> history;
    std::int64_t historyFrames = 0;
    std::atomic<std::int32_t> historyBlockFrames { 0 };
    std::atomic<std::int64_t> historyStart { 0 }, historyEnd { 0 }, matchOffset { 0 };
    std::atomic<std::int64_t> projectRunStart { 0 }, projectOffset { 0 };
    std::atomic<bool> projectKnown { false };
    std::atomic<bool> matchOffsetValid { false };
    RendererAlignment alignment;
    std::atomic<std::uint64_t> historySequence { 0 }, historyProof { 0 }, historyPreRun { 0 };
};
}
