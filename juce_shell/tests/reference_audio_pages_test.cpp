#include "../src/reference_audition/ReferenceAudioPages.h"
#include "ReferenceConversionParityChecks.h"
#include "ReferenceShortLoopPagesTest.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <thread>

namespace ref = hypha::reference_audition;

namespace
{
    bool ownershipHookCalled = false;

    void replaceAnotherPageAfterOwnership (ref::AudioPages& pages)
    {
        ownershipHookCalled = true;
        ref::AudioPages::setAcquireOwnershipHookForTest (nullptr);
        pages.request (336'000);
        pages.service();
    }

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

        bool unreadableFirstPage = false;

        static float valueAt (int channel, std::int64_t position)
        {
            return static_cast<float> ((channel + 1) * 0.1
                                       + static_cast<double> (position % 1000) / 10'000.0);
        }

        bool readSamples (int* const* destination, int destinationChannels,
                          int destinationOffset, juce::int64 sourceStart,
                          int frames) override
        {
            if (unreadableFirstPage && sourceStart == 0) return false;
            for (int channel = 0; channel < destinationChannels; ++channel)
            {
                auto* samples = reinterpret_cast<float*> (destination[channel]);
                for (int frame = 0; frame < frames; ++frame)
                    samples[destinationOffset + frame] = valueAt (channel, sourceStart + frame);
            }
            return true;
        }
    };

    class SineReader final : public juce::AudioFormatReader
    {
    public:
        SineReader (double rate, double frequency, float amplitude,
                    int channels, std::int64_t length)
            : juce::AudioFormatReader (nullptr, "sine"),
              toneFrequency (frequency), toneAmplitude (amplitude)
        {
            sampleRate = rate;
            numChannels = static_cast<unsigned int> (channels);
            lengthInSamples = length;
            bitsPerSample = 32;
            usesFloatingPointData = true;
        }

        bool readSamples (int* const* destination, int destinationChannels,
                          int destinationOffset, juce::int64 sourceStart,
                          int frames) override
        {
            for (int channel = 0; channel < destinationChannels; ++channel)
            {
                auto* samples = reinterpret_cast<float*> (destination[channel]);
                for (int frame = 0; frame < frames; ++frame)
                {
                    const auto position = static_cast<double> (sourceStart + frame);
                    samples[destinationOffset + frame] = toneAmplitude * static_cast<float> (
                        std::sin (juce::MathConstants<double>::twoPi * toneFrequency
                                  * position / sampleRate));
                }
            }
            return true;
        }

    private:
        const double toneFrequency;
        const float toneAmplitude;
    };

    bool closeEnough (float left, float right)
    {
        return std::abs (left - right) < 1.0e-6f;
    }
}

int main()
{
    verifyShortLoopPages (require);
    verifyReferenceConversionParity<TestReader> (require);
    {
        ref::AudioPages recovered { ref::AudioPages::ServiceMode::manual };
        auto reader = std::make_unique<TestReader> (48'000.0, 2, 480'000);
        auto* const decoder = reader.get();
        decoder->unreadableFirstPage = true;
        require (recovered.installReaderForTest (std::move (reader), 48'000.0, 2).isEmpty(),
                 "a failing page leaves the source open for retries");
        recovered.service(); recovered.service();
        require (! recovered.readyAt (0, 1) && recovered.hasPersistentReadFailure (0),
                 "three actual reads establish failure of the requested page");
        decoder->unreadableFirstPage = false;
        recovered.request (48'000); recovered.service();
        require (recovered.readyAt (0, 1) && ! recovered.hasPersistentReadFailure (0),
                 "a successful prefetch clears earlier page read failures");
    }
    ref::AudioPages pages { ref::AudioPages::ServiceMode::manual };
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

    std::atomic<bool> observe { true };
    std::thread observer ([&]
    {
        while (observe.load (std::memory_order_acquire))
            require (pages.readyAt (100, 1),
                     "non-owning readiness observation must remain stable");
    });
    juce::AudioBuffer<float> observedOutput (2, 1);
    int observationFailures = 0;
    for (int attempt = 0; attempt < 100'000; ++attempt)
        if (! pages.render (observedOutput, 100, 1.0f))
            ++observationFailures;
    observe.store (false, std::memory_order_release);
    observer.join();
    require (observationFailures == 0,
             "readiness observation must never consume the audio page lease");

    ref::AudioPages::setAcquireOwnershipHookForTest (replaceAnotherPageAfterOwnership);
    output.clear();
    require (pages.render (output, 100, 1.0f) && ownershipHookCalled
             && closeEnough (output.getSample (0, 0), TestReader::valueAt (0, 100)),
             "a page owned by the audio thread must keep its identity while the worker fills");

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
    require (! pages.render (miss, 200'000, 1.0f),
             "uncached transport jump must fail closed to caller-owned A");
    require (closeEnough (miss.getSample (0, 0), before),
             "cache miss must not partially overwrite A");
    pages.request (200'000);
    pages.service();
    require (pages.render (miss, 200'000, 1.0f),
             "non-RT service must make the requested jump available");

    ref::AudioPages continuity { ref::AudioPages::ServiceMode::manual };
    require (continuity.installReaderForTest (
                 std::make_unique<TestReader> (48'000.0, 2, 480'000), 48'000.0, 2).isEmpty(),
             "continuity source must open");
    for (std::int64_t second = 0; second < 9; ++second)
    {
        const auto position = second * 48'000;
        continuity.request (position);
        continuity.service();
        juce::AudioBuffer<float> continuousOutput (2, 512);
        continuousOutput.clear();
        require (continuity.readyAt (position, continuousOutput.getNumSamples())
                 && continuity.render (continuousOutput, position, 1.0f),
                 "cache lookahead must not evict the current page after one cache rotation");
    }

    continuity.setPinnedCue (100, 1'000, true);
    continuity.request (999);
    continuity.service();
    juce::AudioBuffer<float> cueLoop (2, 8);
    cueLoop.clear();
    require (continuity.renderCue (cueLoop, 999, 100, 1'000, true, 1.0f),
             "a callback crossing a looping cue end must render atomically");
    require (closeEnough (cueLoop.getSample (0, 0), TestReader::valueAt (0, 999))
             && closeEnough (cueLoop.getSample (0, 1), TestReader::valueAt (0, 100)),
             "a looping cue must wrap inside the callback without reading outside the cue");
    juce::AudioBuffer<float> noLoop (2, 8);
    noLoop.clear();
    noLoop.setSample (0, 0, 0.75f);
    require (! continuity.renderCue (noLoop, 999, 100, 1'000, false, 1.0f)
             && noLoop.getSample (0, 0) == 0.75f,
             "a non-looping cue crossing must leave the complete A buffer untouched");

    juce::AudioBuffer<float> beyondEnd (2, 512);
    beyondEnd.clear();
    require (! pages.readyAt (479'600, beyondEnd.getNumSamples())
             && ! pages.render (beyondEnd, 479'600, 1.0f),
             "a callback extending past source EOF must fail closed instead of rendering padded audio");

    pages.close();
    require (! pages.sourceOpen() && ! pages.render (miss, 0, 1.0f),
             "closed source must keep A untouched");
    require (pages.installReaderForTest (
                 std::make_unique<TestReader> (44'100.0, 2, 441'000), 48'000.0, 2)
                 == "sample_rate_unsupported",
             "sample-rate conversion must require one explicit approval");
    require (pages.installReaderForTest (
                 std::make_unique<TestReader> (44'100.0, 2, 441'000), 48'000.0, 2, true).isEmpty(),
             "approved sample-rate conversion must prepare a bounded runtime view");
    require (pages.lengthInSamples() == 480'000 && pages.readyAt (0, 512),
             "approved conversion must expose the host-rate clock and initial audio page");
    juce::AudioBuffer<float> converted (2, 512);
    converted.clear();
    require (pages.render (converted, 0, 1.0f)
             && std::isfinite (converted.getSample (0, 511)),
             "approved conversion must render finite audio without changing the source file");

    constexpr int calibrationFrames = 8'192;
    require (pages.installReaderForTest (
                 std::make_unique<SineReader> (44'100.0, 1'000.0, 0.5f, 2, 441'000),
                 48'000.0, 2, true).isEmpty(),
             "upsampling calibration source must open after approval");
    juce::AudioBuffer<float> passband (2, calibrationFrames);
    passband.clear();
    require (pages.render (passband, 0, 1.0f),
             "upsampling calibration source must render");
    double maximumPassbandError = 0.0;
    for (int frame = 128; frame < calibrationFrames; ++frame)
    {
        const auto expected = 0.5 * std::sin (
            juce::MathConstants<double>::twoPi * 1'000.0 * frame / 48'000.0);
        maximumPassbandError = std::max (
            maximumPassbandError,
            std::abs (static_cast<double> (passband.getSample (0, frame)) - expected));
    }
    require (maximumPassbandError < 0.002,
             "approved conversion must preserve a 1 kHz passband tone within 0.002 peak error");

    require (pages.installReaderForTest (
                 std::make_unique<SineReader> (96'000.0, 30'000.0, 0.5f, 2, 960'000),
                 48'000.0, 2, true).isEmpty(),
             "downsampling calibration source must open after approval");
    juce::AudioBuffer<float> stopband (2, calibrationFrames);
    stopband.clear();
    require (pages.render (stopband, 0, 1.0f),
             "downsampling calibration source must render");
    double stopbandEnergy = 0.0;
    int stopbandSamples = 0;
    for (int frame = 256; frame < calibrationFrames; ++frame)
    {
        const auto sample = static_cast<double> (stopband.getSample (0, frame));
        stopbandEnergy += sample * sample;
        ++stopbandSamples;
    }
    const auto stopbandRms = std::sqrt (stopbandEnergy / stopbandSamples);
    require (stopbandRms < 0.002,
             "approved downsampling must suppress a 30 kHz out-of-band tone below 0.002 RMS");

    // A mono source on a stereo track plays its one channel on both sides, exactly as the
    // source (no gain, no downmix); a stereo source on a mono track is still refused.
    {
        ref::AudioPages monoOnStereo { ref::AudioPages::ServiceMode::manual };
        require (monoOnStereo.installReaderForTest (
                     std::make_unique<TestReader> (48'000.0, 1, 480'000), 48'000.0, 2).isEmpty(),
                 "a mono source must open on a stereo track");
        require (monoOnStereo.sourceFileChannels() == 1 && monoOnStereo.playbackLoudnessOffsetLu() > 3.0102
                     && monoOnStereo.playbackLoudnessOffsetLu() < 3.0104,
                 "a mono source on a stereo track must report the BS.1770 dual-mono offset");
        juce::AudioBuffer<float> both (2, 512);
        both.clear();
        require (monoOnStereo.render (both, 100, 1.0f), "a mono source must render on a stereo track");
        bool identical = true;
        for (int frame = 0; frame < 512; ++frame)
            identical = identical && both.getSample (0, frame) == TestReader::valueAt (0, 100 + frame)
                && both.getSample (1, frame) == both.getSample (0, frame);
        require (identical, "a mono source must play bit identical on both channels");

        // Converted rate: each side equals the one-channel conversion of the same source.
        ref::AudioPages convertedStereo { ref::AudioPages::ServiceMode::manual };
        ref::AudioPages convertedMono { ref::AudioPages::ServiceMode::manual };
        require (convertedStereo.installReaderForTest (
                     std::make_unique<SineReader> (44'100.0, 997.0, 0.5f, 1, 441'000), 48'000.0, 2, true).isEmpty()
                 && convertedMono.installReaderForTest (
                     std::make_unique<SineReader> (44'100.0, 997.0, 0.5f, 1, 441'000), 48'000.0, 1, true).isEmpty(),
                 "a converted mono source must open on stereo and mono tracks");
        juce::AudioBuffer<float> convertedBoth (2, 512), convertedOne (1, 512);
        convertedBoth.clear();
        convertedOne.clear();
        require (convertedStereo.render (convertedBoth, 4'800, 1.0f)
                     && convertedMono.render (convertedOne, 4'800, 1.0f),
                 "converted mono sources must render");
        bool convertedIdentical = true;
        for (int frame = 0; frame < 512; ++frame)
            convertedIdentical = convertedIdentical
                && convertedBoth.getSample (0, frame) == convertedOne.getSample (0, frame)
                && convertedBoth.getSample (1, frame) == convertedOne.getSample (0, frame);
        require (convertedIdentical, "a converted mono source must play the same samples on both channels");
        require (convertedMono.playbackLoudnessOffsetLu() == 0.0,
                 "a mono source on a mono track has no loudness offset");
        // B's display meters read the source the same way, at the same rate and converted.
        for (const int displayRate : { 48'000, 44'100 })
        {
            TestReader monoReader (48'000.0, 1, 480'000);
            juce::AudioBuffer<float> shown, scratch;
            require (ref::readReferenceVisualAudio (monoReader, 1'000, 512, displayRate, 2, shown, scratch),
                     "the display reader reads a mono source for a stereo track");
            bool shownIdentical = true;
            for (int frame = 0; frame < 512; ++frame)
                shownIdentical = shownIdentical && shown.getSample (1, frame) == shown.getSample (0, frame)
                    && (displayRate != 48'000 || shown.getSample (0, frame) == TestReader::valueAt (0, 1'000 + frame));
            require (shownIdentical, "the display reader gives both channels the same mono samples");
        }
        ref::AudioPages stereoOnStereo { ref::AudioPages::ServiceMode::manual };
        require (stereoOnStereo.installReaderForTest (
                     std::make_unique<TestReader> (48'000.0, 2, 480'000), 48'000.0, 2).isEmpty()
                     && stereoOnStereo.sourceFileChannels() == 2 && stereoOnStereo.playbackLoudnessOffsetLu() == 0.0,
                 "a stereo source on a stereo track has no loudness offset");
    }
    require (pages.installReaderForTest (
                 std::make_unique<TestReader> (48'000.0, 2, 480'000), 48'000.0, 1)
                 == "channel_layout_unsupported",
             "a stereo source on a mono track must refuse instead of fabricating a downmix");

    std::cout << "Reference audio page tests passed\n";
    return 0;
}
