#include "PluginProcessor.h"
#include "live_compare/LiveCompareIdle.h"
#include "live_compare/LiveCompareInterruption.h"
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
    const auto preparationCommand = liveCompare.blindTiming.command(); // fixed at callback opening
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
    const bool compensationChanged = compensationOff != liveCompare.compensationWasOff;
    if (compensationChanged)
    {
        block.afterGap = true;
        liveCompare.compensationWasOff = compensationOff;
    }
    const auto timing = liveCompare.preparation.observe (block,
        static_cast<std::uint32_t> (preparedFormat.sampleRate), liveCompare.authority.ticket(),
        usable && ! outputTaken && ! compensationOff && ! contentHeld && ! liveCompare.authority.restoring());
    const auto finishToken = liveCompare.completion.command();
    const bool finishing = liveCompare.completion.pending();
    const auto blindCommand = liveCompare.blind.command();
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
    // Normal unity POST has no audition work. Preparation above remains continuous, including
    // the first LOOP; discontinuities still revoke MATCH before this branch. Never bypass a
    // published fade/ramp lease, a pending END, Blind or held/non-unity POST level.
    if (hypha::live_compare::unchangedUnityPostOnly (liveCompare, liveCompare.ring.hasPublishedRealtime(),
            finishing, blindCommand.active(), liveCompare.postLevel.value()))
    {
        liveCompare.preAudible.store (false, std::memory_order_release);
        liveCompare.preWaiting.store (false, std::memory_order_release);
        liveCompare.postActual.store (1.0f, std::memory_order_release);
        return;
    }
    // The idle subset grants no comparison permission; output always uses a new full snapshot.
    const auto gains = hypha::live_compare::readGainSnapshot (liveCompare);
    float postTarget = gains.post;
    const bool permitted = liveCompare.authority.permitted();
    const hypha::live_compare::Interruption interruption {
        permitted ? Reason::none : (liveCompare.pairRevocationGeneration.load (std::memory_order_acquire)
            == liveCompare.authority.generation() ? Reason::pairChanged : Reason::restored),
        bypassed, nonRealtimeMode, usable, outputTaken, block.playing, block.projectValid,
        block.clockValid, callbackGap, compensationOff, contentHeld };
    const auto selection = liveCompare.selection.command();
    const auto revision = gains.revision;
    const float gain = gains.pre, ceiling = gains.ceiling;
    const bool coherent = gains.coherent;
    const bool blindRejected = blindCommand.active()
        && (! permitted || ! liveCompare.blind.valid (blindCommand) || discontinuity || ! coherent
            || liveCompare.blindGainRevision.load (std::memory_order_acquire) != revision
            || contentHeld || compensationOff || ! usable || outputTaken
            || ! liveCompare.matched.load (std::memory_order_acquire));
    if (blindRejected)
    {
        const auto reason = interruption.reason (Reason::gainChanged);
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
            liveCompare.renderer.revokeTiming();
            liveCompare.reentry.lost (hypha::live_compare::TimelineBreak::unknown, false);
            const auto loss = bypassed ? Reason::bypassed : nonRealtimeMode ? Reason::offline
                : outputTaken ? Reason::outputTaken : Reason::formatChanged;
            liveCompare.blindTiming.observe (preparationCommand, false, interruption.reason (loss),
                liveCompare.blindTimingReceipt.load (std::memory_order_acquire));
            liveCompare.selection.fail (selection, loss);
            liveCompare.preAudible.store (false, std::memory_order_release);
            liveCompare.preWaiting.store (false, std::memory_order_release);
            return;
        }
        rendered = true;
        const bool namedEligible = permitted && liveCompare.sessionActive.load (std::memory_order_acquire)
            && ! finishing && ! blindCommand.active() && ! contentHeld && ! compensationOff
            && ! liveCompare.blindTiming.failedAndOpen();
        const auto freshBlind = liveCompare.blindTimingRequest.load (std::memory_order_acquire);
        if (freshBlind != liveCompare.blindTimingSeen)
        {
            if (namedEligible && liveCompare.blindTimingAuthority.load (std::memory_order_acquire)
                == liveCompare.authority.ticket())
            {
                liveCompare.blindTimingSeen = freshBlind;
                liveCompare.renderer.renewNamedTiming();
                liveCompare.reentry.explicitlyRenew();
                liveCompare.preparation.initialRequested.store (true, std::memory_order_release);
                liveCompare.sessionGeneration.fetch_add (1, std::memory_order_acq_rel);
                liveCompare.matched.store (false, std::memory_order_release);
                liveCompare.blindTimingReceipt.store (freshBlind, std::memory_order_release);
            }
            else if (! permitted || ! liveCompare.sessionActive.load (std::memory_order_acquire)
                || finishing || blindCommand.active()) liveCompare.blindTimingSeen = freshBlind;
        }
        if (compensationChanged)
        {
            // The explicit host notification is known, unlike an unexplained callback hole.
            // OFF holds the selection; ON discards every old timing/measurement origin and
            // permits a new named admission. It grants neither PCM nor Blind permission.
            liveCompare.renderer.revokeTiming();
            liveCompare.reentry.lost (hypha::live_compare::TimelineBreak::compensationChanged,
                namedEligible && ! callbackGap && block.playing && block.clockValid && block.projectValid);
        }
        if (liveCompare.reentry.request (selection, namedEligible, liveCompare.renderer.needsNewAdmission()))
        {
            liveCompare.renderer.renewNamedTiming();
            liveCompare.preparation.initialRequested.store (true, std::memory_order_release);
        }
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
        liveCompare.blindTiming.observe (preparationCommand,
            report.verdict == hypha::live_compare::Verdict::accepted,
            report.guardTripped || (report.timelineChanged && report.loss != hypha::live_compare::TimelineBreak::metadataPending)
                ? interruption.reason (report.reason) : interruption.reason(),
            liveCompare.blindTimingReceipt.load (std::memory_order_acquire));
        if (report.timelineChanged && ! discontinuity)
        {
            liveCompare.timelineGeneration.fetch_add (1, std::memory_order_acq_rel);
            liveCompare.sessionGeneration.fetch_add (1, std::memory_order_acq_rel);
            liveCompare.matched.store (false, std::memory_order_release);
        }
        if (report.timelineChanged)
        {
            liveCompare.reentry.lost (report.loss, namedEligible);
            if (namedEligible && report.loss != hypha::live_compare::TimelineBreak::metadataPending
                && ! hypha::live_compare::namedTransportRestart (report.loss))
                liveCompare.selection.fail (selection, report.reason);
        }
        const bool renewed = report.verdict == hypha::live_compare::Verdict::accepted
            && liveCompare.reentry.accepted (coherent
                && liveCompare.gainRevision.load (std::memory_order_acquire) == revision);
        if (renewed && namedEligible && gains.retained && liveCompare.matchRetained.load (std::memory_order_acquire)
            && gains.identity.same (hypha::live_compare::gainIdentity (*ring, liveCompare.authority.ticket()))
            && liveCompare.authority.permitted() && liveCompare.sessionActive.load (std::memory_order_acquire)
            && ! liveCompare.completion.pending() && ! liveCompare.blind.command().active()
            && liveCompare.gainRevision.load (std::memory_order_acquire) == revision)
        {
            liveCompare.matchRun.store (liveCompare.playbackRun.load (std::memory_order_acquire), std::memory_order_release);
            liveCompare.matchGeneration.store (liveCompare.sessionGeneration.load (std::memory_order_acquire), std::memory_order_release);
            liveCompare.matched.store (true, std::memory_order_release);
        }
        liveCompare.timingReentryPending.store (liveCompare.reentry.waiting(), std::memory_order_release);
        const bool gainCurrent = coherent && liveCompare.gainRevision.load (std::memory_order_acquire) == revision;
        preRemaining = liveCompare.renderer.hasPre();
        if (blindCommand.active() && ! blindRejected)
        {
            if (! gainCurrent || liveCompare.blindGainRevision.load (std::memory_order_acquire) != revision)
                liveCompare.blind.invalidate (blindCommand, Reason::gainChanged);
            else if (report.verdict != hypha::live_compare::Verdict::accepted || report.guardTripped)
            {
                liveCompare.blind.invalidate (blindCommand, report.reason);
            }
            else
            {
                using Source = hypha::live_compare::RenderReport::AudibleSource;
                const auto requested = blindCommand.pre() ? Source::pre : Source::post;
                liveCompare.blind.observe (blindCommand, report.stableSource && report.gainSettled
                    && report.audibleSource == requested);
            }
        }
        if (gainCurrent && report.gainSettled && ! report.guardTripped)
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
        if (postTarget == 1.0f)
        {
            hypha::live_compare::GainUpdate update (liveCompare.gainRevision);
            if (update) liveCompare.postTarget.store (1.0f, std::memory_order_release);
        }
        liveCompare.completion.observe (finishToken, true, preRemaining,
                                       liveCompare.postLevel.value());
    }
}
