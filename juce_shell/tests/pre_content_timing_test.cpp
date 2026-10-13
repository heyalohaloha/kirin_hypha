#include "../src/PluginProcessor.h"
#include "ValidationStorageSandbox.h"
#include "LiveTimingFixtureAccess.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include "TestProcessorStorage.h"
struct Clock : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo pos; pos.setIsPlaying (true); pos.setTimeInSamples (position);
        pos.setKirinAuxiliaryClockSource (source); pos.setKirinAuxiliaryClockSamples (position);
        return pos;
    }
    juce::int64 position = 0;
    int source = 2;
};
static void require (bool ok, const char* message)
{ if (! ok) { std::cerr << "PRE content timing: " << message << '\n'; std::exit (1); } }
static void verifyAaxReset()
{
    using Processor = KirinHyphaProcessorBase;
    for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        for (int channels : { 1, 2 })
            for (int mode = 0; mode < 3; ++mode)
            {
                juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AAX);
                Processor processor (role);
                auto layout = processor.getBusesLayout();
                const auto channelSet = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
                layout.inputBuses.set (0, channelSet); layout.outputBuses.set (0, channelSet);
                require (processor.setBusesLayout (layout), "AAX reset fixture negotiates mono/stereo");
                Clock clock; clock.source = 3; clock.position = 960000;
                processor.setPlayHead (&clock); hypha::tests::prepareFixtureProcessor (processor, 48000, 256);
                LiveTimingFixtureAccess::configureAaxClock (processor);
                juce::AudioBuffer<float> buffer (channels, 256), original (channels, 256);
                for (int channel = 0; channel < channels; ++channel)
                    for (int i = 0; i < 256; ++i)
                        buffer.setSample (channel, i, static_cast<float> (i - 128) / 512.0f);
                original.makeCopyOf (buffer);
                juce::MidiBuffer midi;
                const auto block = [&] {
                    processor.processBlock (buffer, midi); clock.position += 256;
                    for (int channel = 0; channel < channels; ++channel)
                        require (std::memcmp (buffer.getReadPointer (channel), original.getReadPointer (channel),
                                             256 * sizeof (float)) == 0, "AAX reset keeps normal A bit identical");
                    require (processor.getLatencySamples() == 0, "AAX reset retains zero normal latency");
                };
                for (int i = 0; i < 4; ++i)
                    block();
                const auto before = LiveTimingFixtureAccess::aaxLocalClock (processor);
                require (before.valid, "continuous AddClock establishes local analysis");
                processor.kirinHostTimelineReset (mode != 2, mode == 1);
                block();
                require (! LiveTimingFixtureAccess::aaxLocalClock (processor).valid,
                         "host reset fences AAX analysis even when the next clock is continuous");
                block();
                const auto after = LiveTimingFixtureAccess::aaxLocalClock (processor);
                require (after.valid && after.revision != before.revision,
                         "fresh continuity resumes AAX analysis in a new revision");
                processor.releaseResources();
            }
}
int main()
{
    ValidationStorageSandbox sandbox;
   #if KIRIN_HYPHA_PRE_DISPLAY
    hypha::pre_display::Controller::placeUnderForTest (sandbox.directory());
   #endif
    juce::ScopedJuceInitialiser_GUI initialise;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_AudioUnit);
    KirinHyphaProcessorBase pre (KirinHyphaProcessorBase::Role::Pre);
    Clock clock; pre.setPlayHead (&clock); hypha::tests::prepareFixtureProcessor (pre, 96000, 512);
    hypha::live_compare::OffsetEstimate e;
    e.determined = true; e.sampleRate = 96000; e.peak = 0.4; e.dominance = 20;
    require (! pre.holdLiveCompareForContentJump (e), "PRE can never acquire a POST content hold");
    juce::AudioBuffer<float> buffer (2, 512), original (2, 512);
    juce::MidiBuffer midi;
    for (int mode = 0; mode < 4; ++mode)
    {
        if (mode != 0) pre.kirinHostTimelineReset (mode != 3, mode == 2);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 512; ++i) buffer.setSample (channel, i, static_cast<float> (i - 256) / 1024.0f);
        original.makeCopyOf (buffer);
        pre.processBlock (buffer, midi);
        for (int channel = 0; channel < 2; ++channel)
            require (std::memcmp (buffer.getReadPointer (channel), original.getReadPointer (channel), 512 * sizeof (float)) == 0,
                     "normal A stays bit identical across stopped/playing/unknown reset notifications");
        require (pre.getLatencySamples() == 0 && ! pre.liveCompareStatus().contentHeld,
                 "reset does not add PRE latency or audition authority");
        clock.position += 512;
    }
    pre.releaseResources();
    verifyAaxReset();
    std::cout << "PRE content timing: PASS 4 notification/audio + 12 AAX reset cases\n";
}
