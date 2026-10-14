#include "PluginProcessor.h"
#include "live_compare/LiveCompareIdle.h"
#include "live_compare/LiveCompareInterruption.h"

namespace
{
using hypha::live_compare::SharedRingMapping;
constexpr auto aaxWrapper = juce::AudioProcessor::wrapperType_AAX;
constexpr auto auWrapper = juce::AudioProcessor::wrapperType_AudioUnit;
constexpr auto vst3Wrapper = juce::AudioProcessor::wrapperType_VST3;
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
    const bool projectAnalysis = wrapperType == vst3Wrapper
        && liveCompare.clockAuthority == static_cast<std::uint8_t> (hypha::live_compare::ClockAuthority::certifiedProject);
    const auto continuous = liveCompare.clock.next (clock.auxiliary, frames, projectAnalysis);
    hypha::live_compare::BlockClock block;
    block.clock = continuous.samples;
    block.clockValid = continuous.valid;
    block.project = clock.positionSamples;
    // A render fallback is not an independent project witness for AU linear calibration.
    block.projectValid = clock.hasPosition && (wrapperType != auWrapper
        || clock.clockSource == KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE);
    if (projectAnalysis) block.projectValid = block.projectValid && clock.clockSource == KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE;
    block.playing = clock.playing;
    block.frames = frames;
    block.loop = clock.loop;
    // Pro Tools reports one transport position per cycle and compensates an insert's position by
    // the delay before it, so for K samples after every loop wrap POST's position lies before the
    // loop start: it is the delayed tail of the previous lap, and its audio is that tail. Name it
    // so. The wrap then happens where this position crosses the loop end, K samples after PRE's,
    // which is what the pair proof, the loop cycle and the ring anchor already expect. Only the
    // comparison's block is renamed; capture and Record keep the host's own position.
    if (wrapperType == aaxWrapper && block.projectValid && block.loop.active
        && block.loop.usable (preparedFormat.sampleRate) && block.loop.ppq < block.loop.start)
    {
        const auto length = block.loop.end - block.loop.start;
        const auto laps = std::ceil ((block.loop.start - block.loop.ppq) / length);
        const auto samples = laps * length * 60.0 * preparedFormat.sampleRate / block.loop.bpm;
        if (std::isfinite (laps) && std::isfinite (samples) && laps >= 1.0 && samples < 9007199254740992.0)
        {
            block.loop.ppq += laps * length;
            block.project += static_cast<std::int64_t> (std::llround (samples));
        }
    }
    block.clockBasis = static_cast<std::uint8_t> (continuous.basis);
    block.clockAuthority = liveCompare.clockAuthority;
    block.maximumDelaySamples = liveCompare.maximumDelaySamples;
    block.presentationSource = clock.presentationSource;
    block.outputPresentationValid = clock.outputPresentationValid;
    block.outputPresentationSamples = clock.outputPresentationSamples;
    // The callback's one wall-clock reading serves the gap rule and the chain timing display.
    // POST took it as the callback began; PRE takes it here, after its own measurement work.
    block.wallNanos = clock.callbackNanos != 0 ? clock.callbackNanos
                                               : hypha::live_compare::callbackWallNanos();
    block.thread = hypha::live_compare::callbackThread();
    // Guidance only: remember that this host can supply a continuous clock or an output-latency
    // report, so a later missing clock is told apart from a host that never has one.
    if (continuous.valid) liveCompare.clockSeen.store (true, std::memory_order_relaxed);
    if (clock.outputPresentationValid) liveCompare.outputLatencySeen.store (true, std::memory_order_relaxed);
    const bool wallGap = liveCompare.gaps.observe (block.wallNanos, frames, preparedFormat.sampleRate);
    block.wallGap = wallGap;
    block.afterGap = hypha::live_compare::callbackGapBreaksContinuity (wallGap, continuous.basis);
    if (projectAnalysis) block.afterGap = liveCompare.reaperContinuity.breaks (block, wallGap, preparedFormat.sampleRate);
    const int channels = buffer.getNumChannels();
    const bool usable = ! bypassed && ! nonRealtimeMode && channels > 0 && channels <= 2;
    const auto reset = liveCompare.hostResetGeneration.load (std::memory_order_acquire);
    const bool hostReset = reset != liveCompare.hostResetSeen;
    liveCompare.hostResetSeen = reset;
    const auto stop = liveCompare.contentHold.generation();
    const bool notifiedStop = stop != liveCompare.stopSeen;
    liveCompare.stopSeen = stop;
    if (notifiedStop) liveCompare.wasPlaying = false;
    const bool callbackGap = block.afterGap;
    if (hostReset) block.afterGap = true;
    block.transportRestart = hostReset && notifiedStop;
    if (projectAnalysis) nativeHostContext.reaper.observe (block, preparedFormat.sampleRate, usable);
    auto* trace = liveCompare.trace ? liveCompare.trace->row() : nullptr;
    if (trace != nullptr)
    {
        trace->block = block; trace->blockSeen = true;
        trace->wallGap = wallGap; trace->outputTaken = outputTaken;
    }
    if (projectAnalysis)
    {
        auto analysisBlock = block;
        // REAPER's measurement certificate remains independent of the audition's API token.
        analysisBlock.runtimeChainA = analysisBlock.runtimeChainB = analysisBlock.runtimeRevision = 0;
        analysisBlock.maximumDelaySamples = 0;
        analysisBlock.afterGap = wallGap || hostReset;
        analysisBlock.projectValid = clock.hasPosition && clock.clockSource == KIRIN_HYPHA_CLOCK_PROJECT_TIMELINE;
        const auto reading = liveCompare.projectAnalysisClock.observe (analysisBlock, preparedFormat.sampleRate, usable);
        liveCompare.analysisClock.local = liveCompare.analysisClock.paired = reading;
    }
    if (wrapperType == aaxWrapper || wrapperType == auWrapper
        || liveCompare.clockAuthority
        == static_cast<std::uint8_t> (hypha::live_compare::ClockAuthority::certifiedContent))
        liveCompare.analysisClock.observeLocal (block, preparedFormat.sampleRate, usable);

