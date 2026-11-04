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
    // 探す細かさは host の 1 sample。音源の rate が host より高いと、同じ位置でも音源の sample では数 sample 動いて見える。
    inline bool referenceAlignmentMoved (std::int64_t found, std::int64_t predicted, std::int64_t sourceRate, std::int64_t hostRate) noexcept
    {
        const auto tolerance = hostRate > 0 && sourceRate > hostRate ? (sourceRate + hostRate - 1) / hostRate : std::int64_t { 1 };
        return (found > predicted ? found - predicted : predicted - found) > tolerance;
    }

    RuntimeContentAlignment alignReferenceContent (
        const RuntimeACaptureAudio&, const RuntimeSource&,
        const RuntimeDetailedMeasurement&, bool sampleRateConversionApproved,
        std::optional<std::int64_t> expectedSourceStart = std::nullopt);
}
