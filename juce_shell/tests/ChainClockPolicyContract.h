#pragma once

#include "../src/HyphaChainClockPolicy.h"

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
   #if ! JUCE_DEBUG
    for (const auto wrapper : { Wrapper::wrapperType_VST3, Wrapper::wrapperType_AudioUnit,
                                Wrapper::wrapperType_AAX })
        require (chain_clock_policy::current (wrapper) == KIRIN_CHAIN_CLOCK_POLICY_UNKNOWN,
                 "a release build certifies no host");
   #endif
}
}
