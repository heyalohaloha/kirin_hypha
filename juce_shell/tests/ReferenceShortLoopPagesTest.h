#pragma once
#include "../src/reference_audition/ReferenceAudioPages.h"
#include <cstring>

namespace hypha::tests::short_loop_pages
{
class Reader final : public juce::AudioFormatReader
{
public:
    Reader() : juce::AudioFormatReader (nullptr, "loop-pages")
    {
        sampleRate = 96'000; numChannels = 2; lengthInSamples = 96'000 * 60;
        bitsPerSample = 32; usesFloatingPointData = true;
    }
    static float valueAt (int channel, std::int64_t position)
    { return static_cast<float> ((channel + 1) * 0.1 + static_cast<double> (position) * 1.0e-8); }
    bool readSamples (int* const* destination, int channels, int offset,
                      juce::int64 start, int frames) override
    {
        for (int c = 0; c < channels; ++c)
            for (int f = 0; f < frames; ++f)
                reinterpret_cast<float*> (destination[c])[offset + f] = valueAt (c, start + f);
        return true;
    }
};
}

template <class Require>
void verifyShortLoopPages (Require require)
{
    using Reader = hypha::tests::short_loop_pages::Reader;
    constexpr std::int64_t rate = 96'000;
    hypha::reference_audition::AudioPages pages {
        hypha::reference_audition::AudioPages::ServiceMode::manual };
    require (pages.installReaderForTest (std::make_unique<Reader>(),
                                        rate, 2).isEmpty(), "short-loop source opens");
    // Fill all six slots with earlier locations before approaching the loop.
    for (const int second : { 10, 20, 47, 48, 49 })
    {
        pages.request (second * rate + rate * 9 / 10);
        pages.service();
        require (pages.readyAt (second * rate, 512), "current loop page remains resident");
    }
    juce::AudioBuffer<float> output (2, 512);
    // The host wraps before another worker service: the upcoming page must survive.
    require (pages.readyAt (47 * rate + rate * 9 / 10, 512)
        && pages.render (output, 47 * rate + rate * 9 / 10, 1.0f),
        "nearby loop-start page survives lookahead replacement");
    for (int ch = 0; ch < 2; ++ch)
        for (int f = 0; f < output.getNumSamples(); ++f)
        {
            const auto expected = Reader::valueAt (ch, 47 * rate + rate * 9 / 10 + f);
            require (std::memcmp (output.getReadPointer (ch) + f, &expected, sizeof (expected)) == 0,
                     "retained loop page has the exact requested PCM");
        }
}
