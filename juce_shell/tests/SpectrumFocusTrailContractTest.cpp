#include "SpectrumFocusTrailContractTest.h"

#include "../src/HyphaSpectrumFocusTrail.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>

namespace hypha::tests
{
namespace
{
    void require (bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "Spectrum Focus Trail contract failed at line " << line
                  << ": " << expression << '\n';
        std::exit (EXIT_FAILURE);
    }

   #define KIRIN_FOCUS_REQUIRE(expression) require ((expression), #expression, __LINE__)

    spectrum_focus::DeltaBins bins (float first, float last)
    {
        spectrum_focus::DeltaBins values {};
        for (size_t index = 0; index < values.size(); ++index)
        {
            const float position = static_cast<float> (index)
                                 / static_cast<float> (values.size() - 1u);
            values[index] = first + position * (last - first);
        }
        return values;
    }

    void verifyRate (uint32_t sampleRate)
    {
        spectrum_focus::FocusTrailHistory history;
        const int64_t cadence = sampleRate / ui_contract::spectrumPresentationHz;
        const auto values = bins (-18.0f, 18.0f);
        for (size_t index = 1u; index <= spectrum_focus::focusTrailCapacity; ++index)
        {
            KIRIN_FOCUS_REQUIRE (history.append (
                static_cast<int64_t> (index) * cadence, sampleRate, values)
                == spectrum_focus::AppendResult::appended);
        }
        KIRIN_FOCUS_REQUIRE (history.size() == spectrum_focus::focusTrailCapacity);
        KIRIN_FOCUS_REQUIRE (history.currentSampleRate() == sampleRate);
        KIRIN_FOCUS_REQUIRE (history.endpointAt (history.size() - 1u)
                             == static_cast<int64_t> (
                                 spectrum_focus::focusTrailCapacity) * cadence);
        const double expectedAge = static_cast<double> (
            static_cast<int64_t> (spectrum_focus::focusTrailCapacity - 1u) * cadence)
            / sampleRate;
        KIRIN_FOCUS_REQUIRE (std::abs (history.ageSecondsAt (0u) - expectedAge) < 1.0e-9);
    }
}

void verifySpectrumFocusTrailContract()
{
    using spectrum_focus::AppendResult;
    using spectrum_focus::FocusTrailHistory;

    KIRIN_FOCUS_REQUIRE (spectrum_focus::focusTrailCapacity == 180u);
    KIRIN_FOCUS_REQUIRE (sizeof (FocusTrailHistory) < 256u * 1024u);
    verifyRate (44'100u);
    verifyRate (48'000u);
    verifyRate (96'000u);

    FocusTrailHistory history;
    const auto rising = bins (-12.0f, 12.0f);
    KIRIN_FOCUS_REQUIRE (history.append (-1'600, 48'000u, rising)
                         == AppendResult::appended);
    KIRIN_FOCUS_REQUIRE (history.append (0, 48'000u, rising)
                         == AppendResult::appended);
    KIRIN_FOCUS_REQUIRE (history.append (0, 48'000u, bins (6.0f, -6.0f))
                         == AppendResult::duplicateIgnored);
    KIRIN_FOCUS_REQUIRE (history.size() == 2u);
    KIRIN_FOCUS_REQUIRE (std::abs (history.valueAt (1u, 0.0f) + 12.0f) < 0.001f);
    KIRIN_FOCUS_REQUIRE (std::abs (history.valueAt (1u, 1.0f) - 12.0f) < 0.001f);
    KIRIN_FOCUS_REQUIRE (std::abs (history.valueAt (1u, 0.5f)) < 0.1f);

    KIRIN_FOCUS_REQUIRE (history.append (3'200, 48'000u, rising)
                         == AppendResult::discontinuityReset);
    KIRIN_FOCUS_REQUIRE (history.size() == 1u && history.endpointAt (0u) == 3'200);
    KIRIN_FOCUS_REQUIRE (history.append (1'600, 48'000u, rising)
                         == AppendResult::discontinuityReset);
    KIRIN_FOCUS_REQUIRE (history.size() == 1u && history.endpointAt (0u) == 1'600);
    KIRIN_FOCUS_REQUIRE (history.append (2'940, 44'100u, rising)
                         == AppendResult::discontinuityReset);
    KIRIN_FOCUS_REQUIRE (history.currentSampleRate() == 44'100u);

    auto invalid = rising;
    invalid[7] = std::numeric_limits<float>::quiet_NaN();
    KIRIN_FOCUS_REQUIRE (history.append (4'410, 44'100u, invalid)
                         == AppendResult::rejected);
    KIRIN_FOCUS_REQUIRE (history.size() == 1u && history.endpointAt (0u) == 2'940);

    history.clear();
    const int64_t cadence = 1'600;
    for (size_t index = 1u; index <= spectrum_focus::focusTrailCapacity + 1u; ++index)
        KIRIN_FOCUS_REQUIRE (history.append (
            static_cast<int64_t> (index) * cadence, 48'000u, rising)
            == AppendResult::appended);
    KIRIN_FOCUS_REQUIRE (history.size() == spectrum_focus::focusTrailCapacity);
    KIRIN_FOCUS_REQUIRE (history.endpointAt (0u) == 2 * cadence);
    KIRIN_FOCUS_REQUIRE (history.endpointAt (history.size() - 1u)
                         == static_cast<int64_t> (
                             spectrum_focus::focusTrailCapacity + 1u) * cadence);
}
}
