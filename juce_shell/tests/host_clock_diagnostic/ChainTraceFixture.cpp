#include "ChainTraceFixture.h"
#include "../../src/chain_trace/ChainTraceWriter.h"
#include <juce_audio_processors/juce_audio_processors.h>

#ifndef KIRIN_CHAIN_TRACE_SOURCE
#define KIRIN_CHAIN_TRACE_SOURCE 0
#endif

// Standalone diagnostics only; never linked into Hypha or its normal build graph.
class ChainTraceFixture final : public juce::AudioProcessor, private juce::Timer
{
public:
    ChainTraceFixture() : AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
       #if KIRIN_CHAIN_TRACE_SOURCE
        addParameter (armed = new juce::AudioParameterBool ({ "armed", 1 }, "Arm diagnostic source", false));
       #else
        addParameter (amount = new juce::AudioParameterInt ({ "samples", 1 }, "Delay samples", 0, Delay::maximum, 0));
        addParameter (report = new juce::AudioParameterBool ({ "report", 1 }, "Report latency (off is negative control)", true));
        startTimerHz (50);
       #endif
    }
    ~ChainTraceFixture() override { stopTimer(); }
    void prepareToPlay (double value, int) override { rate = value; delay.reset(); }
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& b) const override
    { return b.getMainInputChannelSet() == b.getMainOutputChannelSet()
        && (b.getMainInputChannelSet() == juce::AudioChannelSet::mono()
            || b.getMainInputChannelSet() == juce::AudioChannelSet::stereo()); }
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        const auto command = published.load (std::memory_order_acquire);
        const auto samples = static_cast<int> (command & 0x1ffffu);
        const auto reported = static_cast<int> ((command >> 17) & 0x1ffffu);
        hypha::chain_trace::Row* row = trace ? trace->begin (buffer.getArrayOfReadPointers(),
            buffer.getNumChannels(), buffer.getNumSamples()) : nullptr;
        bool playing = false;
        hypha::HostProcessClock clock;
        if (const auto* ph = getPlayHead()) if (const auto pos = ph->getPosition())
        {
            playing = pos->getIsPlaying(); clock.playing = playing;
            if (const auto v = pos->getTimeInSamples()) { clock.hasPosition = true; clock.positionSamples = *v; }
        }
       #if KIRIN_CHAIN_TRACE_SOURCE
        source.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(),
                        armed->get() && playing && ! isNonRealtime());
       #else
        delay.process (buffer.getArrayOfWritePointers(), buffer.getNumChannels(), buffer.getNumSamples(), samples);
       #endif
        if (row != nullptr)
        {
            row->role = KIRIN_CHAIN_TRACE_SOURCE ? 3 : 2; row->wrapper = static_cast<int> (wrapperType);
            row->rate = rate; row->host = clock; row->block.playing = playing;
            row->bypassed = false; row->offline = isNonRealtime(); row->reset = 0;
            row->active = row->selected = row->blind = row->contentHeld = false;
            row->preGain = row->postGain = 1;
            row->fixtureDelay = samples; row->fixtureReported = reported;
            row->fixtureGeneration = command >> 34;
            trace->finish (buffer.getArrayOfReadPointers());
        }
    }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }
    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return new juce::GenericAudioProcessorEditor (*this); }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
private:
    void timerCallback() override
    {
       #if ! KIRIN_CHAIN_TRACE_SOURCE
        const auto wanted = amount->get(), latency = report->get() ? wanted : 0;
        const auto previous = published.load (std::memory_order_relaxed);
        if (wanted == static_cast<int> (previous & 0x1ffffu)
            && latency == static_cast<int> ((previous >> 17) & 0x1ffffu)) return;
        setLatencySamples (latency); // Host notification on the message thread, never audio IO.
        const auto generation = (previous >> 34) + 1;
        published.store ((generation << 34) | (static_cast<std::uint64_t> (latency) << 17)
            | static_cast<std::uint64_t> (wanted), std::memory_order_release);
       #endif
    }
    using Delay = hypha::chain_trace_fixture::Delay;
    Delay delay;
    hypha::chain_trace_fixture::Source source;
    std::unique_ptr<hypha::chain_trace::Recorder> trace = hypha::chain_trace::Recorder::fromEnvironment();
    std::atomic<std::uint64_t> published { 0 };
   #if KIRIN_CHAIN_TRACE_SOURCE
    juce::AudioParameterBool* armed = nullptr;
   #else
    juce::AudioParameterBool* report = nullptr;
    juce::AudioParameterInt* amount = nullptr;
   #endif
    double rate = 0;
};
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChainTraceFixture(); }
