#include "FixedValidationDelay.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <algorithm>

namespace
{
using hypha::pdc_validation::FixedValidationDelay;

class PdcValidationDelayProcessor final : public juce::AudioProcessor
{
public:
    PdcValidationDelayProcessor()
        : juce::AudioProcessor (BusesProperties()
              .withInput ("Input", juce::AudioChannelSet::stereo(), true)
              .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    {
        setLatencySamples (FixedValidationDelay::latencySamples);
    }

    void prepareToPlay (double sampleRate, int) override
    {
        currentSampleRate = sampleRate;
        delay.prepare (getTotalNumInputChannels());
        setLatencySamples (FixedValidationDelay::latencySamples);
    }

    void releaseResources() override {}

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override
    {
        const auto input = layouts.getMainInputChannelSet();
        return input == layouts.getMainOutputChannelSet()
            && (input == juce::AudioChannelSet::mono()
                || input == juce::AudioChannelSet::stereo());
    }

    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals noDenormals;
        const auto channels = getTotalNumInputChannels();
        const auto outputs = getTotalNumOutputChannels();
        const auto frames = buffer.getNumSamples();
        for (int channel = channels; channel < outputs; ++channel)
            buffer.clear (channel, 0, frames);
        if (! delay.process (buffer.getArrayOfWritePointers(), channels, frames))
        {
            for (int channel = 0; channel < channels; ++channel)
                buffer.clear (channel, 0, frames);
        }
    }

    void processBlock (juce::AudioBuffer<double>& buffer, juce::MidiBuffer&) override
    {
        buffer.clear();
    }
    bool supportsDoublePrecisionProcessing() const override { return false; }

    void reset() override
    {
        delay.reset();
    }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override
    {
        return currentSampleRate > 0.0
            ? static_cast<double> (FixedValidationDelay::latencySamples) / currentSampleRate : 0.0;
    }
    bool hasEditor() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

private:
    FixedValidationDelay delay;
    double currentSampleRate = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PdcValidationDelayProcessor)
};
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PdcValidationDelayProcessor();
}
