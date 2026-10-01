#pragma once

#include "../src/HyphaChainClockPolicy.h"
#include "../src/HyphaLiveCompareClockPolicy.h"

#include <cstdlib>
#include <iostream>

namespace hypha::tests
{
// Only the one measured host is certified, and a release build certifies none, so CHAIN ACTION
// never reaches the product screen.
inline void verifyChainClockPolicy()
{
    const auto require = [] (bool ok, const char* what)
    {
        if (ok) return;
        std::cerr << "Chain clock policy: " << what << '\n';
        std::exit (EXIT_FAILURE);
    };
    using Wrapper = juce::AudioProcessor;
    using chain_clock_policy::classify;
    require (classify (true, Wrapper::wrapperType_VST3, "Studio Pro", "8.1.2.113407")
                 == KIRIN_CHAIN_CLOCK_POLICY_STUDIO_PRO_812_WINDOWS_VST3, "the measured host");
    require (classify (false, Wrapper::wrapperType_VST3, "Studio Pro", "8.1.2.113407") == 0, "macOS");
    require (classify (true, Wrapper::wrapperType_AAX, "Studio Pro", "8.1.2.113407") == 0, "AAX");
    require (classify (true, Wrapper::wrapperType_VST3, "Studio Pro", "8.1.2.113408") == 0, "version");
    require (classify (true, Wrapper::wrapperType_VST3, "Other Host", "8.1.2.113407") == 0, "host");
    using live_compare::ClockAuthority;
    const auto macVst = live_compare_clock_policy::classify (Wrapper::wrapperType_VST3,
        "Studio Pro", "8.1.2 Build 113407", 48000.0);
    require (macVst.authority == ClockAuthority::certifiedContent && macVst.maximumDelaySamples == 0,
             "measured macOS VST3 content clock is exact-host certified");
    const auto windowsVst = live_compare_clock_policy::classify (Wrapper::wrapperType_VST3,
        "Studio Pro", "8.1.2.113407", 48000.0);
    require (windowsVst.authority == ClockAuthority::certifiedContent,
             "measured Windows VST3 content clock is exact-host certified");
    const auto aax = live_compare_clock_policy::classify (Wrapper::wrapperType_AAX,
        "ProTools", "2026.4.0.158", 48000.0);
    require (aax.authority == ClockAuthority::boundedAaxEngine && aax.maximumDelaySamples == 16383,
             "measured AAX host uses the documented 48 kHz compensation bound");
    require (live_compare_clock_policy::proToolsDelayBound (96000.0) == 32767
        && live_compare_clock_policy::proToolsDelayBound (192000.0) == 65534
        && live_compare_clock_policy::proToolsDelayBound (50000.0) == 0,
        "AAX delay bounds cover only documented sample rates");
    require (live_compare_clock_policy::classify (Wrapper::wrapperType_AAX,
        "ProTools", "2026.5", 48000.0).authority == ClockAuthority::none,
        "an unmeasured Pro Tools release is not silently certified");
   #if ! JUCE_DEBUG
    for (const auto wrapper : { Wrapper::wrapperType_VST3, Wrapper::wrapperType_AudioUnit,
                                Wrapper::wrapperType_AAX })
        require (chain_clock_policy::current (wrapper) == KIRIN_CHAIN_CLOCK_POLICY_UNKNOWN,
                 "a release build certifies no host");
   #endif
}
}
