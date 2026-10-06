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
    static bool configureStudioProClock (KirinHyphaProcessorBase& processor) noexcept
    {
        const auto certificate = hypha::live_compare_clock_policy::classify (
            juce::AudioProcessor::wrapperType_VST3, "Studio Pro", "8.1.2.113407", 48000);
        if (certificate.authority != hypha::live_compare::ClockAuthority::certifiedContent
            || certificate.maximumDelaySamples != 0) return false;
        processor.liveCompare.clockAuthority = static_cast<std::uint8_t> (certificate.authority);
        processor.liveCompare.maximumDelaySamples = certificate.maximumDelaySamples;
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
