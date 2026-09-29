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
    const int frames = buffer.getNumSamples();
    const auto continuous = liveCompare.clock.next (clock.auxiliary, frames);
    hypha::live_compare::BlockClock block;
    block.clock = continuous.samples;
    block.clockValid = continuous.valid;
    block.project = clock.positionSamples;
    block.projectValid = clock.hasPosition;
    block.playing = clock.playing;
    block.frames = frames;
    block.afterGap = liveCompare.gaps.observe (steadyNanos(), frames, preparedFormat.sampleRate);
    const int channels = buffer.getNumChannels();
    const bool usable = ! bypassed && ! nonRealtimeMode && channels > 0 && channels <= 2;

    if (role == Role::Pre)
    {
        liveCompare.ring.withRealtime ([&] (SharedRingMapping& mapping)
        {
            if (auto* ring = mapping.ring(); ring != nullptr && usable)
                liveCompare.feeder.feed (*ring, block, buffer.getArrayOfReadPointers(), channels);
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
    if (compensationOff != liveCompare.compensationWasOff)
    {
        block.afterGap = true;
        liveCompare.compensationWasOff = compensationOff;
    }
    const auto finishToken = liveCompare.completion.command();
    const bool finishing = liveCompare.completion.pending();
    const auto blindCommand = liveCompare.blind.command();
    const auto revision = liveCompare.gainRevision.load (std::memory_order_acquire);
    const float gain = liveCompare.gain.load (std::memory_order_acquire);
    const float ceiling = liveCompare.ceilingLinear.load (std::memory_order_acquire);
    float postTarget = liveCompare.postTarget.load (std::memory_order_acquire);
    const bool coherent = (revision & 1u) == 0
        && revision == liveCompare.gainRevision.load (std::memory_order_acquire);
    const bool discontinuity = ! block.playing || ! block.projectValid || block.afterGap
        || (liveCompare.previousProjectValid && block.project != liveCompare.previousProjectEnd);
    liveCompare.previousProjectEnd = block.project + frames;
    liveCompare.previousProjectValid = block.projectValid && block.playing;
    if (discontinuity) block.afterGap = true; // MATCH never joins history across stop/seek/loop.
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
        && (! liveCompare.blind.valid (blindCommand) || discontinuity || ! coherent
            || contentHeld || compensationOff || ! usable || outputTaken
            || ! liveCompare.matched.load (std::memory_order_acquire));
    if (blindRejected)
    {
        liveCompare.blind.invalidate (blindCommand);
        liveCompare.preSelected.store (false, std::memory_order_release);
    }
    const bool preSelected = ! finishing && coherent && ! blindRejected
        && (blindCommand.active() ? blindCommand.pre()
                                  : liveCompare.preSelected.load (std::memory_order_acquire));
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
        if (blindRejected || ! coherent) liveCompare.renderer.silenceTransition();
        if (finishing && coherent && liveCompare.renderer.hasPre()) postTarget = heldTarget;
        if (! usable || outputTaken)
        {
            // Offline render, bypass, a layout change or another audition keeps POST; PRE needs
            // selecting again.
            liveCompare.renderer.silenceTransition();
            if (preSelected)
            {
                liveCompare.preSelected.store (false, std::memory_order_release);
                liveCompare.interrupted.store (true, std::memory_order_release);
            }
            liveCompare.preAudible.store (false, std::memory_order_release);
            liveCompare.preWaiting.store (false, std::memory_order_release);
            return;
        }
        rendered = true;
        const auto report = liveCompare.renderer.render (*ring, mapping.key(), mapping.rate(), block,
                                                         buffer.getArrayOfWritePointers(), channels,
                                                         preSelected && ! contentHeld && ! compensationOff,
                                                         gain,
                                                         liveCompare.postLevel, postTarget,
                                                         ceiling, blindCommand.active() && ! blindRejected);
        preRemaining = liveCompare.renderer.hasPre();
        if (blindCommand.active() && ! blindRejected)
        {
            if (report.verdict != hypha::live_compare::Verdict::accepted || report.guardTripped)
                liveCompare.blind.invalidate (blindCommand);
            else
                liveCompare.blind.observe (blindCommand, report.stableSource && report.gainSettled);
        }
        if (coherent && report.gainSettled && ! report.guardTripped)
            liveCompare.gainReceipt.store (revision, std::memory_order_release);
        if (report.guardTripped)
        {
            // The guard ends PRE for this selection; the user selects it again.
            liveCompare.preSelected.store (false, std::memory_order_release);
            liveCompare.interrupted.store (true, std::memory_order_release);
            liveCompare.guardTripped.store (true, std::memory_order_release);
        }
        liveCompare.verdict.store (static_cast<std::uint8_t> (report.verdict), std::memory_order_release);
        liveCompare.preAudible.store (report.preAudible, std::memory_order_release);
        const bool preHeldBack = report.preWaiting
            || (preSelected && (contentHeld || compensationOff) && ! report.guardTripped);
        liveCompare.preWaiting.store (preHeldBack, std::memory_order_release);
        if (preHeldBack)
            liveCompare.preWaitSeen.store (true, std::memory_order_release);
    });
    // Out of a session POST keeps an approved attenuation until the explicit RETURN (INV-LC14).
    // Offline render, bypass and another audition's output are never touched.
    if (! rendered && usable && ! outputTaken)
    {
        liveCompare.postLevel.apply (buffer.getArrayOfWritePointers(), channels, frames, postTarget);
        if (blindCommand.active()) liveCompare.blind.invalidate (blindCommand);
    }
    liveCompare.postActual.store (liveCompare.postLevel.value(), std::memory_order_release);
    if (finishing && usable && ! outputTaken && frames > 0)
    {
        if (postTarget == 1.0f) liveCompare.postTarget.store (1.0f, std::memory_order_release);
        liveCompare.completion.observe (finishToken, true, preRemaining,
                                       liveCompare.postLevel.value());
    }
}
