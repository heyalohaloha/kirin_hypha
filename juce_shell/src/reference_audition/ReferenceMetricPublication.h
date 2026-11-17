#pragma once
#include "ReferenceMetricSeries.h"
#include <array>
#include <memory>
#include <juce_core/juce_core.h>
namespace hypha::reference_audition::metrics
{
struct SeriesData
{
    std::vector<KirinReferenceMetricFrame> frames;
    std::uint64_t sourceFrames = 0;
    std::uint32_t rate = 0, channels = 0;
    bool fullScanComplete = false; // Prefix Cue facts never imply full-source completion.
};
enum class Availability { unavailable, measuring, ready };
struct Publication
{
    Availability state = Availability::unavailable;
    std::uint64_t selectionGeneration = 0;
    juce::String sourceKey, reason;
    std::array<juce::String, 3> cueReasons;
    std::array<Summary, 3> cue;
    std::shared_ptr<const SeriesData> series;
    juce::String setKey, setReason;
    std::uint64_t setGeneration = 0;
    std::size_t setExpected = 0, setCompleted = 0;
    bool setComplete = false;
    std::array<Band, 3> bands;
};
}
