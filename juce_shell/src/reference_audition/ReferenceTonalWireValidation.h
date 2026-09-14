#pragma once

#include "ReferenceTonalRepository.h"

namespace hypha::reference_audition
{
std::shared_ptr<const ReferenceTonalCurve> validateReferenceTonalArtifact (
    const juce::var&, const RuntimeSource&, std::int64_t rangeStartSample,
    std::int64_t rangeEndSample, const juce::String& artifactSha256);
}