    if (role == Role::Pre)
    {
        liveCompare.ring.withRealtime ([&] (SharedRingMapping& mapping)
        {
            if (auto* ring = mapping.ring(); ring != nullptr)
            {
                auto observed = block;
                if (! usable) observed.clockValid = false;
                const auto fed = liveCompare.feeder.feed (*ring, observed, usable ? buffer.getArrayOfReadPointers() : nullptr, channels);
                if (trace != nullptr) trace->feed = fed;
            }
        });
        return;
    }

    // INV-LC10: a playback run starts at play after stop; a content-jump hold ends with its run.
    if (block.playing && ! liveCompare.wasPlaying)
        liveCompare.playbackRun.fetch_add (1, std::memory_order_acq_rel);
    if (! block.playing)
        liveCompare.contentHold.stopped();
    liveCompare.wasPlaying = block.playing;
    const bool contentHeld = liveCompare.contentHold.held();
    // INV-LC8: a change of the host's delay compensation moves POST's positions; K is proven again.
    const bool compensationOff = liveCompare.compensationOff.load (std::memory_order_acquire);
    const bool compensationChanged = compensationOff != liveCompare.compensationWasOff;
    if (compensationChanged)
    {
        block.afterGap = true;
        liveCompare.compensationWasOff = compensationOff;
    }
    const bool timingEligible = usable && ! outputTaken && ! compensationOff
        && ! liveCompare.authority.restoring();
    bool analysisQualified = false;
    const auto timing = liveCompare.preparation.observe (block,
        static_cast<std::uint32_t> (preparedFormat.sampleRate), liveCompare.authority.ticket(),
        timingEligible,
        usable ? &liveCompare.chain : nullptr, &analysisQualified, trace != nullptr ? &trace->peer : nullptr);
    if (trace != nullptr) { trace->block = block; trace->timing = timing; trace->timingSeen = true; }
    if (! projectAnalysis) liveCompare.analysisClock.observePair (block, timing, analysisQualified);
    const auto loopBlock = hypha::live_compare::pairingLoopBlock (block, wrapperType == auWrapper,
        wrapperType == aaxWrapper, timing, timingEligible);
    liveCompare.loopBlock.store (loopBlock, std::memory_order_relaxed);
    const bool cycleUnverified = loopBlock == hypha::live_compare::LoopPairingBlock::auCycle;
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
        && (! permitted || ! liveCompare.blind.valid (blindCommand)
            || (coherent && liveCompare.blindGainRevision.load (std::memory_order_acquire) != revision));
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
        if (! permitted || blindRejected || ! coherent || contentHeld) liveCompare.renderer.silenceTransition();
        if (finishing && coherent && liveCompare.renderer.hasPre()) postTarget = heldTarget;
        if (! usable || outputTaken)
        {
            // Offline render, bypass and another audition leave their input untouched.
            // The same explicit selection can renew when realtime output is available.
            liveCompare.renderer.revokeTiming();
            liveCompare.reentry.lost (hypha::live_compare::TimelineBreak::unknown,
                permitted && !finishing && liveCompare.sessionActive.load (std::memory_order_acquire));
            const auto loss = bypassed ? Reason::bypassed : nonRealtimeMode ? Reason::offline
                : outputTaken ? Reason::outputTaken : Reason::formatChanged;
            liveCompare.blindTiming.observe (preparationCommand, false, interruption.reason (loss),
                liveCompare.blindTimingReceipt.load (std::memory_order_acquire));
            if (hypha::live_compare::endsComparison (loss)) liveCompare.selection.fail (selection, loss);
            liveCompare.blind.fallback (blindCommand);
            liveCompare.preAudible.store (false, std::memory_order_release);
            liveCompare.preWaiting.store (false, std::memory_order_release);
            return;
        }
        rendered = true;
        const bool namedEligible = permitted && liveCompare.sessionActive.load (std::memory_order_acquire)
            && ! finishing && ! liveCompare.blindTiming.failedAndOpen()
            && ring->header.ownerClosed.load (std::memory_order_acquire) == 0
            && (!coherent || !gains.retained || gains.identity.same (
                hypha::live_compare::gainIdentity (*ring, liveCompare.authority.ticket())));
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
                                                   ceiling, blindCommand.active() && ! blindRejected,
                                                   cycleUnverified ? Reason::cycleUnverified : Reason::none,
                                                   trace != nullptr ? &trace->decision : nullptr);
        if (trace != nullptr)
        {
            trace->render = report; trace->renderSeen = true;
            trace->preGain = gain; trace->postGain = postTarget;
        }
        if (report.reason == Reason::loopUnproven)
            report.reason = hypha::live_compare::recoveryReason (timing.failure);
        if (projectAnalysis && report.reason == Reason::loopTooShort) report.reason = Reason::reaperLoopTooShort;
        liveCompare.blindTiming.observe (preparationCommand,
            report.verdict == hypha::live_compare::Verdict::accepted,
            cycleUnverified || report.guardTripped || (report.timelineChanged && report.loss != hypha::live_compare::TimelineBreak::metadataPending)
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
            if (report.loss == hypha::live_compare::TimelineBreak::sourceChanged)
                liveCompare.selection.fail (selection, report.reason);
        }
        const bool renewed = report.verdict == hypha::live_compare::Verdict::accepted
            && liveCompare.reentry.accepted (coherent
                && liveCompare.gainRevision.load (std::memory_order_acquire) == revision);
        if (renewed && namedEligible && gains.retained && liveCompare.matchRetained.load (std::memory_order_acquire)
            && gains.identity.same (hypha::live_compare::gainIdentity (*ring, liveCompare.authority.ticket()))
            && liveCompare.authority.permitted() && liveCompare.sessionActive.load (std::memory_order_acquire)
            && ! liveCompare.completion.pending()
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
            if (coherent && (!gainCurrent || liveCompare.blindGainRevision.load (std::memory_order_acquire) != revision))
                liveCompare.blind.invalidate (blindCommand, Reason::gainChanged);
            else if (report.verdict != hypha::live_compare::Verdict::accepted || report.guardTripped
                || contentHeld || compensationOff || !coherent)
            {
                if (hypha::live_compare::endsComparison (report.reason))
                    liveCompare.blind.invalidate (blindCommand, report.reason);
                else liveCompare.blind.fallback (blindCommand);
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
    liveCompare.waitNotice.observe (liveCompare.sessionActive.load (std::memory_order_acquire)
        && (!rendered || !usable || outputTaken || contentHeld || compensationOff || !coherent
            || liveCompare.verdict.load (std::memory_order_acquire)
                != static_cast<std::uint8_t> (hypha::live_compare::Verdict::accepted)
            || liveCompare.observationReason.load (std::memory_order_acquire) == Reason::ceiling
            || liveCompare.observationReason.load (std::memory_order_acquire) == Reason::nonFinite), block.wallNanos);
    if (! usable || outputTaken) liveCompare.postLevel.bypass();
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
