#include "ProductTestClock.h"
#include "AnalysisWorkerReceipt.h"
#include "ReferenceValidationStorageSandbox.h"
#include <iostream>
#include "TestProcessorStorage.h"
namespace
{
using Processor = KirinHyphaProcessorBase;
void require (bool ok, const char* why) { if (! ok) throw std::runtime_error (why); }
struct Host : juce::AudioPlayHead
{
    juce::Optional<PositionInfo> getPosition() const override
    {
        PositionInfo p;
        p.setIsPlaying (true); p.setTimeInSamples (position);
        p.setKirinAuxiliaryClockSource (source);
        p.setKirinAuxiliaryClockSamples (position + 960000);
        p.setKirinAuUsesHostTransportTimeline (true);
        return p;
    }
    std::int64_t position = 0;
    int source = 1;
};

void verify (Processor::Role role, int channels, int delayMs, bool realControl)
{
    hypha::tests::ProductTestClock clock;
    Host host;
    juce::AudioProcessor::setTypeOfNextNewPlugin (juce::AudioProcessor::wrapperType_VST3);
    Processor processor (role);
    auto layout = processor.getBusesLayout();
    const auto set = channels == 1 ? juce::AudioChannelSet::mono() : juce::AudioChannelSet::stereo();
    layout.inputBuses.set (0, set); layout.outputBuses.set (0, set);
    require (processor.setBusesLayout (layout), "layout admitted");
    constexpr int frames = 2048;
    processor.setPlayHead (&host); hypha::tests::prepareFixtureProcessor (processor, 96000, frames);
    require (LiveTimingFixtureAccess::configureVst3Clock (processor, "REAPER", "7.71"), "host policy");
    require (! LiveTimingFixtureAccess::timingRingPublished (processor), "regression requires no shared memory");
    juce::AudioBuffer<float> buffer (channels, frames);
    juce::MidiBuffer midi;
    for (int block = 0; block < 6; ++block)
    {
        std::this_thread::sleep_for (std::chrono::milliseconds (delayMs));
        if (realControl) hypha::live_compare::testCallbackNanos.store (0);
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < frames; ++i) buffer.setSample (ch, i, 0.125f);
        processor.processBlock (buffer, midi);
        hypha::tests::awaitAnalysisInput (processor);
        const auto reading = LiveTimingFixtureAccess::aaxLocalClock (processor);
        require (reading.valid && reading.start == host.position,
                 "slow callback must retain the actual processor's analysis clock");
        for (int ch = 0; ch < channels; ++ch)
            for (int i = 0; i < frames; ++i)
                require (buffer.getSample (ch, i) == 0.125f, "normal A remains bit identical");
        require (processor.getLatencySamples() == 0, "zero latency");
        host.position += frames; clock.advanceBlock (frames, 96000);
    }
    clock.advance (std::chrono::milliseconds (600));
    processor.processBlock (buffer, midi);
    require (! LiveTimingFixtureAccess::aaxLocalClock (processor).valid, "explicit gap retires analysis");
    processor.releaseResources();
}
}
int main (int argc, char**)
try
{
    ReferenceValidationStorageSandbox sandbox;
    juce::ScopedJuceInitialiser_GUI init;
    // The optional negative control uses the old real-clock route under exactly the same
    // assertion; it must fail with 175 ms callbacks. It is not a normal CTest item.
    if (argc > 1) { verify (Processor::Role::Pre, 2, 175, true); return 0; }
    for (auto role : { Processor::Role::Pre, Processor::Role::Post })
        for (int channels : { 1, 2 })
            for (int delayMs : { 0, 40, 175 }) verify (role, channels, delayMs, false);
    std::cout << "processor clock: PRE/POST mono/stereo, 0/40/175 ms, gap, bit identity and zero latency PASS\n";
}
catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
