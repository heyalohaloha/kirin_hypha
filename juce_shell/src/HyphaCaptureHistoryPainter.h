#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include <juce_graphics/juce_graphics.h>

#include "kirin_hypha_ffi.h"

namespace hypha::capture_history
{
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

// Factual two-second/run maxima plus the exact maximum in the visible window. `true_peak.max` is
// used so a short transient is retained rather than replaced with a bucket mean.
TruePeakSummary analyseTruePeak (const std::vector<KirinMeterHistoryEntry>&,
                                 double sampleRate);

// Returns a nearby measured entry while the pointer is inside the history plot or TP event rail.
// Header, padding, and unmeasured portions of the fixed 60-second window return no observation.
std::optional<std::size_t> hitTest (juce::Rectangle<int> area,
                                    const std::vector<KirinMeterHistoryEntry>&,
                                    juce::Point<float> position,
                                    double sampleRate);

// A deliberately narrow 60-second history for the LEVEL Observation Plate. M is the primary
// trajectory, S is secondary, TP is represented as sparse measured event stems, and channel clip
// runs remain timestamped pips rather than a second continuous curve with a competing axis.
void paint (juce::Graphics&,
            juce::Rectangle<int>,
            const std::vector<KirinMeterHistoryEntry>&,
            bool delta,
            double sampleRate,
            std::optional<std::size_t> hoveredIndex = std::nullopt,
            juce::String contextFact = {});
}
