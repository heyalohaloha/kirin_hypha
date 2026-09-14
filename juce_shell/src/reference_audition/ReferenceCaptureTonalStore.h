#pragma once

#include "ReferenceTonalCapture.h"
#include <functional>

namespace hypha::reference_audition
{
struct CaptureTonalArtifactInput
{
    juce::String captureId;
    juce::String baseCaptureSha256;
    std::int64_t hostStart = 0;
    int sampleRate = 0;
    int channels = 0;
    std::uint64_t frames = 0;
    std::array<std::uint32_t, 3> fftSize {}, hopSamples {};
    const std::array<std::vector<float>, 60>* values = nullptr;
};

CaptureTonalSummary storeCaptureTonalArtifact (
    const juce::File& transportRoot, const CaptureTonalArtifactInput&,
    const CaptureTonalSummary& wholeCapture);

CaptureTonalSummary loadCaptureTonalArtifact (
    const juce::File& transportRoot, const CaptureTonalSummary& receipt,
    const juce::String& captureId, std::uint64_t rangeStart, std::uint64_t rangeEnd,
    const std::function<bool()>& cancelled = {});
}
