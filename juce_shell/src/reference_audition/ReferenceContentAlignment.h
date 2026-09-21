#pragma once
#include <optional>
#include "ReferenceRuntimeACapture.h"
#include "ReferenceRuntimeV2Measurement.h"

namespace hypha::reference_audition
{
    struct RuntimeContentAlignment
    {
        bool established = false;
        std::int64_t sourceStartSample = 0;
        double minimumCorrelation = 0.0;
        double minimumAmbiguityDb = 0.0;
        std::int64_t windowSpreadSamples = 0;
        std::vector<float> alignedProbe;
        juce::String reason;
    };

    // Only the worker calls this. Coarse search uses OS's already measured RMS
    // bins; bounded file windows refine the location without decoding the song.
    RuntimeContentAlignment alignReferenceContent (
        const RuntimeACaptureAudio&, const RuntimeSource&,
        const RuntimeDetailedMeasurement&, bool sampleRateConversionApproved,
        std::optional<std::int64_t> expectedSourceStart = std::nullopt);
}
