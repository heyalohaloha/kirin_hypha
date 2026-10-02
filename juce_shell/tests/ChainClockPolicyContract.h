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
    require (live_compare_clock_policy::classify (Wrapper::wrapperType_VST3,
        host_identity::Identity { "Studio Pro", "8.1.2.113407", "8.1.2.0" }, 48000.0)
            .authority == ClockAuthority::certifiedContent,
        "native Windows text, not its truncated fixed version, selects the VST3 certificate");
    require (live_compare_clock_policy::classify (Wrapper::wrapperType_VST3,
        host_identity::Identity { "Studio Pro", "", "8.1.2.0" }, 48000.0)
            .authority == ClockAuthority::none,
        "a missing native text must not fall back to the fixed Studio Pro version");
    require (live_compare_clock_policy::classify (Wrapper::wrapperType_AAX,
        host_identity::Identity { "ProTools", "26.4.0.5\"", "26.4.0.5" }, 48000.0)
            .authority == ClockAuthority::boundedAaxEngine,
        "AAX retains the measured fixed-version identity without trimming its quoted text");
    require (live_compare_clock_policy::classify (Wrapper::wrapperType_AAX,
        host_identity::Identity { "ProTools", "26.4.0.5", "26.4.1.179" }, 48000.0)
            .authority == ClockAuthority::none,
        "an unmeasured fixed AAX version must not fall back to a certified-looking text");
    for (const auto& version : { "", "8.1.2.0", "8.1.2.113408", "8.1.2 Build 113408" })
        require (live_compare_clock_policy::classify (Wrapper::wrapperType_VST3,
            "Studio Pro", version, 48000.0).authority == ClockAuthority::none,
            "missing, fixed-only or unmeasured build identity must fail closed");
    require (live_compare_clock_policy::classify (Wrapper::wrapperType_VST3,
        "Studio Pro 8", "8.1.2 Build 113407", 48000.0).authority == ClockAuthority::none,
        "an app folder name is not an executable identity");
    const auto aax = live_compare_clock_policy::classify (Wrapper::wrapperType_AAX,
        "ProTools", "26.4.0.5", 48000.0);
    require (aax.authority == ClockAuthority::boundedAaxEngine && aax.maximumDelaySamples == 16383,
             "measured AAX host uses the documented 48 kHz compensation bound");
    require (live_compare_clock_policy::proToolsDelayBound (96000.0) == 32767
        && live_compare_clock_policy::proToolsDelayBound (192000.0) == 65534
        && live_compare_clock_policy::proToolsDelayBound (50000.0) == 0,
        "AAX delay bounds cover only documented sample rates");
    for (const auto& version : { "26.4.0.6", "26.4.1.179", "2026.4.0.5", "2026.5" })
        require (live_compare_clock_policy::classify (Wrapper::wrapperType_AAX,
            "ProTools", version, 48000.0).authority == ClockAuthority::none,
            "an unmeasured or marketing-form Pro Tools version is not silently certified");
   #if ! JUCE_DEBUG
    for (const auto wrapper : { Wrapper::wrapperType_VST3, Wrapper::wrapperType_AudioUnit,
                                Wrapper::wrapperType_AAX })
        require (chain_clock_policy::current (wrapper) == KIRIN_CHAIN_CLOCK_POLICY_UNKNOWN,
                 "a release build certifies no host");
   #endif
}
}
