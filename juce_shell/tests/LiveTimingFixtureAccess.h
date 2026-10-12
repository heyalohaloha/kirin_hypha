#pragma once
#include "../src/PluginProcessor.h"
#include "../src/HyphaLiveCompareClockPolicy.h"

// Non-shipping synthetic host. Both compilers must model the SAME measured clock policy;
// opt-in macOS tracing must not secretly supply a prerequisite missing from the Windows test.
// There is no exported hook, new product field, or run-time certificate bypass. Configuration
// happens before callbacks; reads below are bounded atomics on the fixture's owning thread.
class LiveTimingFixtureAccess
{
public:
    // qualified = a Pro Tools build (the measured 26.4.0.5 by default, or another build's version);
    // otherwise an AAX host that is not Pro Tools (no certificate, no bound). All of them feed the
    // AAX Perceptual route, as prepareToPlay does for every AAX host.
    static void configureAaxClock (KirinHyphaProcessorBase& processor, bool qualified = true,
                                   const char* version = "26.4.0.5")
    {
        const auto certificate = hypha::live_compare_clock_policy::classify (
            juce::AudioProcessor::wrapperType_AAX, "Pro Tools", version, 48000);
        processor.liveCompare.clockAuthority = qualified ? static_cast<std::uint8_t> (certificate.authority) : 0;
        processor.liveCompare.maximumDelaySamples = qualified ? certificate.maximumDelaySamples : 0;
        kirin_hypha_set_aax_analysis_ingress (processor.hyphaHandle, true,
                                              processor.role == KirinHyphaProcessorBase::Role::Post);
    }
    static void enableFixtureWrites (KirinHyphaProcessorBase& processor)
    { processor.enableWritesNow(); }
    static void serviceTiming (KirinHyphaProcessorBase& processor)
    { processor.serviceLiveCompare(); }
    static void serviceProcessorTimer (KirinHyphaProcessorBase& processor)
    { processor.timerCallback(); }
    static void retirePreparedFormatRing (KirinHyphaProcessorBase& processor)
    { processor.stopLiveCompareForFormatChange(); }
    // Exercise the real publication slot and service path without requiring OS shared memory.
    // This mapping has no PCM; it must never stand in for a successful LISTEN/BLIND test.
    static bool publishUnmappedRing (KirinHyphaProcessorBase& processor)
    {
        auto& slot = processor.liveCompare.ring;
        slot.retire();
        return slot.collect() && slot.publish (std::make_unique<hypha::live_compare::SharedRingMapping>());
    }
    static void fadePending (KirinHyphaProcessorBase& processor, bool pending)
    { processor.liveCompare.preAudible.store (pending); }
    static const hypha::live_compare::SharedRingMapping* publishedRing (const KirinHyphaProcessorBase& processor)
    { return processor.liveCompare.ring.control(); }
    static bool timingRingPublished (const KirinHyphaProcessorBase& processor) noexcept
    { return processor.liveCompare.ring.hasPublishedRealtime(); }
    static hypha::live_compare::AnalysisClockReading aaxLocalClock (const KirinHyphaProcessorBase& processor) noexcept
    { return processor.liveCompare.analysisClock.local; }
    static hypha::live_compare::AnalysisClockReading aaxPairedClock (const KirinHyphaProcessorBase& processor) noexcept
    { return processor.liveCompare.analysisClock.paired; }

    // Drive the production input/timing/analysis transaction while another audition owns output.
    static void processAaxAnalysisWithOtherOutputOwner (KirinHyphaProcessorBase& processor,
                                                       juce::AudioBuffer<float>& buffer)
    {
        const auto clock = processor.readHostProcessClock();
        const auto frames = static_cast<std::uint64_t> (buffer.getNumSamples());
        const bool admitted = processor.feedMeasurementInput (buffer, clock, 0, buffer.getNumSamples(),
            clock.positionSamples, frames, false);
        processor.processLiveCompare (buffer, clock, false, false, true);
        processor.pushPairedAnalysis (admitted ? frames : 0, static_cast<std::uint32_t> (buffer.getNumChannels()), 0);
    }

