#pragma once

#include "ReferenceRuntimeV2Measurement.h"

namespace hypha::reference_audition
{
inline const char* crestLabel (const RuntimeDetailedMeasurement* measurement) noexcept
{
    return measurement != nullptr && measurement->dynamics && ! measurement->dynamics->samplePeakCrest
        ? "CREST (OLD)" : "CREST";
}
}
