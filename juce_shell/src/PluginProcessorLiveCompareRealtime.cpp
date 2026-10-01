#include "PluginProcessor.h"
#include <chrono>

namespace
{
using hypha::live_compare::SharedRingMapping;
std::uint64_t steadyNanos() noexcept
{
    return static_cast<std::uint64_t> (std::chrono::duration_cast<std::chrono::nanoseconds> (
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
}

// Audio Thread. Both roles advance their continuous clock and gap detector on every callback so
// that the clock semantics hold whether or not a session is active.
void KirinHyphaProcessorBase::processLiveCompare (juce::AudioBuffer<float>& buffer,
                                                  const hypha::HostProcessClock& clock,
                                                  bool bypassed, bool nonRealtimeMode,
                                                  bool outputTaken) noexcept
{
    using Reason = hypha::live_compare::RecoveryReason;
    const int frames = buffer.getNumSamples();
    const auto continuous = liveCompare.clock.next (clock.auxiliary, frames);
    hypha::live_compare::BlockClock block;
    block.clock = continuous.samples;
    block.clockValid = continuous.valid;
    block.project = clock.positionSamples;
    block.projectValid = clock.hasPosition;
    block.playing = clock.playing;
    block.frames = frames;
    block.loop = clock.loop;
    block.clockBasis = static_cast<std::uint8_t> (continuous.basis);
    block.clockAuthority = liveCompare.clockAuthority;
    block.maximumDelaySamples = liveCompare.maximumDelaySamples;
    block.presentationSource = clock.presentationSource;
    block.outputPresentationValid = clock.outputPresentationValid;
    block.outputPresentationSamples = clock.outputPresentationSamples;
    const bool wallGap = liveCompare.gaps.observe (steadyNanos(), frames, preparedFormat.sampleRate);
    block.afterGap = hypha::live_compare::callbackGapBreaksContinuity (wallGap, continuous.basis);
    const int channels = buffer.getNumChannels();
    const bool usable = ! bypassed && ! nonRealtimeMode && channels > 0 && channels <= 2;

    if (role == Role::Pre)
    {
        liveCompare.ring.withRealtime ([&] (SharedRingMapping& mapping)
        {
            if (auto* ring = mapping.ring(); ring != nullptr)
            {
                auto observed = block;
                if (! usable) observed.clockValid = false;
                liveCompare.feeder.feed (*ring, observed, usable ? buffer.getArrayOfReadPointers() : nullptr, channels);
            }
        });
        return;
    }

    // INV-LC10: a playback run starts at play after stop; a content-jump hold ends with its run.
    if (block.playing && ! liveCompare.wasPlaying)
        liveCompare.playbackRun.fetch_add (1, std::memory_order_acq_rel);
    if (! block.playing)
        liveCompare.contentHold.store (false, std::memory_order_release);
    liveCompare.wasPlaying = block.playing;
    const bool contentHeld = liveCompare.contentHold.load (std::memory_order_acquire);
    // INV-LC8: a change of the host's delay compensation moves POST's positions; K is proven again.
    const bool compensationOff = liveCompare.compensationOff.load (std::memory_order_acquire);
    const bool callbackGap = block.afterGap;
    if (compensationOff != liveCompare.compensationWasOff)
    {
        block.afterGap = true;
        liveCompare.compensationWasOff = compensationOff;
    }
    const auto timing = liveCompare.preparation.observe (block,
        static_cast<std::uint32_t> (preparedFormat.sampleRate), liveCompare.authority.ticket(),
        usable && ! outputTaken && ! compensationOff && ! contentHeld && ! liveCompare.authority.restoring());
    const auto finishToken = liveCompare.completion.command();
    const bool finishing = liveCompare.completion.pending();
    const bool permitted = liveCompare.authority.permitted();
    const auto blindCommand = liveCompare.blind.command();
    const auto selection = liveCompare.selection.command();
    const auto revision = liveCompare.gainRevision.load (std::memory_order_acquire);
    const float gain = liveCompare.gain.load (std::memory_order_acquire);
    const float ceiling = liveCompare.ceilingLinear.load (std::memory_order_acquire);
    float postTarget = liveCompare.postTarget.load (std::memory_order_acquire);
    const bool coherent = (revision & 1u) == 0
        && revision == liveCompare.gainRevision.load (std::memory_order_acquire);
    const bool discontinuity = ! block.playing || ! block.projectValid || ! block.clockValid || block.afterGap;
    if (discontinuity)
    {
        liveCompare.timelineGeneration.fetch_add (1, std::memory_order_acq_rel);
        liveCompare.sessionGeneration.fetch_add (1, std::memory_order_acq_rel);
    }
    if (discontinuity || liveCompare.matchRun.load (std::memory_order_acquire)
                           != liveCompare.playbackRun.load (std::memory_order_acquire)
        || liveCompare.matchGeneration.load (std::memory_order_acquire)
                           != liveCompare.sessionGeneration.load (std::memory_order_acquire))
    {
        liveCompare.matched.store (false, std::memory_order_release);
    }
    const bool blindRejected = blindCommand.active()
        && (! permitted || ! liveCompare.blind.valid (blindCommand) || discontinuity || ! coherent
            || contentHeld || compensationOff || ! usable || outputTaken
            || ! liveCompare.matched.load (std::memory_order_acquire));
    if (blindRejected)
    {
        const auto reason = ! permitted ? Reason::restored
            : bypassed ? Reason::bypassed : nonRealtimeMode ? Reason::offline
            : ! usable ? Reason::formatChanged : outputTaken ? Reason::outputTaken
            : compensationOff ? Reason::compensationOff : contentHeld ? Reason::contentChanged
            : ! block.playing ? Reason::stopped : ! block.projectValid ? Reason::projectClockMissing
            : callbackGap ? Reason::callbackGap
            : ! block.clockValid ? Reason::clockMissing : Reason::gainChanged;
        if (! finishing) liveCompare.blind.invalidate (blindCommand, reason);
    }
    const bool preSelected = permitted && ! finishing && coherent && ! blindRejected
        && (blindCommand.active() ? blindCommand.pre()
                                  : selection.pre());
    // END first removes PRE, then returns POST. A closed/retired ring still returns normally.
    const float heldTarget = postTarget;
    if (finishing) postTarget = 1.0f;
    if (! coherent) postTarget = liveCompare.postLevel.value();
    bool rendered = false;
    bool preRemaining = false;
    liveCompare.ring.withRealtime ([&] (SharedRingMapping& mapping)
    {
        auto* ring = mapping.ring();
        if (ring == nullptr)
            return;
        // Renderer lifetime is protected by the publication slot, including END/fault reads.
        if (! permitted || blindRejected || ! coherent) liveCompare.renderer.silenceTransition();
        if (finishing && coherent && liveCompare.renderer.hasPre()) postTarget = heldTarget;
        if (! usable || outputTaken)
        {
            // Offline render, bypass, a layout change or another audition keeps POST; PRE needs
            // selecting again.
            liveCompare.renderer.silenceTransition();
            if (preSelected)
            {
                liveCompare.selection.fail (selection, bypassed ? Reason::bypassed
                    : nonRealtimeMode ? Reason::offline : outputTaken ? Reason::outputTaken : Reason::formatChanged);
            }
            liveCompare.preAudible.store (false, std::memory_order_release);
            liveCompare.preWaiting.store (false, std::memory_order_release);
            return;
        }
        rendered = true;
        if (permitted && liveCompare.preparation.initialRequested.load (std::memory_order_acquire)
            && liveCompare.renderer.adoptInitialTiming (*ring, block, timing))
            liveCompare.preparation.initialRequested.store (false, std::memory_order_release);
        auto report = liveCompare.renderer.render (*ring, mapping.key(), mapping.rate(), block,
                                                   buffer.getArrayOfWritePointers(), channels,
                                                   preSelected && ! contentHeld && ! compensationOff,
                                                   gain,
                                                   liveCompare.postLevel, postTarget,
                                                   ceiling, blindCommand.active() && ! blindRejected);
        if (report.reason == Reason::loopUnproven)
            report.reason = hypha::live_compare::recoveryReason (timing.failure);
        if (report.timelineChanged && ! discontinuity)
        {
            liveCompare.timelineGeneration.fetch_add (1, std::memory_order_acq_rel);
            liveCompare.sessionGeneration.fetch_add (1, std::memory_order_acq_rel);
            liveCompare.matched.store (false, std::memory_order_release);
        }
        preRemaining = liveCompare.renderer.hasPre();
        if (blindCommand.active() && ! blindRejected)
        {
            if (report.verdict != hypha::live_compare::Verdict::accepted || report.guardTripped)
            {
                liveCompare.blind.invalidate (blindCommand, report.reason);
            }
            else
                liveCompare.blind.observe (blindCommand, report.stableSource && report.gainSettled);
        }
        if (coherent && report.gainSettled && ! report.guardTripped)
            liveCompare.gainReceipt.store (revision, std::memory_order_release);
        if (report.guardTripped)
        {
            // The guard ends PRE for this selection; the user selects it again.
            if (! blindCommand.active()) liveCompare.selection.fail (selection, report.reason);
            liveCompare.guardTripped.store (true, std::memory_order_release);
        }
        liveCompare.verdict.store (static_cast<std::uint8_t> (report.verdict), std::memory_order_release);
        liveCompare.preAudible.store (report.preAudible, std::memory_order_release);
        const bool preHeldBack = report.preWaiting
            || (preSelected && (contentHeld || compensationOff) && ! report.guardTripped);
        liveCompare.preWaiting.store (preHeldBack, std::memory_order_release);
        liveCompare.observationReason.store (report.reason, std::memory_order_release);
        if (preHeldBack)
            liveCompare.preWaitSeen.store (true, std::memory_order_release);
    });
    // Out of a session POST keeps an approved attenuation until the explicit RETURN (INV-LC14).
    // Offline render, bypass and another audition's output are never touched.
    if (! rendered && usable && ! outputTaken)
    {
        liveCompare.postLevel.apply (buffer.getArrayOfWritePointers(), channels, frames, postTarget);
        if (blindCommand.active())
        {
            liveCompare.blind.invalidate (blindCommand, Reason::preUnavailable);
        }
    }
    liveCompare.postActual.store (liveCompare.postLevel.value(), std::memory_order_release);
    if (finishing && usable && ! outputTaken && frames > 0)
    {
        if (postTarget == 1.0f) liveCompare.postTarget.store (1.0f, std::memory_order_release);
        liveCompare.completion.observe (finishToken, true, preRemaining,
                                       liveCompare.postLevel.value());
    }
}
