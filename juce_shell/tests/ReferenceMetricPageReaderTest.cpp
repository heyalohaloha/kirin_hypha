#include "reference_audition/ReferenceMetricPageReader.h"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

namespace ref = hypha::reference_audition;
namespace metrics = ref::metrics;
namespace
{
void require (bool condition, const char* message)
{
    if (!condition) throw std::runtime_error (message);
}

class SyntheticReader final : public juce::AudioFormatReader
{
public:
    SyntheticReader (unsigned rate, unsigned channels, juce::int64 length, float offset = 0.0f)
        : AudioFormatReader (nullptr, "metric-reader-test"), bias (offset)
    {
        sampleRate = rate; numChannels = channels; lengthInSamples = length;
        bitsPerSample = 32; usesFloatingPointData = true;
    }
    static float sample (int channel, juce::int64 position, float offset = 0.0f)
    {
        return offset + static_cast<float> (std::sin (static_cast<double> (position) * 0.017))
            * (channel == 0 ? 0.23f : -0.13f);
    }
    bool readSamples (int* const* destination, int channels, int start,
                      juce::int64 position, int frames) override
    {
        for (int channel = 0; channel < channels; ++channel)
            if (destination[channel] != nullptr)
                for (int frame = 0; frame < frames; ++frame)
                    reinterpret_cast<float*> (destination[channel])[start + frame]
                        = sample (channel, position + frame, bias);
        return true;
    }
private:
    float bias;
};

void install (ref::AudioPages& pages, unsigned sourceRate, unsigned sourceChannels,
              unsigned hostRate, unsigned hostChannels, juce::int64 sourceLength,
              float offset = 0.0f)
{
    require (pages.installReaderForTest (
        std::make_unique<SyntheticReader> (sourceRate, sourceChannels, sourceLength, offset),
        hostRate, static_cast<int> (hostChannels), true).isEmpty(), "synthetic reader opens");
}

std::vector<float> direct (ref::AudioPages& pages, std::uint64_t position,
                           unsigned frames, unsigned channels)
{
    juce::AudioBuffer<float> buffer (static_cast<int> (channels), static_cast<int> (frames));
    auto state = ref::AudioPages::RenderResult::waitingForPages;
    for (int attempt = 0; attempt < 3; ++attempt)
    {
        pages.request (static_cast<juce::int64> (position)); pages.service();
        state = pages.renderResult (buffer, static_cast<juce::int64> (position), 1.0f);
        if (state != ref::AudioPages::RenderResult::waitingForPages) break;
    }
    require (state == ref::AudioPages::RenderResult::rendered, "direct gain-one pages render");
    std::vector<float> result (static_cast<std::size_t> (frames) * channels);
    for (unsigned frame = 0; frame < frames; ++frame)
        for (unsigned channel = 0; channel < channels; ++channel)
            result[static_cast<std::size_t> (frame) * channels + channel]
                = buffer.getSample (static_cast<int> (channel), static_cast<int> (frame));
    return result;
}

void matrix()
{
    std::atomic<bool> cancel {false};
    for (const auto rate : {8000u, 44100u, 48000u, 96000u})
        for (const auto channels : {1u, 2u})
        {
            ref::AudioPages pages (ref::AudioPages::ServiceMode::manual);
            install (pages, rate, channels, rate, channels, rate * 4);
            metrics::PageReader reader (pages, rate, channels, cancel);
            const auto aperture = rate / 10;
            for (const auto position : {std::uint64_t {0}, std::uint64_t {rate}})
            {
                std::vector<float> output (aperture * channels, std::numeric_limits<float>::quiet_NaN());
                require (reader.read (position, output.data(), output.size()) == metrics::ReadState::rendered,
                         "all rates/layouts and 8k page crossing render");
                for (unsigned frame = 0; frame < aperture; ++frame)
                    for (unsigned channel = 0; channel < channels; ++channel)
                        require (output[frame * channels + channel]
                            == SyntheticReader::sample (static_cast<int> (channel), static_cast<juce::int64> (position + frame)),
                            "gain-one PCM and channel interleaving are exact");
            }
        }
}

void convertedAndIndependent()
{
    std::atomic<bool> cancel {false};
    ref::AudioPages measured (ref::AudioPages::ServiceMode::manual);
    ref::AudioPages audible (ref::AudioPages::ServiceMode::manual);
    install (measured, 44100, 1, 48000, 2, 44100 * 12);
    install (audible, 44100, 1, 48000, 2, 44100 * 12);
    metrics::PageReader reader (measured, 48000, 2, cancel);
    for (const auto position : {std::uint64_t {0}, std::uint64_t {48000}, std::uint64_t {384000}})
    {
        std::vector<float> actual (4800 * 2);
        require (reader.read (position, actual.data(), actual.size()) == metrics::ReadState::rendered,
                 "SRC mono-to-stereo aperture renders");
        const auto expected = direct (audible, position, 4800, 2);
        require (actual == expected, "dedicated chunked reader matches audible SRC exactly");
        for (std::size_t index = 0; index < actual.size(); index += 2)
            require (actual[index] == actual[index + 1], "mono is duplicated, not re-scaled");
    }
    // Move the audible bank back after the analysis bank has moved eight seconds ahead.
    const auto earlier = direct (audible, 4800, 4800, 2);
    std::vector<float> output (9600);
    require (reader.read (4800, output.data(), output.size()) == metrics::ReadState::rendered,
             "analysis reader can return independently to an earlier page");
    require (output == earlier, "independent page banks do not steal a decoder cursor");
}

void invalidation()
{
    std::atomic<bool> cancel {false};
    ref::AudioPages pages (ref::AudioPages::ServiceMode::manual);
    install (pages, 48000, 2, 48000, 2, 48000 + 17);
    metrics::PageReader reader (pages, 48000, 2, cancel);
    std::vector<float> output (9600, std::numeric_limits<float>::quiet_NaN());
    require (reader.read (0, output.data(), output.size() - 1) != metrics::ReadState::rendered,
             "incomplete interleaved count rejected");
    require (reader.read (0, nullptr, output.size()) != metrics::ReadState::rendered, "null destination rejected");
    require (reader.read (0, output.data(), 0) != metrics::ReadState::rendered, "zero count rejected");
    require (reader.read (48000, output.data(), output.size()) != metrics::ReadState::rendered,
             "partial EOF is not padded into an aperture");
    require (reader.read (std::numeric_limits<std::uint64_t>::max(), output.data(), output.size())
             != metrics::ReadState::rendered, "unrepresentable position rejected");
    cancel.store (true);
    require (reader.read (0, output.data(), output.size()) != metrics::ReadState::rendered, "cancelled read rejected");
    // A fresh token and reader represent the next independent job.
    std::atomic<bool> nextCancel {false};
    metrics::PageReader next (pages, 48000, 2, nextCancel);
    require (next.read (43200, output.data(), output.size()) == metrics::ReadState::rendered,
             "last complete aperture remains readable");
    install (pages, 48000, 2, 48000, 2, 96000, 0.4f);
    require (next.read (0, output.data(), output.size()) != metrics::ReadState::rendered,
             "replacement source generation cannot enter old job");
    metrics::PageReader closing (pages, 48000, 2, nextCancel);
    pages.close();
    require (closing.read (0, output.data(), output.size()) != metrics::ReadState::rendered,
             "closed pages rejected");
}
}

int main()
{
    try
    {
        matrix(); convertedAndIndependent(); invalidation();
        std::cout << "Reference metric page reader contracts passed\n";
        return 0;
    }
    catch (const std::exception& error)
    { std::cerr << error.what() << '\n'; return 1; }
}
