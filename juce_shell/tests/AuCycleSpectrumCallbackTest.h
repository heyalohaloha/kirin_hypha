#pragma once
#include "LiveTimingFixtureAccess.h"
#include <chrono>
#include <cmath>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace hypha::tests
{
inline void verifyAuCycleSpectrumCallbacks()
{
    struct Clock final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo info;
            info.setIsPlaying (true);
            info.setTimeInSamples (position);
            // AU render fallback supplies a local clock, but no independent pair witness.
            info.setKirinAuUsesHostTransportTimeline (! cycle);
            info.setKirinAuxiliaryClockSource (2);
            info.setKirinAuxiliaryClockSamples (position);
            info.setIsLooping (cycle);
            info.setBpm (120); info.setLoopPoints (LoopPoints { 0, 1 });
            info.setPpqPosition ((position % 24000) / 24000.0);
            return info;
        }
        std::int64_t position = 0;
        bool cycle = true;
    } clock;
    const auto require = [] (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); };
    for (int channels : { 1, 2 })
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AudioUnit);
        KirinHyphaProcessorBase post (KirinHyphaProcessorBase::Role::Post);
        auto layout = post.getBusesLayout();
        const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
        layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
        require (post.setBusesLayout (layout), "AU FREQ fixture layout");
        post.setPlayHead (&clock); post.prepareToPlay (48000, 256);
        LiveTimingFixtureAccess::enableFixtureWrites (post);
        const auto owner = post.beginAnalysisUiSession();
        require (post.setAnalysisDemand (owner, { analysis::Kind::spectrum, 0 }), "AU FREQ demand");
        clock.position = 0;
        for (bool cycle : { true, false })
        {
            clock.cycle = cycle;
            std::int64_t latest = -1;
            int observations = 0;
            for (int callback = 0; callback < 220; ++callback)
            {
                juce::AudioBuffer<float> audio (channels, 256);
                for (int ch = 0; ch < channels; ++ch)
                    for (int i = 0; i < 256; ++i)
                        audio.setSample (ch, i, 0.1f * std::sin (static_cast<float> (clock.position + i) * 0.13f));
                juce::AudioBuffer<float> original; original.makeCopyOf (audio);
                juce::MidiBuffer midi; post.processBlock (audio, midi);
                for (int ch = 0; ch < channels; ++ch)
                    require (std::memcmp (audio.getReadPointer (ch), original.getReadPointer (ch),
                                         256 * sizeof (float)) == 0, "AU FREQ preserves normal A bytes");
                require (post.getLatencySamples() == 0, "AU FREQ adds no latency");
                require (post.analysisPairingLoopBlocked() == cycle, "first Cycle has no correspondence proof");
                if (cycle) require (! LiveTimingFixtureAccess::aaxLocalClock (post).valid,
                                    "fixture exercises the unavailable comparison-local clock");
                LiveTimingFixtureAccess::serviceProcessorTimer (post);
                std::this_thread::sleep_for (std::chrono::milliseconds (6));
                KirinSpectrumBatch batch {};
                if (post.pollSpectrumBatch (batch))
                {
                    require (batch.latest.has_data == 0, "unpaired AU cannot publish delta");
                    if (batch.latest.post_has_data != 0)
                    {
                        require (batch.latest.channels == channels, "POST spectrum retains its layout");
                        require (std::all_of (std::begin (batch.latest.post_dbfs), std::end (batch.latest.post_dbfs),
                                             [] (float value) { return std::isfinite (value); }), "finite POST spectrum");
                        latest = batch.latest.presentation_end_samples;
                        ++observations;
                    }
                }
                if (callback >= 80)
                    require (latest > clock.position - 8000 && latest <= clock.position + 256,
                             "POST FREQ keeps updating through first Cycle and Cycle off");
                clock.position += 256;
            }
            require (observations > 140, "sustained AU POST FREQ publication");
        }
        post.endAnalysisUiSession (owner); post.releaseResources();
    }
    std::cout << "AU Cycle FREQ callbacks: mono/stereo local observation and Cycle off PASS\n";
}
}
