#include "SpectrumPresentationContractTest.h"

#include "../src/HyphaSpectrumPresentation.h"
#include "../src/HyphaSpectrumGeometry.h"
#include "../src/HyphaSpectrumChromePainter.h"
#include "kirin_hypha_ffi.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>

namespace hypha::tests
{
namespace
{
    constexpr std::size_t bandCount = KIRIN_SPECTRUM_BAND_COUNT;

    void require (bool condition, const char* expression, int line)
    {
        if (condition)
            return;
        std::cerr << "Spectrum presentation contract failed at line " << line
                  << ": " << expression << '\n';
        std::exit (EXIT_FAILURE);
    }

#define KIRIN_SPECTRUM_REQUIRE(expression) require ((expression), #expression, __LINE__)

    float frequencyForBand (std::size_t index, float minimumHz, float maximumHz)
    {
        const float position = spectrum_geometry::bandCentreNormalisedX (index);
        return minimumHz * std::pow (maximumHz / minimumHz, position);
    }

    void verifyWeightSpan (float minimumHz, float maximumHz)
    {
        const auto weights = spectrum_presentation::lowFrequencyCalmWeights<bandCount> (
            minimumHz, maximumHz);
        KIRIN_SPECTRUM_REQUIRE (
            std::abs (weights.front() - spectrum_presentation::lowCalmMaximumBlend)
            < 1.0e-6f);

        bool reachedUnchangedRange = false;
        for (std::size_t index = 0; index < bandCount; ++index)
        {
            const float frequencyHz = frequencyForBand (index, minimumHz, maximumHz);
            KIRIN_SPECTRUM_REQUIRE (std::isfinite (weights[index]));
            KIRIN_SPECTRUM_REQUIRE (weights[index] >= 0.0f);
            KIRIN_SPECTRUM_REQUIRE (
                weights[index] <= spectrum_presentation::lowCalmMaximumBlend);
            if (index > 0u)
                KIRIN_SPECTRUM_REQUIRE (weights[index] <= weights[index - 1u]);
            if (frequencyHz <= spectrum_presentation::lowCalmFullEndHz)
                KIRIN_SPECTRUM_REQUIRE (
                    std::abs (weights[index]
                              - spectrum_presentation::lowCalmMaximumBlend) < 1.0e-5f);
            if (frequencyHz >= spectrum_presentation::lowCalmTaperEndHz)
            {
                reachedUnchangedRange = true;
                KIRIN_SPECTRUM_REQUIRE (weights[index] <= 0.0f);
            }
        }
        KIRIN_SPECTRUM_REQUIRE (reachedUnchangedRange);
    }

    void requireZeroWeights (float minimumHz, float maximumHz)
    {
        const auto weights = spectrum_presentation::lowFrequencyCalmWeights<bandCount> (
            minimumHz, maximumHz);
        KIRIN_SPECTRUM_REQUIRE (std::all_of (
            weights.begin(), weights.end(), [] (float value) { return value <= 0.0f; }));
    }
}

void verifySpectrumPresentationContract()
{
    // The host-rate aperture keeps the zero-padded bin below 10 Hz at common rates.
    verifyWeightSpan (10.0f, 22'000.0f);
    verifyWeightSpan (96'000.0f / 16'384.0f, 22'000.0f);
    requireZeroWeights (0.0f, 22'000.0f);
    requireZeroWeights (22'000.0f, 10.0f);
    requireZeroWeights (std::numeric_limits<float>::quiet_NaN(), 22'000.0f);

    KIRIN_SPECTRUM_REQUIRE (
        std::abs (spectrum_geometry::bandPositionForNormalisedX (
                      spectrum_geometry::bandCentreNormalisedX (37u))
                  - 37.0f) < 1.0e-5f);
    KIRIN_SPECTRUM_REQUIRE (
        spectrum_geometry::bandPositionForNormalisedX (0.0f) == 0.0f);
    KIRIN_SPECTRUM_REQUIRE (std::abs (
        spectrum_geometry::bandPositionForNormalisedX (1.0f)
        - static_cast<float> (bandCount - 1u)) < 1.0e-6f);
    KIRIN_SPECTRUM_REQUIRE (
        spectrum_chrome::frequencyReadoutText (17.0f, 35.15625f) == "~17 Hz");
    KIRIN_SPECTRUM_REQUIRE (
        spectrum_chrome::frequencyReadoutText (30.0f, 35.15625f) == "~30 Hz");
    KIRIN_SPECTRUM_REQUIRE (
        spectrum_chrome::frequencyReadoutText (36.0f, 35.15625f) == "36 Hz");

    const auto weights = spectrum_presentation::lowFrequencyCalmWeights<bandCount> (
        10.0f, 22'000.0f);
    float preInput[bandCount] {};
    float postInput[bandCount] {};
    float deltaInput[bandCount] {};
    for (std::size_t index = 0; index < bandCount; ++index)
    {
        const float ripple = index % 2u == 0u ? 8.0f : -8.0f;
        preInput[index] = -48.0f + 0.35f * ripple;
        deltaInput[index] = ripple;
        postInput[index] = preInput[index] + deltaInput[index];
    }

    const auto pre = spectrum_presentation::calmLowFrequencies (preInput, weights);
    const auto post = spectrum_presentation::calmLowFrequencies (postInput, weights);
    const auto delta = spectrum_presentation::calmLowFrequencies (deltaInput, weights);
    KIRIN_SPECTRUM_REQUIRE (std::abs (delta[20]) < std::abs (deltaInput[20]));
    const auto repeatedDelta =
        spectrum_presentation::calmLowFrequencies (deltaInput, weights);
    KIRIN_SPECTRUM_REQUIRE (std::memcmp (repeatedDelta.data(), delta.data(),
                                        sizeof (deltaInput)) == 0);

    for (std::size_t index = 0; index < bandCount; ++index)
    {
        KIRIN_SPECTRUM_REQUIRE (std::isfinite (pre[index]));
        KIRIN_SPECTRUM_REQUIRE (std::isfinite (post[index]));
        KIRIN_SPECTRUM_REQUIRE (std::isfinite (delta[index]));
        KIRIN_SPECTRUM_REQUIRE (
            std::abs ((post[index] - pre[index]) - delta[index]) < 1.0e-5f);
        KIRIN_SPECTRUM_REQUIRE (delta[index] >= -8.0f && delta[index] <= 8.0f);
        if (weights[index] <= 0.0f)
        {
            KIRIN_SPECTRUM_REQUIRE (
                std::memcmp (&pre[index], &preInput[index], sizeof (float)) == 0);
            KIRIN_SPECTRUM_REQUIRE (
                std::memcmp (&post[index], &postInput[index], sizeof (float)) == 0);
            KIRIN_SPECTRUM_REQUIRE (
                std::memcmp (&delta[index], &deltaInput[index], sizeof (float)) == 0);
        }
    }

    float flatInput[bandCount];
    std::fill (std::begin (flatInput), std::end (flatInput), -36.0f);
    const auto flatOutput =
        spectrum_presentation::calmLowFrequencies (flatInput, weights);
    KIRIN_SPECTRUM_REQUIRE (std::memcmp (flatOutput.data(), flatInput,
                                        sizeof (flatInput)) == 0);

    const auto invalidWeights =
        spectrum_presentation::lowFrequencyCalmWeights<bandCount> (0.0f, 0.0f);
    const auto invalidOutput =
        spectrum_presentation::calmLowFrequencies (deltaInput, invalidWeights);
    KIRIN_SPECTRUM_REQUIRE (std::memcmp (invalidOutput.data(), deltaInput,
                                        sizeof (deltaInput)) == 0);
}
}
