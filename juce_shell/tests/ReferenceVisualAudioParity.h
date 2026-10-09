#pragma once
#include "../src/reference_audition/ReferenceVisualAudio.h"
#include <cstring>
#include <iostream>
#include <cmath>
namespace reference_visual_parity
{
inline bool uncachedAudio (juce::AudioFormatReader& reader, std::int64_t pageStart,
    int framesPerPage, int hostRate, int channels, juce::AudioBuffer<float>& output,
    juce::AudioBuffer<float>& conversionInput,
    const std::function<bool(const std::function<void()>&)>& conversionGuard)
{
    if (pageStart < 0 || framesPerPage < 1 || hostRate < 8000 || hostRate > 768000
        || !std::isfinite (reader.sampleRate) || reader.sampleRate < 8000 || reader.sampleRate > 768000
        || channels < 1 || channels > 2 || int (reader.numChannels) != channels) return false;
    const auto sourceSampleRate = reader.sampleRate;
    const auto outputSampleRate = double (hostRate);
    output.setSize (channels, framesPerPage, false, false, true);
    if (std::abs (sourceSampleRate - outputSampleRate) <= 0.001)
        return reader.read (&output, 0, framesPerPage, pageStart, true, channels > 1);
        constexpr int halfTaps = 24;
        const double ratio = sourceSampleRate / outputSampleRate;
        const double firstPosition = static_cast<double> (pageStart) * ratio;
        const double lastPosition = static_cast<double> (pageStart + framesPerPage - 1) * ratio;
        const auto rawStart = static_cast<std::int64_t> (std::floor (firstPosition)) - halfTaps;
        const auto rawEnd = static_cast<std::int64_t> (std::ceil (lastPosition)) + halfTaps + 1;
        const auto readStart = juce::jmax<std::int64_t> (0, rawStart);
        const auto readEnd = juce::jmin<std::int64_t> (reader.lengthInSamples, rawEnd);
        const int readFrames = static_cast<int> (juce::jmax<std::int64_t> (0, readEnd - readStart));
        conversionInput.setSize (channels, juce::jmax (1, readFrames), false, false, true);
        conversionInput.clear();
        if (readFrames > 0 && ! reader.read (&conversionInput, 0, readFrames,
                                              readStart, true, channels > 1))
            return false;

    const auto convert = [&] {
        const double cutoff = juce::jmin (1.0, outputSampleRate / sourceSampleRate) * 0.94;
        for (int outputFrame = 0; outputFrame < framesPerPage; ++outputFrame)
        {
            const double position = static_cast<double> (pageStart + outputFrame) * ratio;
            const auto center = static_cast<std::int64_t> (std::floor (position));
            // The source position and sinc/window coefficients are shared by both channels.
            // Accumulate each channel in the original tap order, preserving the exact PCM.
            double weighted[2] { 0.0, 0.0 };
            double weightSum = 0.0;
            for (int tap = -halfTaps + 1; tap <= halfTaps; ++tap)
            {
                const auto sourceFrame = center + tap;
                if (sourceFrame < 0 || sourceFrame >= reader.lengthInSamples)
                    continue;
                const double distance = position - static_cast<double> (sourceFrame);
                const double scaled = juce::MathConstants<double>::pi * cutoff * distance;
                const double sinc = std::abs (scaled) < 1.0e-12
                    ? cutoff : cutoff * std::sin (scaled) / scaled;
                const double normalizedDistance = distance / static_cast<double> (halfTaps);
                const double window = std::abs (normalizedDistance) >= 1.0
                    ? 0.0
                    : 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi
                                            * normalizedDistance)
                           + 0.08 * std::cos (juce::MathConstants<double>::twoPi
                                             * normalizedDistance);
                const double weight = sinc * window;
                const auto inputOffset = sourceFrame - readStart;
                if (inputOffset >= 0 && inputOffset < readFrames)
                {
                    for (int channel = 0; channel < channels; ++channel)
                        weighted[channel] += conversionInput.getSample (
                            channel, static_cast<int> (inputOffset)) * weight;
                    weightSum += weight;
                }
            }
            for (int channel = 0; channel < channels; ++channel)
                output.setSample (channel, outputFrame, static_cast<float> (
                    weightSum == 0.0 ? 0.0 : weighted[channel] / weightSum));
        }
    };
    if (conversionGuard) return conversionGuard (convert);
    convert(); return true;
}
inline void verify (juce::AudioFormatReader& reader)
{
    for (const int rate : { 44100, 48000, 88200, 96000, 47999 })
        for (const auto start : { std::int64_t (0), std::int64_t (1777),
                                 std::int64_t (reader.lengthInSamples * double (rate) / reader.sampleRate) - 17 })
        {
            juce::AudioBuffer<float> expected, actual, oldScratch, newScratch;
            require (uncachedAudio (reader, start, 8192, rate, int (reader.numChannels),
                                   expected, oldScratch, {}), "uncached converter oracle");
            require (hypha::reference_audition::readReferenceVisualAudio (
                reader, start, 8192, rate, int (reader.numChannels), actual, newScratch),
                "bounded coefficient converter");
            for (int channel = 0; channel < actual.getNumChannels(); ++channel)
                require (std::memcmp (expected.getReadPointer (channel), actual.getReadPointer (channel),
                                     8192 * sizeof (float)) == 0,
                         "coefficient cache preserves every PCM bit, including boundaries and cache overflow");
        }
    juce::AudioBuffer<float> output, scratch;
    const int conversionRate = reader.sampleRate == 44100.0 ? 48000 : 44100;
    require (!hypha::reference_audition::readReferenceVisualAudio (
        reader, 0, 128, conversionRate, int (reader.numChannels), output, scratch,
        [] (const auto&) { return false; }), "cancelled resampling job does not publish output");
    require (!hypha::reference_audition::readReferenceVisualAudio (
        reader, -1, 128, 44100, int (reader.numChannels), output, scratch), "negative source position rejected");
    std::cout << "Reference coefficient cache: exact PCM at 5 rates, edges and >512 phases PASS\n";
}

inline void verifyOtherSources (const juce::File& root)
{
    for (const int sourceRate : { 44100, 96000 })
        for (const int channels : { 1, 2 })
        {
            const auto file = root.getNonexistentChildFile ("coefficient-oracle", ".wav", false);
            juce::AudioBuffer<float> audio (channels, 32000);
            for (int channel = 0; channel < channels; ++channel)
                for (int frame = 0; frame < audio.getNumSamples(); ++frame)
                    audio.setSample (channel, frame,
                        float (std::sin (frame * 0.123 + channel) * 0.3));
            {
                juce::WavAudioFormat format;
                auto stream = file.createOutputStream();
                std::unique_ptr<juce::AudioFormatWriter> writer (format.createWriterFor (
                    stream.release(), sourceRate, static_cast<unsigned int> (channels), 32, {}, 0));
                require (writer && writer->writeFromAudioSampleBuffer (audio, 0, audio.getNumSamples()),
                         "disposable coefficient oracle source");
            }
            juce::AudioFormatManager formats;
            formats.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
            require (reader != nullptr, "coefficient oracle reader");
            verify (*reader);
            reader.reset();
            require (file.deleteFile(), "coefficient oracle cleanup");
        }
}

}
