#include "LocalBlindTrial.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>

namespace hypha::local_blind
{
LocalBlindTrial::LocalBlindTrial (TrialFormat f, TrialGain g, std::vector<float> post,
                                std::vector<float> pre, bool firstIsPre, std::size_t budget)
    : format (f), gain (g), frozenPost (std::move (post)), frozenPre (std::move (pre)), oneIsPre (firstIsPre)
{
    const bool layout = f.epochs.valid() && f.sampleRate >= 8000 && f.sampleRate <= 768000
        && (f.channels == 1 || f.channels == 2) && f.frames > 0
        && f.start <= std::numeric_limits<std::int64_t>::max() - f.frames
        && f.transitionFrames <= static_cast<std::uint64_t> (f.frames) / 2
        && f.minimumHeardFrames > 0 && f.minimumHeardFrames <= static_cast<std::uint64_t> (f.frames);
    if (! layout || frozenPost.size() != frozenPre.size()
        || frozenPost.size() / static_cast<std::size_t> (f.channels) != static_cast<std::uint64_t> (f.frames)
        || frozenPost.size() % static_cast<std::size_t> (f.channels) != 0 || frozenPost.size() > budget / sizeof (float) / 2
        || ! std::isfinite (g.fixedPre) || g.fixedPre <= 0
        || ! std::isfinite (g.lowerPost) || g.lowerPost <= 0 || g.lowerPost > 1
        || (g.requiresLowerPostApproval ? (g.fixedPre <= 1 || g.lowerPost >= 1) : g.lowerPost < 1)
        || (g.requiresLowerPostApproval && std::abs (g.fixedPre * g.lowerPost - 1.0f) > 1.0e-5f)
        || std::any_of (frozenPost.begin(), frozenPost.end(), [] (float v) { return ! std::isfinite (v); })
        || std::any_of (frozenPre.begin(), frozenPre.end(), [g] (float v)
            { return ! std::isfinite (v) || ! std::isfinite (v * g.fixedPre); }))
        throw std::invalid_argument ("Invalid immutable local Blind trial");
}

bool LocalBlindTrial::issue (Command next) noexcept
{
    const auto current = command.load (std::memory_order_relaxed);
    if (current > std::numeric_limits<std::uint64_t>::max() - 16) return false;
    command.store (((current >> 3) + 1) * 8 + next, std::memory_order_release);
    return true;
}

bool LocalBlindTrial::start (bool approve) noexcept
{
    if (command.load (std::memory_order_acquire) != ready
        || (gain.requiresLowerPostApproval && ! approve)) return false;
    lowerApproved.store (gain.requiresLowerPostApproval && approve, std::memory_order_relaxed);
    return issue (one);
}

bool LocalBlindTrial::select (int stimulus) noexcept
{
    const auto state = kind (command.load (std::memory_order_acquire));
    if ((state != one && state != two) || failed.load (std::memory_order_acquire) != TrialFailure::none
        || revealed.load (std::memory_order_acquire)
        || (stimulus != 1 && stimulus != 2)) return false;
    return issue (stimulus == 1 ? one : two);
}

bool LocalBlindTrial::answer (TrialAnswer value) noexcept
{
    if (value < TrialAnswer::one || value > TrialAnswer::cannotDistinguish || ! view().canAnswer) return false;
    answered.store (value, std::memory_order_release);
    return true;
}

bool LocalBlindTrial::reveal() noexcept
{
    if (answered.load (std::memory_order_acquire) == TrialAnswer::none || ! view().canAnswer) return false;
    revealed.store (true, std::memory_order_release);
    return true;
}

void LocalBlindTrial::stop() noexcept
{
    if (kind (command.load (std::memory_order_acquire)) != normalRequested) issue (stopRequested);
}

void LocalBlindTrial::requestNormalReturn() noexcept
{
    // An explicit second action; invalidation/close/timeout must never call this automatically.
    if (view().phase == TrialPhase::returnPending) issue (normalRequested);
}

void LocalBlindTrial::invalidate (TrialFailure reason) noexcept
{
    auto none = TrialFailure::none;
    failed.compare_exchange_strong (none, reason, std::memory_order_release, std::memory_order_relaxed);
}

bool LocalBlindTrial::inputLayout (float* const* data, int channels, int frames) const noexcept
{
    if (data == nullptr || (channels != 1 && channels != 2) || frames < 1) return false;
    for (int c = 0; c < channels; ++c) if (data[c] == nullptr) return false;
    return true;
}

TrialOutput LocalBlindTrial::hold (float* const* data, int channels, int frames, const TrialBlock& block) const noexcept
{
    // Offline/bypass is never modified. The pending attenuation survives as state for realtime return.
    if (! lowerApplied.load (std::memory_order_acquire) || ! block.realtime || block.bypassed
        || ! inputLayout (data, channels, frames)) return TrialOutput::untouched;
    for (int c = 0; c < channels; ++c)
        for (int f = 0; f < frames; ++f) data[c][f] *= gain.lowerPost;
    return TrialOutput::heldAttenuation;
}

TrialOutput LocalBlindTrial::render (float* const* data, int channels, int frames, const TrialBlock& block) noexcept
{
    const auto requested = command.load (std::memory_order_acquire);
    const auto mode = kind (requested);
    if (mode == ready) return TrialOutput::untouched;
    if (mode == normalRequested)
    {
        // Only a real, nonempty callback acknowledges the explicit normal-return command.
        if (block.realtime && ! block.bypassed && inputLayout (data, channels, frames))
            returnReceipt.store (requested, std::memory_order_release);
        return TrialOutput::untouched;
    }
    if (mode == stopRequested || failed.load (std::memory_order_acquire) != TrialFailure::none)
        return hold (data, channels, frames, block);
    if (! inputLayout (data, channels, frames) || channels != format.channels || block.sampleRate != format.sampleRate)
        invalidate (TrialFailure::format);
    else if (! block.realtime || block.bypassed)
        invalidate (TrialFailure::transport);
    else if (! (block.epochs == format.epochs))
        invalidate (TrialFailure::epochs);
    else if (block.playing && ! (block.clock == format.clock))
        invalidate (TrialFailure::clock);
    else if (block.playing && (! block.positionValid
             || block.position > std::numeric_limits<std::int64_t>::max() - frames))
        invalidate (TrialFailure::transport);
    if (failed.load (std::memory_order_acquire) != TrialFailure::none)
        return hold (data, channels, frames, block);

    const auto rangeEnd = format.start + format.frames;
    bool finishedRange = rangeComplete.load (std::memory_order_relaxed);
    const bool exactWrap = finishedRange && exactWrapEligible && format.exactLoopAllowed
        && block.exactLoopRangeValid && block.playing && block.position == format.start;
    // A completed pass stays available for an answer. Only an explicit source command
    // or an immediately observed exact host loop may start another pass of the frozen range.
    if ((requested != renderedCommand && finishedRange) || exactWrap)
    {
        hasPrevious = false;
        passActive = false;
        transition.beginPass();
        finishedRange = false;
        rangeComplete.store (false, std::memory_order_release);
        pendingRange.store (true, std::memory_order_release);
    }
    renderedCommand = requested;
    if (! block.playing)
    {
        // Stopped hosts may omit their playback clock. A later playing callback must still
        // match the frozen clock before copying; an incomplete pass cannot survive a stop.
        exactWrapEligible = false;
        if (hasPrevious && ! finishedRange) invalidate (TrialFailure::transport);
        return hold (data, channels, frames, block);
    }
    if (finishedRange)
    {
        exactWrapEligible = false;
        return hold (data, channels, frames, block);
    }
    if (hasPrevious && block.position != previousEnd)
    {
        invalidate (TrialFailure::discontinuity);
        return hold (data, channels, frames, block);
    }
    const auto blockEnd = block.position + frames;
    if (! hasPrevious && (block.position > format.start || blockEnd <= format.start))
        return hold (data, channels, frames, block); // Wait for a full pass; never enter halfway.

    const auto copyStart = std::max (block.position, format.start);
    const auto copyEnd = std::min (blockEnd, rangeEnd);
    const auto first = static_cast<int> (copyStart - block.position);
    const auto count = static_cast<int> (copyEnd - copyStart);
    if (count <= 0)
    {
        invalidate (TrialFailure::range);
        return hold (data, channels, frames, block);
    }
    const bool pre = (mode == one) == oneIsPre;
    const bool lower = lowerApproved.load (std::memory_order_relaxed);
    const bool alreadyLowered = lowerApplied.load (std::memory_order_acquire);
    if (! hasPrevious) entryFromHeld = alreadyLowered;
    if (alreadyLowered)
        for (int c = 0; c < channels; ++c)
            for (int f = 0; f < first; ++f) data[c][f] *= gain.lowerPost;
    if (lower) lowerApplied.store (true, std::memory_order_release);
    const float preGain = lower ? 1.0f : gain.fixedPre;
    const float postGain = lower ? gain.lowerPost : 1.0f;
    const auto offset = static_cast<std::size_t> (copyStart - format.start);
    for (int f = 0; f < count; ++f)
    {
        const auto nativeOffset = offset + static_cast<std::size_t> (f);
        const auto sourceWeight = transition.nextSourceWeight (pre, format.transitionFrames);
        const auto edge = LocalBlindTransition::edgeWeight (
            static_cast<std::int64_t> (nativeOffset), format.frames, format.transitionFrames);
        for (int c = 0; c < channels; ++c)
        {
            const auto index = nativeOffset * static_cast<std::size_t> (channels)
                + static_cast<std::size_t> (c);
            const float copy = sourceWeight <= 0.0f ? frozenPost[index] * postGain
                : sourceWeight >= 1.0f ? frozenPre[index] * preGain
                : LocalBlindTransition::blend (frozenPost[index] * postGain,
                                                frozenPre[index] * preGain, sourceWeight);
            float live = data[c][first + f];
            // The first entry fades from the unattenuated live input. At the far edge, its
            // approved held level is already the return target, including one-block trials.
            if (lower && (entryFromHeld || nativeOffset >= format.transitionFrames))
                live *= gain.lowerPost;
            data[c][first + f] = LocalBlindTransition::blend (live, copy, edge);
        }
    }
    if (lower)
        for (int c = 0; c < channels; ++c)
            for (int f = first + count; f < frames; ++f) data[c][f] *= gain.lowerPost;
    if (! passActive || passStimulus != mode || exactWrap)
    {
        passStimulus = mode;
        passFrames = 0;
        passStart = copyStart;
        passActive = true;
    }
    passFrames += static_cast<std::uint64_t> (count);
    const bool completePass = passFrames >= format.minimumHeardFrames
        && (format.minimumHeardFrames < static_cast<std::uint64_t> (format.frames)
            || passStart == format.start);
    if (completePass)
        (mode == one ? heardOne : heardTwo).store (passFrames, std::memory_order_release);
    hasPrevious = true;
    previousEnd = blockEnd;
    exactWrapEligible = blockEnd == rangeEnd;
    rangeComplete.store (copyEnd == rangeEnd, std::memory_order_release);
    pendingRange.store (false, std::memory_order_release);
    // The entire command (sequence + stimulus) was sampled before copying. A later request is
    // never falsely acknowledged as the one that this callback rendered.
    receipt.store (requested, std::memory_order_release);
    return TrialOutput::copy;
}

TrialView LocalBlindTrial::view() const noexcept
{
    TrialView result;
    const auto current = command.load (std::memory_order_acquire);
    const auto mode = kind (current);
    result.failure = failed.load (std::memory_order_acquire);
    result.lowerPostApprovalRequired = mode == ready && gain.requiresLowerPostApproval;
    if (mode == ready) return result;
    if (mode == stopRequested || mode == normalRequested || result.failure != TrialFailure::none)
    {
        result.phase = normalReturnConfirmed() ? TrialPhase::returned : TrialPhase::returnPending;
        return result;
    }
    const auto confirmed = receipt.load (std::memory_order_acquire);
    result.passComplete = rangeComplete.load (std::memory_order_acquire);
    result.heardOneComplete = heardOne.load (std::memory_order_acquire) >= format.minimumHeardFrames;
    result.heardTwoComplete = heardTwo.load (std::memory_order_acquire) >= format.minimumHeardFrames;
    result.activeStimulus = confirmed == 0 ? 0 : static_cast<int> (kind (confirmed));
    result.pendingStimulus = current == confirmed ? 0 : static_cast<int> (mode);
    const bool isRevealed = revealed.load (std::memory_order_acquire);
    result.canAnswer = ! isRevealed && current == confirmed
        && result.heardOneComplete && result.heardTwoComplete;
    result.answer = answered.load (std::memory_order_acquire);
    result.phase = isRevealed ? TrialPhase::revealed
                             : (pendingRange.load (std::memory_order_acquire)
                                    || (result.passComplete && current != confirmed)
                                ? TrialPhase::armed : TrialPhase::listening);
    if (result.phase == TrialPhase::revealed) result.revealedOneSide = oneIsPre ? 1 : 0;
    return result;
}

bool LocalBlindTrial::normalReturnConfirmed() const noexcept
{
    const auto current = command.load (std::memory_order_acquire);
    return kind (current) == normalRequested && returnReceipt.load (std::memory_order_acquire) == current;
}

std::size_t LocalBlindTrial::pcmBytes() const noexcept { return (frozenPost.size() + frozenPre.size()) * sizeof (float); }
}
