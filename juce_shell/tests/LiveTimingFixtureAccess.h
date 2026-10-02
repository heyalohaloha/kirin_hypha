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
};
