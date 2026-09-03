#include "../src/reference_audition/ReferenceAudioPages.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>

namespace ref = hypha::reference_audition;

namespace
{
    void require (bool condition, const char* message)
    {
        if (! condition)
        {
            std::cerr << "Reference audio pages test failed: " << message << '\n';
            std::exit (1);
        }
    }

    class TestReader final : public juce::AudioFormatReader
    {
    public:
        TestReader (double rate, int channels, std::int64_t length)
            : juce::AudioFormatReader (nullptr, "test")
        {
            sampleRate = rate;
            numChannels = static_cast<unsigned int> (channels);
            lengthInSamples = length;
            bitsPerSample = 32;
            usesFloatingPointData = true;
        }

        static float valueAt (int channel, std::int64_t position)
        {
            return static_cast<float> ((channel + 1) * 0.1
                                       + static_cast<double> (position % 1000) / 10'000.0);
        }

        bool readSamples (int* const* destination, int destinationChannels,
                          int destinationOffset, juce::int64 sourceStart,
                          int frames) override
        {
            for (int channel = 0; channel < destinationChannels; ++channel)
            {
                auto* samples = reinterpret_cast<float*> (destination[channel]);
                for (int frame = 0; frame < frames; ++frame)
                    samples[destinationOffset + frame] = valueAt (channel, sourceStart + frame);
            }
            return true;
        }
    };

    bool closeEnough (float left, float right)
    {
        return std::abs (left - right) < 1.0e-6f;
    }
}

int main()
{
    ref::AudioPages pages;
    require (pages.installReaderForTest (
                 std::make_unique<TestReader> (48'000.0, 2, 480'000), 48'000.0, 2).isEmpty(),
             "matching host format must open");
    require (pages.sourceOpen(), "successful source must publish open state");
    require (pages.cachedPageFrames() == 48'000, "cache page must cover one second");
    require (pages.readyAt (100, 512), "initial page must be ready before publication");

    juce::AudioBuffer<float> output (2, 512);
    output.clear();
    require (pages.render (output, 100, 0.5f), "ready page must render without I/O");
    require (closeEnough (output.getSample (0, 0), TestReader::valueAt (0, 100) * 0.5f)
             && closeEnough (output.getSample (1, 511), TestReader::valueAt (1, 611) * 0.5f),
             "render must preserve channel, position, and B-only gain");

    const auto boundary = static_cast<std::int64_t> (pages.cachedPageFrames() - 2);
    pages.request (boundary);
    pages.service();
    juce::AudioBuffer<float> crossing (2, 8);
    crossing.clear();
    require (pages.render (crossing, boundary, 1.0f),
             "one callback crossing two immutable pages must render atomically");
    for (int frame = 0; frame < crossing.getNumSamples(); ++frame)
        require (closeEnough (crossing.getSample (0, frame),
                              TestReader::valueAt (0, boundary + frame)),
                 "page boundary must be sample-continuous");

    juce::AudioBuffer<float> miss (2, 32);
    miss.clear();
    miss.addFrom (0, 0, output, 0, 0, 32);
    miss.addFrom (1, 0, output, 1, 0, 32);
    const auto before = miss.getSample (0, 0);
    require (! pages.render (miss, 300'000, 1.0f),
             "uncached transport jump must fail closed to caller-owned A");
    require (closeEnough (miss.getSample (0, 0), before),
             "cache miss must not partially overwrite A");
    pages.request (300'000);
    pages.service();
    require (pages.render (miss, 300'000, 1.0f),
             "non-RT service must make the requested jump available");

    pages.close();
    require (! pages.sourceOpen() && ! pages.render (miss, 0, 1.0f),
             "closed source must keep A untouched");
    require (pages.installReaderForTest (
                 std::make_unique<TestReader> (44'100.0, 2, 441'000), 48'000.0, 2)
                 == "sample_rate_unsupported",
             "unimplemented resampling must reject instead of playing at the wrong speed");
    require (pages.installReaderForTest (
                 std::make_unique<TestReader> (48'000.0, 1, 480'000), 48'000.0, 2)
                 == "channel_layout_unsupported",
             "channel mismatch must reject instead of fabricating a layout");

    std::cout << "Reference audio page tests passed\n";
    return 0;
}
