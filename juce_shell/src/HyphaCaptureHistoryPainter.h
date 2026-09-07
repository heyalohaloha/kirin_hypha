#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <juce_graphics/juce_graphics.h>

#include "kirin_hypha_ffi.h"

namespace hypha::capture_history
{
constexpr double absoluteLoudnessMinimum = -36.0;
constexpr double absoluteLoudnessMaximum = 0.0;
constexpr double deltaLoudnessMinimum = -12.0;
constexpr double deltaLoudnessMaximum = 12.0;

constexpr double normalizedLoudness (double value, bool delta) noexcept
{
    const auto minimum = delta ? deltaLoudnessMinimum : absoluteLoudnessMinimum;
    const auto maximum = delta ? deltaLoudnessMaximum : absoluteLoudnessMaximum;
    const auto normalized = (value - minimum) / (maximum - minimum);
    return normalized < 0.0 ? 0.0 : normalized > 1.0 ? 1.0 : normalized;
}

struct TruePeakSummary
{
    bool available = false;
    double windowMaximumDbtp = 0.0;
    double secondsBeforeEnd = 0.0;
    std::size_t windowMaximumIndex = 0;
    std::vector<std::size_t> eventIndices;
};

// Removes observations newer than the already displayed parent frame. This lets a user-triggered
// history poll enrich Capture without mixing a later audio callback into the frozen UI fact.
void retainThrough (std::vector<KirinMeterHistoryEntry>&, std::uint64_t observedFrames);

// One maximum for each contiguous excursion above the shared TP emphasis threshold, plus the exact
// maximum in the visible window. `true_peak.max` retains a short transient instead of replacing it
// with a bucket mean; periodic local maxima are deliberately not promoted to visual events.
TruePeakSummary analyseTruePeak (const std::vector<KirinMeterHistoryEntry>&,
                                 double sampleRate);

// Returns a nearby measured entry while the pointer is inside the shared loudness / TP plot.
// Header, padding, and unmeasured portions of the fixed 60-second window return no observation.
std::optional<std::size_t> hitTest (juce::Rectangle<int> area,
                                    const std::vector<KirinMeterHistoryEntry>&,
                                    juce::Point<float> position,
                                    double sampleRate);

// A shared 60-second LEVEL context plot. M is the only loudness path; sparse TP events use the
// right +6..-24 dBTP axis, and channel clip runs remain timestamped pips. Detailed M/S/TP history
// belongs to TIME, so LEVEL does not duplicate its secondary S path.
void paint (juce::Graphics&,
            juce::Rectangle<int>,
            const std::vector<KirinMeterHistoryEntry>&,
            bool delta,
            double sampleRate,
            std::optional<std::size_t> hoveredIndex = std::nullopt,
            juce::String contextFact = {});
}
