#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include <juce_graphics/juce_graphics.h>

#include "kirin_hypha_ffi.h"

namespace hypha::run_summary
{
constexpr std::size_t maximumCachedRuns = 64u;

struct Range
{
    bool available = false;
    double minimum = 0.0;
    double maximum = 0.0;
    double mean = 0.0;
};

struct Summary
{
    std::uint64_t generation = 0;
    std::uint64_t runId = 0;
    std::uint64_t firstObservedFrames = 0;
    std::uint64_t lastObservedFrames = 0;
    std::uint64_t observationCount = 0;
    std::array<std::uint64_t, 2> clipEvents {};
    Range momentary;
    Range shortTerm;
    Range correlation;
    Range plr;
    bool truePeakAvailable = false;
    double maximumTruePeak = 0.0;
};

struct Result
{
    bool exactTimeline = false;
    std::vector<Summary> runs;

    bool available() const noexcept { return ! runs.empty(); }
};

// Consumes one already-selected TIME resolution. Unknown-clock observations remain one natural
// run, while exactTimeline is asserted only when every observation has a valid DAW sample span.
Result summarize (const std::vector<KirinMeterHistoryEntry>& history);

int visibleRowCount (int width) noexcept;
double durationSeconds (const Summary&, double sampleRate) noexcept;

void paint (juce::Graphics&, juce::Rectangle<int>, const Result&, double sampleRate);
}
