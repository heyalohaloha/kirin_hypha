#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <cstdint>

// Only the fixture folds project position; emitted PCM continues to advance. The real
// processor reads JUCE's optional loop fields, not a test-only product entry point.
struct LiveBlindLoopFixture
{
    static constexpr std::int64_t length = 24000;
    std::atomic<bool> requested { false };
    std::atomic<std::int64_t> laps { 0 };
    void advance (std::int64_t emitted)
    {
        if (! active && requested.load()) { start = emitted; active = true; }
        if (active) laps.store ((emitted - start) / length);
    }
    void decorate (juce::AudioPlayHead::PositionInfo& info, std::int64_t emitted) const
    {
        if (! active) return;
        const auto project = start + (emitted - start) % length;
        info.setTimeInSamples (project);
        info.setTimeInSeconds (static_cast<double> (project) / 48000);
        info.setIsLooping (true);
        info.setBpm (120.0);
        info.setPpqPosition (static_cast<double> (project) / 24000);
        info.setLoopPoints (juce::AudioPlayHead::LoopPoints {
            static_cast<double> (start) / 24000, static_cast<double> (start + length) / 24000 });
    }
private:
    bool active = false; // audio thread only, including getPosition
    std::int64_t start = 0;
};