    static void timingObservation (KirinHyphaProcessorBase& processor,
        hypha::live_compare::RecoveryReason reason, std::uint8_t authority)
    {
        auto& state = processor.liveCompare;
        state.clockAuthority = authority;
        state.sessionActive.store (true);
        state.selection.select (true);
        state.preWaiting.store (true);
        state.observationReason.store (reason);
    }
    static hypha::live_compare::OffsetEstimate holdContent (KirinHyphaProcessorBase& processor, std::int64_t lag = 96)
    {
        hypha::live_compare::OffsetEstimate e;
        e.determined = true; e.sampleRate = processor.preparedFormat.sampleRate > 0
            ? processor.preparedFormat.sampleRate : 96000; e.peak = 0.4; e.dominance = 20;
        e.lagFrames = lag; e.clockK = 4096; e.blockFrames = 512;
        e.stopGeneration = processor.liveCompare.contentHold.generation();
        e.timelineGeneration = processor.liveCompare.timelineGeneration.load();
        processor.holdLiveCompareForContentJump (e);
        return e;
    }
    static void blindWaiting (KirinHyphaProcessorBase& processor, hypha::live_compare::BlindStage stage)
    { processor.liveCompare.blindStage = stage; }
    static void compensationDisabled (KirinHyphaProcessorBase& processor, bool disabled)
    { processor.liveCompare.compensationOff.store (disabled); }
    static hypha::live_compare::OffsetEstimate jumpFacts (const KirinHyphaProcessorBase& processor)
    { return processor.liveCompare.contentJumpFacts; }
    static void heldGains (KirinHyphaProcessorBase& processor)
    {
        processor.lastPlaying.store (true);
        processor.liveCompare.gain.store (0.75f);
        processor.liveCompare.postTarget.store (0.5f);
        processor.liveCompare.postActual.store (0.5f);
    }
    static bool serviceTimerRunning (const KirinHyphaProcessorBase& processor) noexcept
    { return processor.isTimerRunning(); }
    static bool serviceUpdatePending (const KirinHyphaProcessorBase& processor) noexcept
    { return processor.isUpdatePending(); }
    static bool retainsFadeSource (const KirinHyphaProcessorBase& processor) noexcept
    {
        const auto* mapping = processor.liveCompare.ring.control();
        return processor.liveCompare.ring.hasPublishedRealtime() && mapping != nullptr
            && mapping->ring() != nullptr && mapping->ring()->header.demand.load() == 1;
    }
    static bool configureStudioProClock (KirinHyphaProcessorBase& processor) noexcept
    { return configureVst3Clock (processor, "Studio Pro", "8.1.2.113407"); }
    static bool configureVst3Clock (KirinHyphaProcessorBase& processor,
                                    const char* name, const char* version) noexcept
    {
        const auto certificate = hypha::live_compare_clock_policy::classify (
            juce::AudioProcessor::wrapperType_VST3, name, version, 48000);
        if (! hypha::live_compare::certifiedVst3Analysis (certificate.authority)
            || certificate.maximumDelaySamples != 0) return false;
        processor.liveCompare.clockAuthority = static_cast<std::uint8_t> (certificate.authority);
        processor.liveCompare.maximumDelaySamples = certificate.maximumDelaySamples;
        kirin_hypha_set_content_analysis_ingress (processor.hyphaHandle,
            processor.role == KirinHyphaProcessorBase::Role::Post);
        return true;
    }

    static hypha::live_compare::BlindCommand command (const KirinHyphaProcessorBase& processor) noexcept
    { return processor.liveCompare.blind.command(); }

    static int audible (const KirinHyphaProcessorBase& processor) noexcept
    { return processor.liveCompare.blind.view().audible; }

    static bool initialObservationRequested (const KirinHyphaProcessorBase& processor) noexcept
    {
        return processor.liveCompare.ring.hasPublishedRealtime()
            && processor.liveCompare.preparation.initialRequested.load (std::memory_order_acquire);
    }
    static std::atomic<std::uint64_t>& gainRevision (KirinHyphaProcessorBase& processor) noexcept
    { return processor.liveCompare.gainRevision; }
    struct TimingAdmission { std::uint64_t request, receipt, authority; };
    static TimingAdmission timingAdmission (const KirinHyphaProcessorBase& processor) noexcept
    {
        const auto& state = processor.liveCompare;
        return { state.blindTimingRequest.load (std::memory_order_acquire),
            state.blindTimingReceipt.load (std::memory_order_acquire),
            state.blindTimingAuthority.load (std::memory_order_acquire) };
    }
    // The Blind preparation's own proof. The audio thread sets it only in a block that accepted the
    // timing without a loss; an accepted verdict alone can come from a block that also lost it.
    static bool blindPreparationProven (const KirinHyphaProcessorBase& processor) noexcept
    { return (processor.liveCompare.blindTiming.command().word & 2u) != 0; }
    struct AudioView
    {
        bool preAudible = false, matchReady = false, matched = false;
        bool active = false, finishing = false, preWaiting = false;
        float gain = 1.0f, postActual = 1.0f, postTarget = 1.0f;
        hypha::live_compare::Verdict verdict = hypha::live_compare::Verdict::noClock;
    };
    static AudioView audioView (const KirinHyphaProcessorBase& processor) noexcept
    {
        const auto& state = processor.liveCompare;
        const auto permission = state.authority.ticket();
        const auto gains = hypha::live_compare::readGainSnapshot (state);
        const bool permitted = state.authority.permitted();
        AudioView result;
        result.finishing = state.completion.pending();
        result.active = ! result.finishing && permitted && state.sessionActive.load (std::memory_order_acquire);
        result.preAudible = permitted && state.preAudible.load (std::memory_order_acquire);
        result.preWaiting = permitted && state.preWaiting.load (std::memory_order_acquire);
        result.matched = permitted && gains.coherent && gains.retained
            && state.matched.load (std::memory_order_acquire)
            && state.matchGeneration.load (std::memory_order_acquire) == state.sessionGeneration.load (std::memory_order_acquire)
            && state.matchRun.load (std::memory_order_acquire) == state.playbackRun.load (std::memory_order_acquire);
        result.verdict = static_cast<hypha::live_compare::Verdict> (state.verdict.load (std::memory_order_acquire));
        result.matchReady = permitted && hypha::live_compare::currentGainReceipt (state, gains)
            && result.verdict == hypha::live_compare::Verdict::accepted;
        result.gain = gains.pre; result.postActual = state.postActual.load (std::memory_order_acquire);
        result.postTarget = gains.coherent ? gains.post : result.postActual;
        std::atomic_thread_fence (std::memory_order_acquire);
        if (gains.revision != state.gainRevision.load (std::memory_order_acquire))
            result.matched = result.matchReady = false;
        if (permission != state.authority.ticket() || ! state.authority.permitted())
            result.active = result.preAudible = result.matched = result.matchReady = false;
        return result; // no message-thread Blind stage, string or mutable renderer state
    }
};
