#pragma once
#include "TestProcessorStorage.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>
#include <type_traits>
#include <thread>
namespace hypha::tests
{
template <typename Sample>
inline void precisionProcessorBenchmark (int frames)
{
    using Processor = KirinHyphaProcessorBase;
    struct Clock final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        { PositionInfo p; p.setIsPlaying (true); p.setTimeInSamples (position); return p; }
        std::int64_t position = 0;
    } clock;
    std::vector<double> times; times.reserve (2048);
    for (auto role : {Processor::Role::Pre, Processor::Role::Post})
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
        Processor processor (role);
        processor.setProcessingPrecision (std::is_same_v<Sample, double> ? juce::AudioProcessor::doublePrecision
                                                                       : juce::AudioProcessor::singlePrecision);
        processor.setPlayHead (&clock); prepareFixtureProcessor (processor, 48000, frames);
        juce::AudioBuffer<Sample> audio (2, frames); juce::MidiBuffer midi;
        auto due = std::chrono::steady_clock::now();
        for (int block = 0; block < 1152; ++block)
        {
            for (int ch = 0; ch < 2; ++ch) for (int f = 0; f < frames; ++f)
                audio.setSample (ch, f, static_cast<Sample> (0.125 + (f % 19) / 1024.0));
            const auto started = std::chrono::steady_clock::now();
            processor.processBlock (audio, midi);
            const auto elapsed = std::chrono::duration<double, std::micro> (std::chrono::steady_clock::now() - started).count();
            if (block >= 128) times.push_back (elapsed);
            clock.position += frames;
            due += std::chrono::duration_cast<std::chrono::steady_clock::duration> (
                std::chrono::duration<double> (static_cast<double> (frames) / 48000.0));
            if (due > std::chrono::steady_clock::now()) std::this_thread::sleep_until (due);
        }
        processor.releaseResources();
    }
    std::sort (times.begin(), times.end());
    std::cout << "normal A " << sizeof (Sample) * 8 << "bit PRE/POST stereo frames=" << frames
              << " mean_us=" << std::accumulate (times.begin(), times.end(), 0.0) / static_cast<double> (times.size())
              << " p50_us=" << times[times.size() / 2] << " p95_us=" << times[times.size() * 95 / 100] << '\n';
}
inline void precisionProcessorBenchmarks (int frames)
{ precisionProcessorBenchmark<float> (frames); precisionProcessorBenchmark<double> (frames); }
}
