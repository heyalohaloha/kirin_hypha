#pragma once
#include "../src/PluginProcessor.h"
#include <cstring>
#include <stdexcept>
#include "TestProcessorStorage.h"
namespace hypha::tests
{
// A callback without a PRE witness must never manufacture a pair. This small processor test
// also runs where named shared-memory mappings are unavailable; paired PCM is tested separately.
inline void verifyAuCycleCallbackGuidance()
{
    struct Clock final : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo p;
            p.setIsPlaying (true);
            p.setTimeInSamples (position);
            p.setKirinAuUsesHostTransportTimeline (true);
            p.setKirinAuxiliaryClockSource (2);
            p.setKirinAuxiliaryClockSamples (position);
            p.setIsLooping (cycle);
            p.setBpm (120); p.setPpqPosition (position / 24000.0);
            p.setLoopPoints (LoopPoints { 0, 4 });
            return p;
        }
        std::int64_t position = 0;
        bool cycle = true;
    } clock;
    const auto require = [] (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); };
    using Processor = KirinHyphaProcessorBase;
    for (const auto role : { Processor::Role::Pre, Processor::Role::Post })
    {
        juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AudioUnit);
        Processor processor (role);
        processor.setPlayHead (&clock);
        hypha::tests::prepareFixtureProcessor (processor, 48000, 256);
        for (bool cycle : { true, false })
        {
            clock.cycle = cycle;
            juce::AudioBuffer<float> buffer (2, 256);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i) buffer.setSample (ch, i, 0.125f);
            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 256; ++i)
                    require (buffer.getSample (ch, i) == 0.125f, "unverified AU callback preserves POST / normal PRE");
            require (processor.analysisPairingLoopBlocked() == (cycle && role == Processor::Role::Post),
                     "AU Cycle without current PRE proof must publish loop guidance; Cycle off clears it");
            require (! processor.liveCompareStatus().preAudible && processor.getLatencySamples() == 0,
                     "unverified AU callback cannot audition PRE or add latency");
            clock.position += 256;
        }
        processor.releaseResources();
    }
}
}
